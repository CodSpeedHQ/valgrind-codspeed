/*--------------------------------------------------------------------*/
/*--- Tracegrind                                                    ---*/
/*---                                                       dump.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Tracegrind, a Valgrind tool for call tracing.

   Based on callgrind, Copyright (C) 2002-2017, Josef Weidendorfer.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

#include "config.h"
#include "global.h"
#include "tg_lz4.h"
#include "tg_msgpack.h"

#include "pub_tool_libcfile.h"
#include "pub_tool_threadstate.h"

/* Total reads/writes/misses sum over all threads. */
FullCost TG_(total_cost) = 0;

EventMapping* TG_(dumpmap) = 0;

/* ================================================================== */
/* === Trace output                                                === */
/* ================================================================== */

trace_output TG_(trace_out) = {
   .fd = -1, .seq = 0, .initialized = False, .header_written = False};

/* ================================================================== */
/* === MsgPack + LZ4 output                                       === */
/* ================================================================== */

#define MSGPACK_CHUNK_ROWS  4096         /* Rows per compressed chunk */
#define MSGPACK_INITIAL_BUF (256 * 1024) /* Initial buffer size */

/* String intern table: maps string pointer -> integer ID.
 * Since fn_node/obj_node/file_node names are stable interned pointers,
 * we can use the pointer value directly as the hash key.
 */
#define INTERN_HASH_SIZE 4096

typedef struct _intern_entry {
   const HChar*          str;
   Int                   len;
   UInt                  id;
   struct _intern_entry* next;
} intern_entry;

typedef struct {
   intern_entry* buckets[INTERN_HASH_SIZE];
   UInt          next_id;
   /* Ordered list for writing the string table */
   const HChar** strings;
   Int*          lengths;
   UInt          strings_cap;
} intern_table;

typedef struct {
   msgpack_buffer buf;           /* Buffer for serializing rows */
   UInt           rows_in_chunk; /* Number of rows in current chunk */
   UInt           n_event_cols;  /* Number of dynamic event columns */
   const HChar**  col_names;     /* Column names (for header) */
   Int            ncols;         /* Total columns including events */
   UChar*         compress_buf;  /* Persistent compression output buffer */
   SizeT          compress_cap;  /* Capacity of compress_buf */
   intern_table   interns;       /* String interning table */
} msgpack_state;

static msgpack_state mp_state;

/* Intern a string: returns its integer ID. If not seen before, assigns a new
 * ID. */
static UInt intern_string(const HChar* str, Int len)
{
   /* Use pointer value as hash key (strings are interned/stable pointers) */
   UWord         h = ((UWord)str >> 3) % INTERN_HASH_SIZE;
   intern_entry* e = mp_state.interns.buckets[h];
   while (e) {
      if (e->str == str)
         return e->id;
      e = e->next;
   }

   /* New string - assign ID */
   UInt id = mp_state.interns.next_id++;

   /* Store in hash */
   intern_entry* ne            = VG_(malloc)("tg.intern", sizeof(intern_entry));
   ne->str                     = str;
   ne->len                     = len;
   ne->id                      = id;
   ne->next                    = mp_state.interns.buckets[h];
   mp_state.interns.buckets[h] = ne;

   /* Store in ordered list for string table output */
   if (id >= mp_state.interns.strings_cap) {
      UInt new_cap =
         mp_state.interns.strings_cap ? mp_state.interns.strings_cap * 2 : 256;
      mp_state.interns.strings = VG_(realloc)(
         "tg.intern.s", mp_state.interns.strings, new_cap * sizeof(HChar*));
      mp_state.interns.lengths = VG_(realloc)(
         "tg.intern.l", mp_state.interns.lengths, new_cap * sizeof(Int));
      mp_state.interns.strings_cap = new_cap;
   }
   mp_state.interns.strings[id] = str;
   mp_state.interns.lengths[id] = len;

   return id;
}

static void init_intern_table(void)
{
   VG_(memset)(mp_state.interns.buckets, 0, sizeof(mp_state.interns.buckets));
   mp_state.interns.next_id     = 0;
   mp_state.interns.strings     = NULL;
   mp_state.interns.lengths     = NULL;
   mp_state.interns.strings_cap = 0;
}

