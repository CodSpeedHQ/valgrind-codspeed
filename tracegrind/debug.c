/*
   This file is part of Tracegrind, a Valgrind tool for call graph
   profiling programs.

   Copyright (C) 2002-2017, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)

   This tool is derived from and contains lot of code from Cachegrind
   Copyright (C) 2002-2017 Nicholas Nethercote (njn@valgrind.org)

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

#include "events.h"
#include "global.h"

/* If debugging mode of, dummy functions are provided (see below)
 */
#if TG_ENABLE_DEBUG

/*------------------------------------------------------------*/
/*--- Debug output helpers                                 ---*/
/*------------------------------------------------------------*/

static void print_indent(int s)
{
   /* max of 40 spaces */
   const HChar sp[] = "                                        ";
   if (s > 40)
      s = 40;
   VG_(printf)("%s", sp + 40 - s);
}

void TG_(print_bb)(int s, BB* bb)
{
   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   VG_(printf)("BB %#lx (Obj '%s')", bb_addr(bb), bb->obj->name);
}

static void print_mangled_cxt(Context* cxt, int rec_index)
{
   int i;

   if (!cxt)
      VG_(printf)("(none)");
   else {
      VG_(printf)("%s", cxt->fn[0]->name);
      if (rec_index > 0)
         VG_(printf)("'%d", rec_index + 1);
      for (i = 1; i < cxt->size; i++)
         VG_(printf)("'%s", cxt->fn[i]->name);
   }
}

void TG_(print_cxt)(Int s, Context* cxt, int rec_index)
{
   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   if (cxt) {
      UInt* pactive = TG_(get_fn_entry)(cxt->fn[0]->number);
      TG_ASSERT(rec_index < cxt->fn[0]->separate_recursions);

      VG_(printf)("Cxt %u", cxt->base_number + rec_index);
      if (*pactive > 0)
         VG_(printf)(" [active=%u]", *pactive);
      VG_(printf)(": ");
      print_mangled_cxt(cxt, rec_index);
      VG_(printf)("\n");
   } else
      VG_(printf)("(no context)\n");
}

void TG_(print_execstate)(int s, exec_state* es)
{
   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   if (!es) {
      VG_(printf)("ExecState 0x0\n");
      return;
   }

   VG_(printf)(
      "ExecState [Sig %d, collect %s, nonskipped %p]: jmps_passed %d\n",
      es->sig, es->collect ? "yes" : "no", es->nonskipped, es->jmps_passed);
}

void TG_(print_bbcc)(int s, BBCC* bbcc)
{
   BB* bb;

   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   if (!bbcc) {
      VG_(printf)("BBCC 0x0\n");
      return;
   }

   bb = bbcc->bb;
   TG_ASSERT(bb != 0);

   VG_(printf)("%s +%#lx=%#lx, ", bb->obj->name + bb->obj->last_slash_pos,
               (UWord)bb->offset, bb_addr(bb));
   TG_(print_cxt)(s + 8, bbcc->cxt, bbcc->rec_index);
}

void TG_(print_eventset)(int s, EventSet* es)
{
   int         i, j;
   UInt        mask;
   EventGroup* eg;

   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   if (!es) {
      VG_(printf)("(EventSet not set)\n");
      return;
   }

   VG_(printf)("EventSet %u (%d groups, size %d):", es->mask, es->count,
               es->size);

   if (es->count == 0) {
      VG_(printf)("-\n");
      return;
   }

   for (i = 0, mask = 1; i < MAX_EVENTGROUP_COUNT; i++, mask = mask << 1) {
      if ((es->mask & mask) == 0)
         continue;
      eg = TG_(get_event_group)(i);
      if (!eg)
         continue;
      VG_(printf)(" (%d: %s", i, eg->name[0]);
      for (j = 1; j < eg->size; j++)
         VG_(printf)(" %s", eg->name[j]);
      VG_(printf)(")");
   }
   VG_(printf)("\n");
}

