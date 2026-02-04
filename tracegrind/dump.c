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

#include "pub_tool_threadstate.h"
#include "pub_tool_libcfile.h"

/* ================================================================== */
/* === Legacy dump state (kept for totals verification)           === */
/* ================================================================== */

static Int out_counter = 0;
static HChar* out_file = 0;
static Bool dumps_initialized = False;

/* Total reads/writes/misses sum over all dumps and threads. */
FullCost TG_(total_cost) = 0;

EventMapping* TG_(dumpmap) = 0;

Int TG_(get_dump_counter)(void)
{
  return out_counter;
}

/* ================================================================== */
/* === CSV trace output                                           === */
/* ================================================================== */

trace_output TG_(trace_out) = { .fd = -1, .seq = 0,
                                .initialized = False,
                                .header_written = False };

/* Write a string to the trace output fd */
static void trace_write(const HChar* buf, Int len)
{
    if (TG_(trace_out).fd < 0) return;
    VG_(write)(TG_(trace_out).fd, buf, len);
}

/* Escape a string for CSV: if it contains comma, quote, or newline,
 * wrap in quotes and double any quotes.  Otherwise just copy.
 * Writes to buf, returns chars written. buf must be large enough.
 */
static Int csv_escape(HChar* buf, Int bufsize, const HChar* src)
{
    Bool needs_quote = False;
    const HChar* p;
    Int i;

    for (p = src; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n') {
            needs_quote = True;
            break;
        }
    }

    if (!needs_quote) {
        i = 0;
        for (p = src; *p && i < bufsize - 1; p++, i++)
            buf[i] = *p;
        buf[i] = '\0';
        return i;
    }

    i = 0;
    if (i < bufsize - 1) buf[i++] = '"';
    for (p = src; *p && i < bufsize - 2; p++) {
        if (*p == '"' && i < bufsize - 3) {
            buf[i++] = '"';
            buf[i++] = '"';
        } else {
            buf[i++] = *p;
        }
    }
    if (i < bufsize - 1) buf[i++] = '"';
    buf[i] = '\0';
    return i;
}