static void free_intern_table(void)
{
   UInt i;
   for (i = 0; i < INTERN_HASH_SIZE; i++) {
      intern_entry* e = mp_state.interns.buckets[i];
      while (e) {
         intern_entry* next = e->next;
         VG_(free)(e);
         e = next;
      }
   }
   if (mp_state.interns.strings)
      VG_(free)(mp_state.interns.strings);
   if (mp_state.interns.lengths)
      VG_(free)(mp_state.interns.lengths);
}

/* Write a compressed chunk to the trace output */
static void msgpack_flush_chunk(void)
{
   if (mp_state.rows_in_chunk == 0)
      return;
   if (TG_(trace_out).fd < 0)
      return;

   /* Compress the msgpack data with LZ4 */
   SizeT src_size     = mp_state.buf.size;
   SizeT dst_capacity = tg_lz4_compress_bound(src_size);

   /* Grow persistent compression buffer if needed */
   if (dst_capacity > mp_state.compress_cap) {
      if (mp_state.compress_buf)
         VG_(free)(mp_state.compress_buf);
      mp_state.compress_buf = VG_(malloc)("tg.mp.compress", dst_capacity);
      mp_state.compress_cap = dst_capacity;
   }

   SizeT compressed_size = tg_lz4_compress(mp_state.compress_buf, dst_capacity,
                                           mp_state.buf.data, src_size);

   if (compressed_size == 0) {
      return;
   }

   /* Write chunk header: 4 bytes uncompressed size, 4 bytes compressed size */
   UChar hdr[8];
   hdr[0] = (UChar)(src_size & 0xff);
   hdr[1] = (UChar)((src_size >> 8) & 0xff);
   hdr[2] = (UChar)((src_size >> 16) & 0xff);
   hdr[3] = (UChar)((src_size >> 24) & 0xff);
   hdr[4] = (UChar)(compressed_size & 0xff);
   hdr[5] = (UChar)((compressed_size >> 8) & 0xff);
   hdr[6] = (UChar)((compressed_size >> 16) & 0xff);
   hdr[7] = (UChar)((compressed_size >> 24) & 0xff);
   VG_(write)(TG_(trace_out).fd, hdr, 8);

   /* Write compressed data */
   VG_(write)(TG_(trace_out).fd, mp_state.compress_buf, compressed_size);

   /* Reset buffer for next chunk */
   msgpack_reset(&mp_state.buf);
   mp_state.rows_in_chunk = 0;
}