void TG_(print_cost)(int s, EventSet* es, ULong* c)
{
   Int         i, j, pos, off;
   UInt        mask;
   EventGroup* eg;

   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   if (!es) {
      VG_(printf)("Cost (Nothing, EventSet not set)\n");
      return;
   }
   if (!c) {
      VG_(printf)("Cost (Null, EventSet %u)\n", es->mask);
      return;
   }

   if (es->size == 0) {
      VG_(printf)("Cost (Nothing, EventSet with len 0)\n");
      return;
   }

   pos = s;
   pos += VG_(printf)("Cost [%p]: ", c);
   off = 0;
   for (i = 0, mask = 1; i < MAX_EVENTGROUP_COUNT; i++, mask = mask << 1) {
      if ((es->mask & mask) == 0)
         continue;
      eg = TG_(get_event_group)(i);
      if (!eg)
         continue;
      for (j = 0; j < eg->size; j++) {

         if (off > 0) {
            if (pos > 70) {
               VG_(printf)(",\n");
               print_indent(s + 5);
               pos = s + 5;
            } else
               pos += VG_(printf)(", ");
         }

         pos += VG_(printf)("%s %llu", eg->name[j], c[off++]);
      }
   }
   VG_(printf)("\n");
}

void TG_(print_short_jcc)(jCC* jcc)
{
   if (jcc)
      VG_(printf)("%#lx => %#lx [calls %llu/Ir %llu, Dr %llu, Dw %llu]",
                  bb_jmpaddr(jcc->from->bb), bb_addr(jcc->to->bb),
                  jcc->call_counter,
                  jcc->cost ? jcc->cost[fullOffset(EG_IR)] : 0,
                  jcc->cost ? jcc->cost[fullOffset(EG_DR)] : 0,
                  jcc->cost ? jcc->cost[fullOffset(EG_DW)] : 0);
   else
      VG_(printf)("[Skipped JCC]");
}

void TG_(print_jcc)(int s, jCC* jcc)
{
   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   if (!jcc) {
      VG_(printf)("JCC to skipped function\n");
      return;
   }
   VG_(printf)("JCC %p from ", jcc);
   TG_(print_bbcc)(s + 9, jcc->from);
   print_indent(s + 4);
   VG_(printf)("to   ");
   TG_(print_bbcc)(s + 9, jcc->to);
   print_indent(s + 4);
   VG_(printf)("Calls %llu\n", jcc->call_counter);
   print_indent(s + 4);
   TG_(print_cost)(s + 9, TG_(sets).full, jcc->cost);
}

/* dump out the current call stack */
void TG_(print_stackentry)(int s, int sp)
{
   call_entry* ce;

   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   ce = TG_(get_call_entry)(sp);
   VG_(printf)("[%-2d] SP %#lx, RA %#lx", sp, ce->sp, ce->ret_addr);
   if (ce->nonskipped)
      VG_(printf)(" NonSkipped BB %#lx / %s", bb_addr(ce->nonskipped->bb),
                  ce->nonskipped->cxt->fn[0]->name);
   VG_(printf)("\n");
   print_indent(s + 5);
   TG_(print_jcc)(5, ce->jcc);
}

/* debug output */
#if 0
static void print_call_stack()
{
    int c;

    VG_(printf)("Call Stack:\n");
    for(c=0;c<TG_(current_call_stack).sp;c++)
      TG_(print_stackentry)(-2, c);
}
#endif

void TG_(print_bbcc_fn)(BBCC* bbcc)
{
   obj_node* obj;

   if (!bbcc) {
      VG_(printf)("%08x", 0u);
      return;
   }

   VG_(printf)("%08lx/%c  %u:", bb_addr(bbcc->bb),
               (bbcc->bb->sect_kind == Vg_SectText)   ? 'T'
               : (bbcc->bb->sect_kind == Vg_SectData) ? 'D'
               : (bbcc->bb->sect_kind == Vg_SectBSS)  ? 'B'
               : (bbcc->bb->sect_kind == Vg_SectGOT)  ? 'G'
               : (bbcc->bb->sect_kind == Vg_SectPLT)  ? 'P'
                                                      : 'U',
               bbcc->cxt->base_number + bbcc->rec_index);
   print_mangled_cxt(bbcc->cxt, bbcc->rec_index);

   obj = bbcc->cxt->fn[0]->file->obj;
   if (obj->name[0])
      VG_(printf)(" %s", obj->name + obj->last_slash_pos);

   if (VG_(strcmp)(bbcc->cxt->fn[0]->file->name, "???") != 0) {
      VG_(printf)(" %s", bbcc->cxt->fn[0]->file->name);
      if ((bbcc->cxt->fn[0] == bbcc->bb->fn) && (bbcc->bb->line > 0))
         VG_(printf)(":%u", bbcc->bb->line);
   }
}