void TG_(trace_open_output)(void)
{
    SysRes res;
    HChar filename[512];

    if (TG_(trace_out).initialized) return;

    if (!TG_(clo).out_format)
        TG_(clo).out_format = DEFAULT_OUTFORMAT;

    HChar* expanded = VG_(expand_file_name)("--tracegrind-out-file",
                                             TG_(clo).out_format);
    VG_(strncpy)(filename, expanded, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    VG_(free)(expanded);

    res = VG_(open)(filename,
                    VKI_O_CREAT|VKI_O_WRONLY|VKI_O_TRUNC,
                    VKI_S_IRUSR|VKI_S_IWUSR);
    if (sr_isError(res)) {
        VG_(message)(Vg_UserMsg,
                     "Error: cannot open trace output file '%s'\n", filename);
        VG_(exit)(1);
    }

    TG_(trace_out).fd = (Int)sr_Res(res);
    TG_(trace_out).seq = 0;
    TG_(trace_out).initialized = True;
    TG_(trace_out).header_written = False;

    if (VG_(clo_verbosity) > 1)
        VG_(message)(Vg_DebugMsg, "Trace output to %s\n", filename);
}

/* Write the CSV header row.
 * Called lazily on first sample emission so that event sets are fully configured.
 */
static void trace_write_header(void)
{
    HChar buf[4096];
    Int pos = 0;

    if (TG_(trace_out).header_written) return;
    TG_(trace_out).header_written = True;

    pos += VG_(sprintf)(buf + pos, "seq,tid,event,fn,obj,file,line");

    /* Emit column names for all events in the full event set */
    EventSet* es = TG_(sets).full;
    Int g, i;
    for (g = 0; g < MAX_EVENTGROUP_COUNT; g++) {
        if (!(es->mask & (1u << g))) continue;
        EventGroup* eg = TG_(get_event_group)(g);
        if (!eg) continue;
        for (i = 0; i < eg->size; i++) {
            pos += VG_(sprintf)(buf + pos, ",%s", eg->name[i]);
        }
    }

    pos += VG_(sprintf)(buf + pos, "\n");
    trace_write(buf, pos);
}

void TG_(trace_emit_sample)(ThreadId tid, const HChar* event_type,
                             fn_node* fn)
{
    HChar buf[4096];
    HChar escaped[1024];
    Int pos = 0;
    Int i;

    if (!TG_(trace_out).initialized) return;
    if (TG_(trace_out).fd < 0) return;

    /* Lazily write header on first sample */
    if (!TG_(trace_out).header_written)
        trace_write_header();

    /* Get current thread info for per-thread last_sample_cost */
    thread_info* ti = TG_(get_current_thread)();
    if (!ti) return;

    EventSet* es = TG_(sets).full;
    FullCost current_cost = TG_(current_state).cost;

    /* If last_sample_cost not yet allocated, allocate and zero it */
    if (!ti->last_sample_cost) {
        ti->last_sample_cost = TG_(get_eventset_cost)(es);
        TG_(init_cost)(es, ti->last_sample_cost);
    }

    TG_(trace_out).seq++;

    /* Resolve function info */
    const HChar* fn_name = fn ? fn->name : "???";
    const HChar* obj_name = (fn && fn->file && fn->file->obj)
                            ? fn->file->obj->name : "???";
    const HChar* file_name = (fn && fn->file) ? fn->file->name : "???";
    UInt line = (fn && fn->file) ? 0 : 0;  /* line from fn_node's BB */

    /* Try to get line number from the function's BB debug info */
    if (fn && fn->pure_cxt) {
        /* We could look up debug info here, but fn_node doesn't store line.
         * The BB that was the entry point does store it. We use 0 as default. */
    }

    /* seq, tid, event */
    pos += VG_(sprintf)(buf + pos, "%llu,%u,%s,",
                        TG_(trace_out).seq,
                        (UInt)tid,
                        event_type);

    /* fn (escaped) */
    csv_escape(escaped, sizeof(escaped), fn_name);
    pos += VG_(sprintf)(buf + pos, "%s,", escaped);

    /* obj (escaped) */
    csv_escape(escaped, sizeof(escaped), obj_name);
    pos += VG_(sprintf)(buf + pos, "%s,", escaped);

    /* file (escaped) */
    csv_escape(escaped, sizeof(escaped), file_name);
    pos += VG_(sprintf)(buf + pos, "%s,", escaped);

    /* line */
    pos += VG_(sprintf)(buf + pos, "%u", line);

    /* Compute and emit deltas for all event groups */
    if (current_cost && ti->last_sample_cost) {
        for (i = 0; i < es->size; i++) {
            ULong delta = current_cost[i] - ti->last_sample_cost[i];
            pos += VG_(sprintf)(buf + pos, ",%llu", delta);
        }
        /* Update last_sample_cost snapshot */
        TG_(copy_cost)(es, ti->last_sample_cost, current_cost);
    } else {
        /* No cost data available, emit zeros */
        for (i = 0; i < es->size; i++) {
            pos += VG_(sprintf)(buf + pos, ",0");
        }
    }

    pos += VG_(sprintf)(buf + pos, "\n");
    trace_write(buf, pos);
}

void TG_(trace_close_output)(void)
{
    if (!TG_(trace_out).initialized) return;
    if (TG_(trace_out).fd < 0) return;

    /* Write a totals summary comment at the end for verification */
    if (TG_(total_cost)) {
        HChar buf[4096];
        Int pos = 0;
        Int i;
        EventSet* es = TG_(sets).full;

        pos += VG_(sprintf)(buf + pos, "# totals:");
        for (i = 0; i < es->size; i++) {
            pos += VG_(sprintf)(buf + pos, " %llu", TG_(total_cost)[i]);
        }
        pos += VG_(sprintf)(buf + pos, "\n");
        trace_write(buf, pos);
    }

    VG_(close)(TG_(trace_out).fd);
    TG_(trace_out).fd = -1;
    TG_(trace_out).initialized = False;

    if (VG_(clo_verbosity) > 1)
        VG_(message)(Vg_DebugMsg,
                     "Trace output closed (%llu samples written)\n",
                     TG_(trace_out).seq);
}


/* ================================================================== */
/* === Simplified dump (totals only, for verification)            === */
/* ================================================================== */

/* Command buffer for dump header */
static HChar *cmdbuf;

static void init_cmdbuf(void)
{
  SizeT size;
  Int i,j;

  size  = 1;
  size += VG_(strlen)( VG_(args_the_exename) );
  for (i = 0; i < VG_(sizeXA)( VG_(args_for_client) ); i++) {
     const HChar *arg = *(HChar**)VG_(indexXA)( VG_(args_for_client), i );
     size += 1;
     for(j=0; arg[j]; j++)
       switch(arg[j]) {
       case '\n':
       case '\\':
	 size++;
	 /* fallthrough */
       default:
	 size++;
       }
  }

  cmdbuf = TG_MALLOC("tg.dump.ic.1", size + 1);

  size = VG_(sprintf)(cmdbuf, " %s", VG_(args_the_exename));

  for(i = 0; i < VG_(sizeXA)( VG_(args_for_client) ); i++) {
     const HChar *arg = * (HChar**) VG_(indexXA)( VG_(args_for_client), i );
     cmdbuf[size++] = ' ';
     for(j=0; arg[j]; j++)
       switch(arg[j]) {
       case '\n':
	 cmdbuf[size++] = '\\';
	 cmdbuf[size++] = 'n';
	 break;
       case '\\':
	 cmdbuf[size++] = '\\';
	 cmdbuf[size++] = '\\';
	 break;
       default:
	 cmdbuf[size++] = arg[j];
	 break;
       }
  }
  cmdbuf[size] = '\0';
}


/* Dump profile now only computes totals (no callgraph output).
 * The real output is the streaming CSV trace.
 */
void TG_(dump_profile)(const HChar* trigger, Bool only_current_thread)
{
   TG_DEBUG(2, "+ dump_profile(Trigger '%s')\n",
	    trigger ? trigger : "Prg.Term.");

   TG_(init_dumps)();
   out_counter++;

   /* Compute totals from all threads */
   if (!TG_(total_cost)) {
       TG_(total_cost) = TG_(get_eventset_cost)(TG_(sets).full);
       TG_(init_cost)(TG_(sets).full, TG_(total_cost));
   }

   /* Sum costs from all threads into total_cost */
   {
       Int t;
       thread_info** thr = TG_(get_threads)();
       for (t = 1; t < VG_N_THREADS; t++) {
           if (!thr[t]) continue;
           TG_(add_diff_cost)(TG_(sets).full, TG_(total_cost),
                              thr[t]->lastdump_cost,
                              thr[t]->states.entry[0]->cost);
           /* Update lastdump_cost */
           TG_(copy_cost)(TG_(sets).full, thr[t]->lastdump_cost,
                          thr[t]->states.entry[0]->cost);
       }
   }

   if (VG_(clo_verbosity) > 1)
       VG_(message)(Vg_DebugMsg, "Dump done (trigger: %s).\n",
                    trigger ? trigger : "Prg.Term.");
}


void TG_(init_dumps)(void)
{
   static int thisPID = 0;
   int currentPID = VG_(getpid)();
   if (currentPID == thisPID) {
       TG_ASSERT(out_file != 0);
       return;
   }
   thisPID = currentPID;

   if (!TG_(clo).out_format)
     TG_(clo).out_format = DEFAULT_OUTFORMAT;

   if (out_file) {
       VG_(free)(out_file);
       out_counter = 0;
   }

   out_file =
       VG_(expand_file_name)("--tracegrind-out-file", TG_(clo).out_format);

   if (!dumps_initialized)
       init_cmdbuf();

   dumps_initialized = True;
}