/* Write file header with schema metadata (discriminated union format) */
static void msgpack_write_header(void)
{
   msgpack_buffer hdr;
   msgpack_init(&hdr, 2048);

   /* Header is a map with metadata */
   msgpack_write_map_header(&hdr, 7);

   /* version */
   msgpack_write_key(&hdr, "version");
   msgpack_write_uint(&hdr, 5);

   /* format */
   msgpack_write_key(&hdr, "format");
   msgpack_write_str(&hdr, "tracegrind-msgpack", -1);

   /* creator */
   msgpack_write_key(&hdr, "creator");
   msgpack_write_str(&hdr, "valgrind-tracegrind", -1);

   /* creator_version */
   msgpack_write_key(&hdr, "creator_version");
   msgpack_write_str(&hdr, VERSION, -1);

   /* event_schemas - discriminated union: each event type has its own schema */
   msgpack_write_key(&hdr, "event_schemas");
   msgpack_write_map_header(&hdr, 7); /* 7 event types */

   /* Event type 0 (MARKER) schema */
   msgpack_write_key(&hdr, "0");
   msgpack_write_array_header(&hdr, 4);
   msgpack_write_str(&hdr, "seq", -1);
   msgpack_write_str(&hdr, "tid", -1);
   msgpack_write_str(&hdr, "event", -1);
   msgpack_write_str(&hdr, "marker", -1);

   /* Event types 1-4: 7 fixed columns + "counters" sentinel */
   {
      const HChar* ev_keys[] = {"1", "2", "3", "4"};
      Int          k;
      for (k = 0; k < 4; k++) {
         msgpack_write_key(&hdr, ev_keys[k]);
         msgpack_write_array_header(&hdr, 8);
         msgpack_write_str(&hdr, "seq", -1);
         msgpack_write_str(&hdr, "tid", -1);
         msgpack_write_str(&hdr, "event", -1);
         msgpack_write_str(&hdr, "fn", -1);
         msgpack_write_str(&hdr, "obj", -1);
         msgpack_write_str(&hdr, "file", -1);
         msgpack_write_str(&hdr, "line", -1);
         msgpack_write_str(&hdr, "counters", -1);
      }
   }

   /* Event type 5 (FORK) schema */
   msgpack_write_key(&hdr, "5");
   msgpack_write_array_header(&hdr, 4);
   msgpack_write_str(&hdr, "seq", -1);
   msgpack_write_str(&hdr, "tid", -1);
   msgpack_write_str(&hdr, "event", -1);
   msgpack_write_str(&hdr, "child_pid", -1);

   /* Event type 6 (THREAD_CREATE) schema */
   msgpack_write_key(&hdr, "6");
   msgpack_write_array_header(&hdr, 4);
   msgpack_write_str(&hdr, "seq", -1);
   msgpack_write_str(&hdr, "tid", -1);
   msgpack_write_str(&hdr, "event", -1);
   msgpack_write_str(&hdr, "child_tid", -1);

   /* counters - array of dynamic counter column names */
   msgpack_write_key(&hdr, "counters");
   msgpack_write_array_header(&hdr, mp_state.n_event_cols);
   {
      Int i;
      for (i = 7; i < mp_state.ncols; i++) {
         msgpack_write_str(&hdr, mp_state.col_names[i], -1);
      }
   }

   /* counter_units - map from counter name to unit string.
      Following callgrind's convention: only time counters get units. */
   msgpack_write_key(&hdr, "counter_units");
   {
      Int          n_units  = 0;
      const HChar* unit_str = NULL;
      switch (TG_(clo).collect_systime) {
      case systime_no:
         break;
      case systime_msec:
         unit_str = "ms";
         n_units  = 1;
         break;
      case systime_usec:
         unit_str = "us";
         n_units  = 1;
         break;
      case systime_nsec:
         unit_str = "ns";
         n_units  = 2;
         break;
      }
      msgpack_write_map_header(&hdr, n_units);
      if (unit_str) {
         msgpack_write_key(&hdr, "sysTime");
         msgpack_write_str(&hdr, unit_str, -1);
         if (TG_(clo).collect_systime == systime_nsec) {
            msgpack_write_key(&hdr, "sysCpuTime");
            msgpack_write_str(&hdr, unit_str, -1);
         }
      }
   }

   /* Compress and write header chunk */
   SizeT  src_size     = hdr.size;
   SizeT  dst_capacity = tg_lz4_compress_bound(src_size);
   UChar* compressed   = VG_(malloc)("tg.mp.hdr", dst_capacity);

   SizeT compressed_size =
      tg_lz4_compress(compressed, dst_capacity, hdr.data, src_size);

   /* Magic + version (8 bytes): "TGMP" + version(5) */
   UChar magic[8] = {'T', 'G', 'M', 'P', 0x05, 0x00, 0x00, 0x00};
   VG_(write)(TG_(trace_out).fd, magic, 8);

   /* Header chunk size (4 bytes uncompressed, 4 bytes compressed) */
   UChar hdr_size[8];
   hdr_size[0] = (UChar)(src_size & 0xff);
   hdr_size[1] = (UChar)((src_size >> 8) & 0xff);
   hdr_size[2] = (UChar)((src_size >> 16) & 0xff);
   hdr_size[3] = (UChar)((src_size >> 24) & 0xff);
   hdr_size[4] = (UChar)(compressed_size & 0xff);
   hdr_size[5] = (UChar)((compressed_size >> 8) & 0xff);
   hdr_size[6] = (UChar)((compressed_size >> 16) & 0xff);
   hdr_size[7] = (UChar)((compressed_size >> 24) & 0xff);
   VG_(write)(TG_(trace_out).fd, hdr_size, 8);

   /* Compressed header data */
   VG_(write)(TG_(trace_out).fd, compressed, compressed_size);

   VG_(free)(compressed);
   msgpack_free(&hdr);
}