void TG_(print_bbcc_cost)(int s, BBCC* bbcc)
{
   BB*   bb;
   Int   i, cjmpNo;
   ULong ecounter;

   if (s < 0) {
      s = -s;
      print_indent(s);
   }

   if (!bbcc) {
      VG_(printf)("BBCC 0x0\n");
      return;
   }

   bb = bbcc->bb;
   TG_ASSERT(bb != 0);

   TG_(print_bbcc)(s, bbcc);

   ecounter = bbcc->ecounter_sum;

   print_indent(s + 2);
   VG_(printf)("ECounter: sum %llu ", ecounter);
   for (i = 0; i < bb->cjmp_count; i++) {
      VG_(printf)("[%u]=%llu ", bb->jmp[i].instr, bbcc->jmp[i].ecounter);
   }
   VG_(printf)("\n");

   cjmpNo = 0;
   for (i = 0; i < bb->instr_count; i++) {
      InstrInfo* ii = &(bb->instr[i]);
      print_indent(s + 2);
      VG_(printf)("[%2d] IOff %2u ecnt %3llu ", i, ii->instr_offset, ecounter);
      TG_(print_cost)(s + 5, ii->eventset, bbcc->cost + ii->cost_offset);

      /* update execution counter */
      if (cjmpNo < bb->cjmp_count)
         if (bb->jmp[cjmpNo].instr == i) {
            ecounter -= bbcc->jmp[cjmpNo].ecounter;
            cjmpNo++;
         }
   }
}

/* dump out an address with source info if available */
void TG_(print_addr)(Addr addr)
{
   const HChar *fn_buf, *fl_buf, *dir_buf;
   const HChar* obj_name;
   DebugInfo*   di;
   UInt         ln, i = 0, opos = 0;

   if (addr == 0) {
      VG_(printf)("%08lx", addr);
      return;
   }

   TG_(get_debug_info)(addr, &dir_buf, &fl_buf, &fn_buf, &ln, &di);

   if (VG_(strcmp)(fn_buf, "???") == 0)
      VG_(printf)("%#lx", addr);
   else
      VG_(printf)("%#lx %s", addr, fn_buf);

   if (di) {
      obj_name = VG_(DebugInfo_get_filename)(di);
      if (obj_name) {
         while (obj_name[i]) {
            if (obj_name[i] == '/')
               opos = i + 1;
            i++;
         }
         if (obj_name[0])
            VG_(printf)(" %s", obj_name + opos);
      }
   }

   if (ln > 0) {
      if (dir_buf[0])
         VG_(printf)(" (%s/%s:%u)", dir_buf, fl_buf, ln);
      else
         VG_(printf)(" (%s:%u)", fl_buf, ln);
   }
}

void TG_(print_addr_ln)(Addr addr)
{
   TG_(print_addr)(addr);
   VG_(printf)("\n");
}

static ULong bb_written = 0;

void TG_(print_bbno)(void)
{
   if (bb_written != TG_(stat).bb_executions) {
      bb_written = TG_(stat).bb_executions;
      VG_(printf)("BB# %llu\n", TG_(stat).bb_executions);
   }
}

void TG_(print_context)(void)
{
   BBCC* bbcc;

   TG_DEBUG(0, "In tid %u [%d] ", TG_(current_tid), TG_(current_call_stack).sp);
   bbcc = TG_(current_state).bbcc;
   print_mangled_cxt(TG_(current_state).cxt, bbcc ? bbcc->rec_index : 0);
   VG_(printf)("\n");
}

void* TG_(malloc)(const HChar* cc, UWord s, const HChar* f)
{
   TG_DEBUG(3, "Malloc(%lu) in %s.\n", s, f);
   return VG_(malloc)(cc, s);
}

#else /* TG_ENABLE_DEBUG */

void TG_(print_bbno)(void) {}
void TG_(print_context)(void) {}
void TG_(print_jcc)(int s, jCC* jcc) {}
void TG_(print_bbcc)(int s, BBCC* bbcc) {}
void TG_(print_bbcc_fn)(BBCC* bbcc) {}
void TG_(print_cost)(int s, EventSet* es, ULong* cost) {}
void TG_(print_bb)(int s, BB* bb) {}
void TG_(print_cxt)(int s, Context* cxt, int rec_index) {}
void TG_(print_short_jcc)(jCC* jcc) {}
void TG_(print_stackentry)(int s, int sp) {}
void TG_(print_addr)(Addr addr) {}
void TG_(print_addr_ln)(Addr addr) {}

#endif