/* Initialize msgpack state with schema from event sets */
static void msgpack_init_state(void)
{
   EventSet* es = TG_(sets).full;
   Int       g, i;

   /* Count dynamic event columns */
   Int n_events = 0;
   for (g = 0; g < MAX_EVENTGROUP_COUNT; g++) {
      if (!(es->mask & (1u << g)))
         continue;
      EventGroup* eg = TG_(get_event_group)(g);
      if (!eg)
         continue;
      n_events += eg->size;
   }

   mp_state.n_event_cols = n_events;
   mp_state.ncols        = 7 + n_events; /* 7 fixed + dynamic */

   /* Allocate column names array */
   mp_state.col_names =
      VG_(malloc)("tg.mp.cols", mp_state.ncols * sizeof(HChar*));

   /* Fixed columns */
   mp_state.col_names[0] = "seq";
   mp_state.col_names[1] = "tid";
   mp_state.col_names[2] = "event";
   mp_state.col_names[3] = "fn";
   mp_state.col_names[4] = "obj";
   mp_state.col_names[5] = "file";
   mp_state.col_names[6] = "line";

   /* Dynamic event columns */
   Int c = 7;
   for (g = 0; g < MAX_EVENTGROUP_COUNT; g++) {
      if (!(es->mask & (1u << g)))
         continue;
      EventGroup* eg = TG_(get_event_group)(g);
      if (!eg)
         continue;
      for (i = 0; i < eg->size; i++) {
         mp_state.col_names[c++] = eg->name[i];
      }
   }

   /* Initialize buffer */
   msgpack_init(&mp_state.buf, MSGPACK_INITIAL_BUF);
   mp_state.rows_in_chunk = 0;
   mp_state.compress_buf  = NULL;
   mp_state.compress_cap  = 0;

   /* Initialize string intern table */
   init_intern_table();

   /* Write file header */
   msgpack_write_header();
}

/* Add an ENTER/EXIT row to the msgpack output */
static void msgpack_add_row(ULong        seq,
                            Int          tid,
                            Int          event,
                            const HChar* fn_name,
                            Int          fn_len,
                            const HChar* obj_name,
                            Int          obj_len,
                            const HChar* file_name,
                            Int          file_len,
                            Int          line,
                            const ULong* deltas,
                            Int          n_deltas)
{
   /* Intern strings -> integer IDs (avoids memcpy of string data per row) */
   UInt fn_id   = intern_string(fn_name, fn_len);
   UInt obj_id  = intern_string(obj_name, obj_len);
   UInt file_id = intern_string(file_name, file_len);

   /* Pre-ensure capacity for entire row to avoid per-field bounds checks.
    * With interning: 1 (array hdr) + 9 (seq) + 5 (tid) + 5 (event)
    *   + 5 (fn_id) + 5 (obj_id) + 5 (file_id) + 5 (line)
    *   + 3 (counters array hdr) + n_deltas*9
    */
   Int worst_case = 43 + n_deltas * 9;
   msgpack_ensure_capacity(&mp_state.buf, worst_case);

   /* Each row is a msgpack array: 7 fixed + 1 counters sub-array */
   msgpack_write_array_header(&mp_state.buf, 8);

   /* Fixed columns - fn/obj/file are integer IDs, not strings */
   msgpack_write_uint(&mp_state.buf, seq);
   msgpack_write_int(&mp_state.buf, tid);
   msgpack_write_int(&mp_state.buf, event);
   msgpack_write_uint(&mp_state.buf, fn_id);
   msgpack_write_uint(&mp_state.buf, obj_id);
   msgpack_write_uint(&mp_state.buf, file_id);
   msgpack_write_int(&mp_state.buf, line);

   /* Counters sub-array */
   msgpack_write_array_header(&mp_state.buf, n_deltas);
   for (Int i = 0; i < n_deltas; i++) {
      msgpack_write_uint(&mp_state.buf, deltas[i]);
   }

   mp_state.rows_in_chunk++;

   /* Flush if chunk is full */
   if (mp_state.rows_in_chunk >= MSGPACK_CHUNK_ROWS) {
      msgpack_flush_chunk();
   }
}

/* Add a FORK row to the msgpack output (minimal schema: seq, tid, event,
 * child_pid) */
static void msgpack_add_fork_row(ULong seq, Int tid, Int child_pid)
{
   /* FORK row is a 4-element array */
   msgpack_write_array_header(&mp_state.buf, 4);
   msgpack_write_uint(&mp_state.buf, seq);
   msgpack_write_int(&mp_state.buf, tid);
   msgpack_write_int(&mp_state.buf, TG_EV_FORK);
   msgpack_write_int(&mp_state.buf, child_pid);

   mp_state.rows_in_chunk++;

   /* Flush if chunk is full */
   if (mp_state.rows_in_chunk >= MSGPACK_CHUNK_ROWS) {
      msgpack_flush_chunk();
   }
}

/* Add a THREAD_CREATE row to the msgpack output (seq, tid, event, child_tid) */
static void msgpack_add_thread_create_row(ULong seq, Int tid, Int child_tid)
{
   msgpack_write_array_header(&mp_state.buf, 4);
   msgpack_write_uint(&mp_state.buf, seq);
   msgpack_write_int(&mp_state.buf, tid);
   msgpack_write_int(&mp_state.buf, TG_EV_THREAD_CREATE);
   msgpack_write_int(&mp_state.buf, child_tid);

   mp_state.rows_in_chunk++;

   if (mp_state.rows_in_chunk >= MSGPACK_CHUNK_ROWS) {
      msgpack_flush_chunk();
   }
}

/* Add a MARKER row to the msgpack output (seq, tid, event, marker_str) */
static void msgpack_add_marker_row(ULong seq, Int tid, const HChar* marker)
{
   msgpack_write_array_header(&mp_state.buf, 4);
   msgpack_write_uint(&mp_state.buf, seq);
   msgpack_write_int(&mp_state.buf, tid);
   msgpack_write_int(&mp_state.buf, TG_EV_MARKER);
   msgpack_write_str(&mp_state.buf, marker, -1);

   mp_state.rows_in_chunk++;

   if (mp_state.rows_in_chunk >= MSGPACK_CHUNK_ROWS) {
      msgpack_flush_chunk();
   }
}

/* Write the string table as a special compressed chunk after all data.
 * Format: a msgpack array of strings, indexed by intern ID. */
static void msgpack_write_string_table(void)
{
   if (TG_(trace_out).fd < 0)
      return;

   UInt count = mp_state.interns.next_id;
   if (count == 0)
      return;

   /* Serialize the string table into a temporary buffer */
   msgpack_buffer stbl;
   msgpack_init(&stbl, 4096);

   msgpack_write_array_header(&stbl, count);
   for (UInt i = 0; i < count; i++) {
      msgpack_write_str(&stbl, mp_state.interns.strings[i],
                        mp_state.interns.lengths[i]);
   }

   /* Compress and write as a chunk with special marker:
    * We use uncompressed_size with high bit set to indicate string table */
   SizeT  src_size     = stbl.size;
   SizeT  dst_capacity = tg_lz4_compress_bound(src_size);
   UChar* compressed   = VG_(malloc)("tg.stbl.compress", dst_capacity);

   SizeT compressed_size =
      tg_lz4_compress(compressed, dst_capacity, stbl.data, src_size);

   if (compressed_size > 0) {
      /* String table chunk marker: uncompressed size with bit 31 set */
      UInt  marker = (UInt)src_size | 0x80000000u;
      UChar hdr[8];
      hdr[0] = (UChar)(marker & 0xff);
      hdr[1] = (UChar)((marker >> 8) & 0xff);
      hdr[2] = (UChar)((marker >> 16) & 0xff);
      hdr[3] = (UChar)((marker >> 24) & 0xff);
      hdr[4] = (UChar)(compressed_size & 0xff);
      hdr[5] = (UChar)((compressed_size >> 8) & 0xff);
      hdr[6] = (UChar)((compressed_size >> 16) & 0xff);
      hdr[7] = (UChar)((compressed_size >> 24) & 0xff);
      VG_(write)(TG_(trace_out).fd, hdr, 8);
      VG_(write)(TG_(trace_out).fd, compressed, compressed_size);
   }

   VG_(free)(compressed);
   msgpack_free(&stbl);
}

/* Close msgpack output */
static void msgpack_close_output(void)
{
   /* Flush any remaining rows */
   msgpack_flush_chunk();

   /* Write the string table */
   msgpack_write_string_table();

   /* Write end marker (zero-size chunk) */
   UChar end[8] = {0, 0, 0, 0, 0, 0, 0, 0};
   VG_(write)(TG_(trace_out).fd, end, 8);

   /* Cleanup */
   msgpack_free(&mp_state.buf);
   free_intern_table();
   if (mp_state.compress_buf) {
      VG_(free)(mp_state.compress_buf);
      mp_state.compress_buf = NULL;
      mp_state.compress_cap = 0;
   }
   if (mp_state.col_names) {
      VG_(free)(mp_state.col_names);
      mp_state.col_names = NULL;
   }
}

void TG_(trace_open_output)(void)
{
   SysRes res;
   HChar  filename[512];

   if (TG_(trace_out).initialized)
      return;

   if (!TG_(clo).out_format)
      TG_(clo).out_format = DEFAULT_OUTFORMAT;

   HChar* expanded =
      VG_(expand_file_name)("--tracegrind-out-file", TG_(clo).out_format);
   VG_(strncpy)(filename, expanded, sizeof(filename) - 1);
   filename[sizeof(filename) - 1] = '\0';
   VG_(free)(expanded);

   res = VG_(open)(filename, VKI_O_CREAT | VKI_O_WRONLY | VKI_O_TRUNC,
                   VKI_S_IRUSR | VKI_S_IWUSR);
   if (sr_isError(res)) {
      VG_(message)(Vg_UserMsg, "Error: cannot open trace output file '%s'\n",
                   filename);
      VG_(exit)(1);
   }

   TG_(trace_out).fd             = (Int)sr_Res(res);
   TG_(trace_out).seq            = 0;
   TG_(trace_out).initialized    = True;
   TG_(trace_out).header_written = False;

   /* Initialize msgpack writer */
   msgpack_init_state();

   if (VG_(clo_verbosity) > 1)
      VG_(message)(Vg_DebugMsg, "Trace output to %s\n", filename);
}

/*
 * Called in child process after fork.
 * Closes the inherited file descriptor (without writing end marker)
 * and opens a new trace file with the child's PID.
 */
void TG_(trace_reopen_child)(void)
{
   /* Close inherited fd without flushing/finalizing (that's parent's job) */
   if (TG_(trace_out).fd >= 0) {
      VG_(close)(TG_(trace_out).fd);
   }

   /* Reset state completely */
   TG_(trace_out).fd             = -1;
   TG_(trace_out).seq            = 0;
   TG_(trace_out).initialized    = False;
   TG_(trace_out).header_written = False;

   /* Open new trace file with child's PID (also re-inits msgpack state) */
   TG_(trace_open_output)();
}

void TG_(trace_emit_sample)(ThreadId tid, Bool is_enter, fn_node* fn)
{
   Int i;

   if (!TG_(trace_out).initialized)
      return;
   if (TG_(trace_out).fd < 0)
      return;

   /* Get current thread info for per-thread last_sample_cost */
   thread_info* ti = TG_(get_current_thread)();
   if (!ti)
      return;

   EventSet* es           = TG_(sets).full;
   FullCost  current_cost = TG_(current_state).cost;

   /* If last_sample_cost not yet allocated, allocate and zero it */
   if (!ti->last_sample_cost) {
      ti->last_sample_cost = TG_(get_eventset_cost)(es);
      TG_(init_cost)(es, ti->last_sample_cost);
   }

   TG_(trace_out).seq++;

   /* Resolve function info with cached lengths */
   const HChar* fn_name;
   Int          fn_len;
   const HChar* obj_name;
   Int          obj_len;
   const HChar* file_name;
   Int          file_len;

   if (fn) {
      fn_name = fn->name;
      fn_len  = (Int)fn->name_len;
      if (fn->file) {
         file_name = fn->file->name;
         file_len  = (Int)fn->file->name_len;
         if (fn->file->obj) {
            obj_name = fn->file->obj->name;
            obj_len  = (Int)fn->file->obj->name_len;
         } else {
            obj_name = "???";
            obj_len  = 3;
         }
      } else {
         file_name = "???";
         file_len  = 3;
         obj_name  = "???";
         obj_len   = 3;
      }
   } else {
      fn_name   = "???";
      fn_len    = 3;
      obj_name  = "???";
      obj_len   = 3;
      file_name = "???";
      file_len  = 3;
   }

   /* Compute deltas for all event counters */
   ULong deltas[64]; /* es->size is always small */
   tl_assert(es->size <= 64);
   if (current_cost && ti->last_sample_cost) {
      for (i = 0; i < es->size; i++) {
         deltas[i] = current_cost[i] - ti->last_sample_cost[i];
      }
      TG_(copy_cost)(es, ti->last_sample_cost, current_cost);
   } else {
      for (i = 0; i < es->size; i++) {
         deltas[i] = 0;
      }
   }

   Int event_val = is_enter ? TG_EV_ENTER_FN : TG_EV_EXIT_FN;

   msgpack_add_row(TG_(trace_out).seq, (Int)tid, event_val, fn_name, fn_len,
                   obj_name, obj_len, file_name, file_len, 0, deltas, es->size);
}

void TG_(trace_emit_enter_inlined)(ThreadId tid, BB* bb, const HChar* inl_fn)
{
   Int i;

   if (!TG_(trace_out).initialized)
      return;
   if (TG_(trace_out).fd < 0)
      return;

   thread_info* ti = TG_(get_current_thread)();
   if (!ti)
      return;

   EventSet* es           = TG_(sets).full;
   FullCost  current_cost = TG_(current_state).cost;

   if (!ti->last_sample_cost) {
      ti->last_sample_cost = TG_(get_eventset_cost)(es);
      TG_(init_cost)(es, ti->last_sample_cost);
   }

   TG_(trace_out).seq++;

   const HChar* fn_name = inl_fn;
   Int          fn_len  = -1; /* inlined fn names not cached, use strlen */
   const HChar* obj_name;
   Int          obj_len;
   const HChar* file_name;
   Int          file_len;

   if (bb->obj) {
      obj_name = bb->obj->name;
      obj_len  = (Int)bb->obj->name_len;
   } else {
      obj_name = "???";
      obj_len  = 3;
   }
   if (bb->fn && bb->fn->file) {
      file_name = bb->fn->file->name;
      file_len  = (Int)bb->fn->file->name_len;
   } else {
      file_name = "???";
      file_len  = 3;
   }

   ULong deltas[64];
   tl_assert(es->size <= 64);
   if (current_cost && ti->last_sample_cost) {
      for (i = 0; i < es->size; i++) {
         deltas[i] = current_cost[i] - ti->last_sample_cost[i];
      }
      TG_(copy_cost)(es, ti->last_sample_cost, current_cost);
   } else {
      for (i = 0; i < es->size; i++) {
         deltas[i] = 0;
      }
   }

   msgpack_add_row(TG_(trace_out).seq, (Int)tid, TG_EV_ENTER_INLINED_FN,
                   fn_name, fn_len, obj_name, obj_len, file_name, file_len,
                   (Int)bb->line, deltas, es->size);
}

void TG_(trace_emit_exit_inlined)(ThreadId tid, BB* bb, const HChar* inl_fn)
{
   Int i;

   if (!TG_(trace_out).initialized)
      return;
   if (TG_(trace_out).fd < 0)
      return;

   thread_info* ti = TG_(get_current_thread)();
   if (!ti)
      return;

   EventSet* es           = TG_(sets).full;
   FullCost  current_cost = TG_(current_state).cost;

   if (!ti->last_sample_cost) {
      ti->last_sample_cost = TG_(get_eventset_cost)(es);
      TG_(init_cost)(es, ti->last_sample_cost);
   }

   TG_(trace_out).seq++;

   const HChar* fn_name = inl_fn;
   Int          fn_len  = -1; /* inlined fn names not cached, use strlen */
   const HChar* obj_name;
   Int          obj_len;
   const HChar* file_name;
   Int          file_len;

   if (bb->obj) {
      obj_name = bb->obj->name;
      obj_len  = (Int)bb->obj->name_len;
   } else {
      obj_name = "???";
      obj_len  = 3;
   }
   if (bb->fn && bb->fn->file) {
      file_name = bb->fn->file->name;
      file_len  = (Int)bb->fn->file->name_len;
   } else {
      file_name = "???";
      file_len  = 3;
   }

   ULong deltas[64];
   tl_assert(es->size <= 64);
   if (current_cost && ti->last_sample_cost) {
      for (i = 0; i < es->size; i++) {
         deltas[i] = current_cost[i] - ti->last_sample_cost[i];
      }
      TG_(copy_cost)(es, ti->last_sample_cost, current_cost);
   } else {
      for (i = 0; i < es->size; i++) {
         deltas[i] = 0;
      }
   }

   msgpack_add_row(TG_(trace_out).seq, (Int)tid, TG_EV_EXIT_INLINED_FN, fn_name,
                   fn_len, obj_name, obj_len, file_name, file_len,
                   (Int)bb->line, deltas, es->size);
}

/*
 * Emit a FORK event when a child process is created.
 * Called from the post-syscall handler when fork/clone returns in parent.
 * child_pid is the PID of the newly created child process.
 */
void TG_(trace_emit_fork)(ThreadId tid, Int child_pid)
{
   if (!TG_(trace_out).initialized)
      return;
   if (TG_(trace_out).fd < 0)
      return;

   TG_(trace_out).seq++;

   /* FORK uses minimal schema: [seq, tid, event, child_pid] */
   msgpack_add_fork_row(TG_(trace_out).seq, (Int)tid, child_pid);
}

void TG_(trace_emit_thread_create)(ThreadId tid, ThreadId child)
{
   if (!TG_(trace_out).initialized)
      return;
   if (TG_(trace_out).fd < 0)
      return;

   TG_(trace_out).seq++;

   msgpack_add_thread_create_row(TG_(trace_out).seq, (Int)tid, (Int)child);
}

void TG_(trace_emit_marker)(ThreadId tid, const HChar* marker)
{
   if (!TG_(trace_out).initialized)
      return;
   if (TG_(trace_out).fd < 0)
      return;

   TG_(trace_out).seq++;

   msgpack_add_marker_row(TG_(trace_out).seq, (Int)tid, marker);
}

void TG_(trace_close_output)(void)
{
   if (!TG_(trace_out).initialized)
      return;
   if (TG_(trace_out).fd < 0)
      return;

   /* Flush remaining rows, write end marker */
   msgpack_close_output();
   VG_(close)(TG_(trace_out).fd);

   TG_(trace_out).fd          = -1;
   TG_(trace_out).initialized = False;

   if (VG_(clo_verbosity) > 1)
      VG_(message)(Vg_DebugMsg, "Trace output closed (%llu samples written)\n",
                   TG_(trace_out).seq);
}

/* Sum costs from all threads into total_cost */
void TG_(compute_total_cost)(void)
{
   if (!TG_(total_cost)) {
      TG_(total_cost) = TG_(get_eventset_cost)(TG_(sets).full);
      TG_(init_cost)(TG_(sets).full, TG_(total_cost));
   }

   {
      Int           t;
      thread_info** thr = TG_(get_threads)();
      for (t = 1; t < VG_N_THREADS; t++) {
         if (!thr[t])
            continue;
         TG_(add_diff_cost)
         (TG_(sets).full, TG_(total_cost), thr[t]->lastdump_cost,
          thr[t]->states.entry[0]->cost);
         TG_(copy_cost)
         (TG_(sets).full, thr[t]->lastdump_cost, thr[t]->states.entry[0]->cost);
      }
   }
}
