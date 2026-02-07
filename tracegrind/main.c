
/*--------------------------------------------------------------------*/
/*--- Tracegrind                                                    ---*/
/*---                                                       main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Tracegrind, a Valgrind tool for call graph
   profiling programs.

   Copyright (C) 2002-2017, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)

   This tool is derived from and contains code from Cachegrind
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

#include "config.h"
#include "tracegrind.h"
#include "global.h"

#include "pub_tool_threadstate.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_transtab.h"       // VG_(discard_translations_safely)

#include "cg_branchpred.c"

/*------------------------------------------------------------*/
/*--- Global variables                                     ---*/
/*------------------------------------------------------------*/

/* for all threads */
CommandLineOptions TG_(clo);
Statistics TG_(stat);
Bool TG_(instrument_state) = True; /* Instrumentation on ? */

/* thread and signal handler specific */
exec_state TG_(current_state);

/* min of L1 and LL cache line sizes.  This only gets set to a
   non-zero value if we are doing cache simulation. */
Int TG_(min_line_size) = 0;


/*------------------------------------------------------------*/
/*--- Statistics                                           ---*/
/*------------------------------------------------------------*/

static void TG_(init_statistics)(Statistics* s)
{
  s->call_counter        = 0;
  s->jcnd_counter        = 0;
  s->jump_counter        = 0;
  s->rec_call_counter    = 0;
  s->ret_counter         = 0;
  s->bb_executions       = 0;

  s->context_counter     = 0;
  s->bb_retranslations   = 0;

  s->distinct_objs       = 0;
  s->distinct_files      = 0;
  s->distinct_fns        = 0;
  s->distinct_contexts   = 0;
  s->distinct_bbs        = 0;
  s->distinct_bbccs      = 0;
  s->distinct_instrs     = 0;
  s->distinct_skips      = 0;

  s->bb_hash_resizes     = 0;
  s->bbcc_hash_resizes   = 0;
  s->jcc_hash_resizes    = 0;
  s->cxt_hash_resizes    = 0;
  s->fn_array_resizes    = 0;
  s->call_stack_resizes  = 0;
  s->fn_stack_resizes    = 0;

  s->full_debug_BBs      = 0;
  s->file_line_debug_BBs = 0;
  s->fn_name_debug_BBs   = 0;
  s->no_debug_BBs        = 0;
  s->bbcc_lru_misses     = 0;
  s->jcc_lru_misses      = 0;
  s->cxt_lru_misses      = 0;
  s->bbcc_clones         = 0;
}


/*------------------------------------------------------------*/
/*--- Simple callbacks (not cache similator)               ---*/
/*------------------------------------------------------------*/

VG_REGPARM(1)
static void log_global_event(InstrInfo* ii)
{
    ULong* cost_Bus;

    TG_DEBUG(6, "log_global_event:  Ir  %#lx/%u\n",
              TG_(bb_base) + ii->instr_offset, ii->instr_size);

    if (!TG_(current_state).collect) return;

    TG_ASSERT( (ii->eventset->mask & (1u<<EG_BUS))>0 );

    TG_(current_state).cost[ fullOffset(EG_BUS) ]++;

    if (TG_(current_state).nonskipped)
        cost_Bus = TG_(current_state).nonskipped->skipped + fullOffset(EG_BUS);
    else
        cost_Bus = TG_(cost_base) + ii->cost_offset + ii->eventset->offset[EG_BUS];
    cost_Bus[0]++;
}


/* For branches, we consult two different predictors, one which
   predicts taken/untaken for conditional branches, and the other
   which predicts the branch target address for indirect branches
   (jump-to-register style ones). */

static VG_REGPARM(2)
void log_cond_branch(InstrInfo* ii, Word taken)
{
    Bool miss;
    Int fullOffset_Bc;
    ULong* cost_Bc;

    TG_DEBUG(6, "log_cond_branch:  Ir %#lx, taken %ld\n",
              TG_(bb_base) + ii->instr_offset, taken);

    miss = 1 & do_cond_branch_predict(TG_(bb_base) + ii->instr_offset, taken);

    if (!TG_(current_state).collect) return;

    TG_ASSERT( (ii->eventset->mask & (1u<<EG_BC))>0 );

    if (TG_(current_state).nonskipped)
        cost_Bc = TG_(current_state).nonskipped->skipped + fullOffset(EG_BC);
    else
        cost_Bc = TG_(cost_base) + ii->cost_offset + ii->eventset->offset[EG_BC];

    fullOffset_Bc = fullOffset(EG_BC);
    TG_(current_state).cost[ fullOffset_Bc ]++;
    cost_Bc[0]++;
    if (miss) {
        TG_(current_state).cost[ fullOffset_Bc+1 ]++;
        cost_Bc[1]++;
    }
}

static VG_REGPARM(2)
void log_ind_branch(InstrInfo* ii, UWord actual_dst)
{
    Bool miss;
    Int fullOffset_Bi;
    ULong* cost_Bi;

    TG_DEBUG(6, "log_ind_branch:  Ir  %#lx, dst %#lx\n",
              TG_(bb_base) + ii->instr_offset, actual_dst);

    miss = 1 & do_ind_branch_predict(TG_(bb_base) + ii->instr_offset, actual_dst);

    if (!TG_(current_state).collect) return;

    TG_ASSERT( (ii->eventset->mask & (1u<<EG_BI))>0 );

    if (TG_(current_state).nonskipped)
        cost_Bi = TG_(current_state).nonskipped->skipped + fullOffset(EG_BI);
    else
        cost_Bi = TG_(cost_base) + ii->cost_offset + ii->eventset->offset[EG_BI];

    fullOffset_Bi = fullOffset(EG_BI);
    TG_(current_state).cost[ fullOffset_Bi ]++;
    cost_Bi[0]++;
    if (miss) {
        TG_(current_state).cost[ fullOffset_Bi+1 ]++;
        cost_Bi[1]++;
    }
}

/*------------------------------------------------------------*/
/*--- Instrumentation structures and event queue handling  ---*/
/*------------------------------------------------------------*/

/* Maintain an ordered list of memory events which are outstanding, in
   the sense that no IR has yet been generated to do the relevant
   helper calls.  The BB is scanned top to bottom and memory events
   are added to the end of the list, merging with the most recent
   notified event where possible (Dw immediately following Dr and
   having the same size and EA can be merged).

   This merging is done so that for architectures which have
   load-op-store instructions (x86, amd64), the insn is treated as if
   it makes just one memory reference (a modify), rather than two (a
   read followed by a write at the same address).

   At various points the list will need to be flushed, that is, IR
   generated from it.  That must happen before any possible exit from
   the block (the end, or an IRStmt_Exit).  Flushing also takes place
   when there is no space to add a new event.

   If we require the simulation statistics to be up to date with
   respect to possible memory exceptions, then the list would have to
   be flushed before each memory reference.  That would however lose
   performance by inhibiting event-merging during flushing.

   Flushing the list consists of walking it start to end and emitting
   instrumentation IR for each event, in the order in which they
   appear.  It may be possible to emit a single call for two adjacent
   events in order to reduce the number of helper function calls made.
   For example, it could well be profitable to handle two adjacent Ir
   events with a single helper call.  */

typedef
   IRExpr
   IRAtom;

typedef
   enum {
      Ev_Ir,  // Instruction read
      Ev_Dr,  // Data read
      Ev_Dw,  // Data write
      Ev_Dm,  // Data modify (read then write)
      Ev_Bc,  // branch conditional
      Ev_Bi,  // branch indirect (to unknown destination)
      Ev_G    // Global bus event
   }
   EventTag;

typedef
   struct {
      EventTag   tag;
      InstrInfo* inode;
      union {
	 struct {
	 } Ir;
	 struct {
	    IRAtom* ea;
	    Int     szB;
	 } Dr;
	 struct {
	    IRAtom* ea;
	    Int     szB;
	 } Dw;
	 struct {
	    IRAtom* ea;
	    Int     szB;
	 } Dm;
         struct {
            IRAtom* taken; /* :: Ity_I1 */
         } Bc;
         struct {
            IRAtom* dst;
         } Bi;
	 struct {
	 } G;
      } Ev;
   }
   Event;

static void init_Event ( Event* ev ) {
   VG_(memset)(ev, 0, sizeof(Event));
}

static IRAtom* get_Event_dea ( Event* ev ) {
   switch (ev->tag) {
      case Ev_Dr: return ev->Ev.Dr.ea;
      case Ev_Dw: return ev->Ev.Dw.ea;
      case Ev_Dm: return ev->Ev.Dm.ea;
      default:    tl_assert(0);
   }
}

static Int get_Event_dszB ( Event* ev ) {
   switch (ev->tag) {
      case Ev_Dr: return ev->Ev.Dr.szB;
      case Ev_Dw: return ev->Ev.Dw.szB;
      case Ev_Dm: return ev->Ev.Dm.szB;
      default:    tl_assert(0);
   }
}


/* Up to this many unnotified events are allowed.  Number is
   arbitrary.  Larger numbers allow more event merging to occur, but
   potentially induce more spilling due to extending live ranges of
   address temporaries. */
#define N_EVENTS 16


/* A struct which holds all the running state during instrumentation.
   Mostly to avoid passing loads of parameters everywhere. */
typedef struct {
    /* The current outstanding-memory-event list. */
    Event events[N_EVENTS];
    Int   events_used;

    /* The array of InstrInfo's is part of BB struct. */
    BB* bb;

    /* BB seen before (ie. re-instrumentation) */
    Bool seen_before;

    /* Number InstrInfo bins 'used' so far. */
    UInt ii_index;

    // current offset of guest instructions from BB start
    UInt instr_offset;

    /* The output SB being constructed. */
    IRSB* sbOut;
} ClgState;


static void showEvent ( Event* ev )
{
   switch (ev->tag) {
      case Ev_Ir:
	 VG_(printf)("Ir (InstrInfo %p) at +%u\n",
		     ev->inode, ev->inode->instr_offset);
	 break;
      case Ev_Dr:
	 VG_(printf)("Dr (InstrInfo %p) at +%u %d EA=",
		     ev->inode, ev->inode->instr_offset, ev->Ev.Dr.szB);
	 ppIRExpr(ev->Ev.Dr.ea);
	 VG_(printf)("\n");
	 break;
      case Ev_Dw:
	 VG_(printf)("Dw (InstrInfo %p) at +%u %d EA=",
		     ev->inode, ev->inode->instr_offset, ev->Ev.Dw.szB);
	 ppIRExpr(ev->Ev.Dw.ea);
	 VG_(printf)("\n");
	 break;
      case Ev_Dm:
	 VG_(printf)("Dm (InstrInfo %p) at +%u %d EA=",
		     ev->inode, ev->inode->instr_offset, ev->Ev.Dm.szB);
	 ppIRExpr(ev->Ev.Dm.ea);
	 VG_(printf)("\n");
	 break;
      case Ev_Bc:
         VG_(printf)("Bc %p   GA=", ev->inode);
         ppIRExpr(ev->Ev.Bc.taken);
         VG_(printf)("\n");
         break;
      case Ev_Bi:
         VG_(printf)("Bi %p  DST=", ev->inode);
         ppIRExpr(ev->Ev.Bi.dst);
         VG_(printf)("\n");
         break;
      case Ev_G:
         VG_(printf)("G  %p\n", ev->inode);
         break;
      default:
	 tl_assert(0);
	 break;
   }
}

/* Generate code for all outstanding memory events, and mark the queue
   empty.  Code is generated into cgs->sbOut, and this activity
   'consumes' slots in cgs->bb. */

static void flushEvents ( ClgState* clgs )
{
   Int        i, regparms, inew;
   const HChar* helperName;
   void*      helperAddr;
   IRExpr**   argv;
   IRExpr*    i_node_expr;
   IRDirty*   di;
   Event*     ev;
   Event*     ev2;
   Event*     ev3;

   if (!clgs->seen_before) {
       // extend event sets as needed
       // available sets: D0 Dr
       for(i=0; i<clgs->events_used; i++) {
	   ev  = &clgs->events[i];
	   switch(ev->tag) {
	   case Ev_Ir:
	       // Ir event always is first for a guest instruction
	       TG_ASSERT(ev->inode->eventset == 0);
	       ev->inode->eventset = TG_(sets).base;
	       break;
	   case Ev_Dr:
               // extend event set by Dr counters
	       ev->inode->eventset = TG_(add_event_group)(ev->inode->eventset,
							   EG_DR);
	       break;
	   case Ev_Dw:
	   case Ev_Dm:
               // extend event set by Dw counters
	       ev->inode->eventset = TG_(add_event_group)(ev->inode->eventset,
							   EG_DW);
	       break;
           case Ev_Bc:
               // extend event set by Bc counters
               ev->inode->eventset = TG_(add_event_group)(ev->inode->eventset,
                                                           EG_BC);
               break;
           case Ev_Bi:
               // extend event set by Bi counters
               ev->inode->eventset = TG_(add_event_group)(ev->inode->eventset,
                                                           EG_BI);
               break;
	   case Ev_G:
               // extend event set by Bus counter
	       ev->inode->eventset = TG_(add_event_group)(ev->inode->eventset,
							   EG_BUS);
	       break;
	   default:
	       tl_assert(0);
	   }
       }
   }

   for(i = 0; i < clgs->events_used; i = inew) {

      helperName = NULL;
      helperAddr = NULL;
      argv       = NULL;
      regparms   = 0;

      /* generate IR to notify event i and possibly the ones
	 immediately following it. */
      tl_assert(i >= 0 && i < clgs->events_used);

      ev  = &clgs->events[i];
      ev2 = ( i < clgs->events_used-1 ? &clgs->events[i+1] : NULL );
      ev3 = ( i < clgs->events_used-2 ? &clgs->events[i+2] : NULL );

      TG_DEBUGIF(5) {
	 VG_(printf)("   flush ");
	 showEvent( ev );
      }

      i_node_expr = mkIRExpr_HWord( (HWord)ev->inode );

      /* Decide on helper fn to call and args to pass it, and advance
	 i appropriately.
	 Dm events have same effect as Dw events */
      switch (ev->tag) {
	 case Ev_Ir:
	    /* Merge an Ir with a following Dr. */
	    if (ev2 && ev2->tag == Ev_Dr) {
	       /* Why is this true?  It's because we're merging an Ir
		  with a following Dr.  The Ir derives from the
		  instruction's IMark and the Dr from data
		  references which follow it.  In short it holds
		  because each insn starts with an IMark, hence an
		  Ev_Ir, and so these Dr must pertain to the
		  immediately preceding Ir.  Same applies to analogous
		  assertions in the subsequent cases. */
	       tl_assert(ev2->inode == ev->inode);
	       helperName = TG_(cachesim).log_1I1Dr_name;
	       helperAddr = TG_(cachesim).log_1I1Dr;
	       argv = mkIRExprVec_3( i_node_expr,
				     get_Event_dea(ev2),
				     mkIRExpr_HWord( get_Event_dszB(ev2) ) );
	       regparms = 3;
	       inew = i+2;
	    }
	    /* Merge an Ir with a following Dw/Dm. */
	    else
	    if (ev2 && (ev2->tag == Ev_Dw || ev2->tag == Ev_Dm)) {
	       tl_assert(ev2->inode == ev->inode);
	       helperName = TG_(cachesim).log_1I1Dw_name;
	       helperAddr = TG_(cachesim).log_1I1Dw;
	       argv = mkIRExprVec_3( i_node_expr,
				     get_Event_dea(ev2),
				     mkIRExpr_HWord( get_Event_dszB(ev2) ) );
	       regparms = 3;
	       inew = i+2;
	    }
	    /* Merge an Ir with two following Irs. */
	    else
	    if (ev2 && ev3 && ev2->tag == Ev_Ir && ev3->tag == Ev_Ir) {
	       helperName = TG_(cachesim).log_3I0D_name;
	       helperAddr = TG_(cachesim).log_3I0D;
	       argv = mkIRExprVec_3( i_node_expr,
				     mkIRExpr_HWord( (HWord)ev2->inode ),
				     mkIRExpr_HWord( (HWord)ev3->inode ) );
	       regparms = 3;
	       inew = i+3;
	    }
	    /* Merge an Ir with one following Ir. */
	    else
	    if (ev2 && ev2->tag == Ev_Ir) {
	       helperName = TG_(cachesim).log_2I0D_name;
	       helperAddr = TG_(cachesim).log_2I0D;
	       argv = mkIRExprVec_2( i_node_expr,
				     mkIRExpr_HWord( (HWord)ev2->inode ) );
	       regparms = 2;
	       inew = i+2;
	    }
	    /* No merging possible; emit as-is. */
	    else {
	       helperName = TG_(cachesim).log_1I0D_name;
	       helperAddr = TG_(cachesim).log_1I0D;
	       argv = mkIRExprVec_1( i_node_expr );
	       regparms = 1;
	       inew = i+1;
	    }
	    break;
	 case Ev_Dr:
	    /* Data read or modify */
	    helperName = TG_(cachesim).log_0I1Dr_name;
	    helperAddr = TG_(cachesim).log_0I1Dr;
	    argv = mkIRExprVec_3( i_node_expr,
				  get_Event_dea(ev),
				  mkIRExpr_HWord( get_Event_dszB(ev) ) );
	    regparms = 3;
	    inew = i+1;
	    break;
	 case Ev_Dw:
	 case Ev_Dm:
	    /* Data write */
	    helperName = TG_(cachesim).log_0I1Dw_name;
	    helperAddr = TG_(cachesim).log_0I1Dw;
	    argv = mkIRExprVec_3( i_node_expr,
				  get_Event_dea(ev),
				  mkIRExpr_HWord( get_Event_dszB(ev) ) );
	    regparms = 3;
	    inew = i+1;
	    break;
         case Ev_Bc:
            /* Conditional branch */
            helperName = "log_cond_branch";
            helperAddr = &log_cond_branch;
            argv = mkIRExprVec_2( i_node_expr, ev->Ev.Bc.taken );
            regparms = 2;
            inew = i+1;
            break;
         case Ev_Bi:
            /* Branch to an unknown destination */
            helperName = "log_ind_branch";
            helperAddr = &log_ind_branch;
            argv = mkIRExprVec_2( i_node_expr, ev->Ev.Bi.dst );
            regparms = 2;
            inew = i+1;
            break;
         case Ev_G:
            /* Global bus event (CAS, LOCK-prefix, LL-SC, etc) */
            helperName = "log_global_event";
            helperAddr = &log_global_event;
            argv = mkIRExprVec_1( i_node_expr );
            regparms = 1;
            inew = i+1;
            break;
	 default:
	    tl_assert(0);
      }

      TG_DEBUGIF(5) {
	  if (inew > i+1) {
	      VG_(printf)("   merge ");
	      showEvent( ev2 );
	  }
	  if (inew > i+2) {
	      VG_(printf)("   merge ");
	      showEvent( ev3 );
	  }
	  if (helperAddr)
	      VG_(printf)("   call  %s (%p)\n",
			  helperName, helperAddr);
      }

      /* helper could be unset depending on the simulator used */
      if (helperAddr == 0) continue;

      /* Add the helper. */
      tl_assert(helperName);
      tl_assert(helperAddr);
      tl_assert(argv);
      di = unsafeIRDirty_0_N( regparms,
			      helperName, VG_(fnptr_to_fnentry)( helperAddr ),
			      argv );
      addStmtToIRSB( clgs->sbOut, IRStmt_Dirty(di) );
   }

   clgs->events_used = 0;
}

static void addEvent_Ir ( ClgState* clgs, InstrInfo* inode )
{
   Event* evt;
   tl_assert(clgs->seen_before || (inode->eventset == 0));
   if (!TG_(clo).simulate_cache) return;

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag      = Ev_Ir;
   evt->inode    = inode;
   clgs->events_used++;
}

static
void addEvent_Dr ( ClgState* clgs, InstrInfo* inode, Int datasize, IRAtom* ea )
{
   Event* evt;
   tl_assert(isIRAtom(ea));
   tl_assert(datasize >= 1);
   if (!TG_(clo).simulate_cache) return;
   tl_assert(datasize <= TG_(min_line_size));

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag       = Ev_Dr;
   evt->inode     = inode;
   evt->Ev.Dr.szB = datasize;
   evt->Ev.Dr.ea  = ea;
   clgs->events_used++;
}

static
void addEvent_Dw ( ClgState* clgs, InstrInfo* inode, Int datasize, IRAtom* ea )
{
   Event* evt;
   tl_assert(isIRAtom(ea));
   tl_assert(datasize >= 1);
   if (!TG_(clo).simulate_cache) return;
   tl_assert(datasize <= TG_(min_line_size));

   /* Is it possible to merge this write with the preceding read? */
   if (clgs->events_used > 0) {
      Event* lastEvt = &clgs->events[clgs->events_used-1];
      if (   lastEvt->tag       == Ev_Dr
          && lastEvt->Ev.Dr.szB == datasize
          && lastEvt->inode     == inode
          && eqIRAtom(lastEvt->Ev.Dr.ea, ea))
      {
         lastEvt->tag   = Ev_Dm;
         return;
      }
   }

   /* No.  Add as normal. */
   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag       = Ev_Dw;
   evt->inode     = inode;
   evt->Ev.Dw.szB = datasize;
   evt->Ev.Dw.ea  = ea;
   clgs->events_used++;
}

static
void addEvent_D_guarded ( ClgState* clgs, InstrInfo* inode,
                          Int datasize, IRAtom* ea, IRAtom* guard,
                          Bool isWrite )
{
   tl_assert(isIRAtom(ea));
   tl_assert(guard);
   tl_assert(isIRAtom(guard));
   tl_assert(datasize >= 1);
   if (!TG_(clo).simulate_cache) return;
   tl_assert(datasize <= TG_(min_line_size));

   /* Adding guarded memory actions and merging them with the existing
      queue is too complex.  Simply flush the queue and add this
      action immediately.  Since guarded loads and stores are pretty
      rare, this is not thought likely to cause any noticeable
      performance loss as a result of the loss of event-merging
      opportunities. */
   tl_assert(clgs->events_used >= 0);
   flushEvents(clgs);
   tl_assert(clgs->events_used == 0);
   /* Same as case Ev_Dw / case Ev_Dr in flushEvents, except with guard */
   IRExpr*      i_node_expr;
   const HChar* helperName;
   void*        helperAddr;
   IRExpr**     argv;
   Int          regparms;
   IRDirty*     di;
   i_node_expr = mkIRExpr_HWord( (HWord)inode );
   helperName  = isWrite ? TG_(cachesim).log_0I1Dw_name
                         : TG_(cachesim).log_0I1Dr_name;
   helperAddr  = isWrite ? TG_(cachesim).log_0I1Dw
                         : TG_(cachesim).log_0I1Dr;
   argv        = mkIRExprVec_3( i_node_expr,
                                ea, mkIRExpr_HWord( datasize ) );
   regparms    = 3;
   di          = unsafeIRDirty_0_N(
                    regparms,
                    helperName, VG_(fnptr_to_fnentry)( helperAddr ),
                    argv );
   di->guard = guard;
   addStmtToIRSB( clgs->sbOut, IRStmt_Dirty(di) );
}

static
void addEvent_Bc ( ClgState* clgs, InstrInfo* inode, IRAtom* guard )
{
   Event* evt;
   tl_assert(isIRAtom(guard));
   tl_assert(typeOfIRExpr(clgs->sbOut->tyenv, guard)
             == (sizeof(RegWord)==4 ? Ity_I32 : Ity_I64));
   if (!TG_(clo).simulate_branch) return;

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag         = Ev_Bc;
   evt->inode       = inode;
   evt->Ev.Bc.taken = guard;
   clgs->events_used++;
}

static
void addEvent_Bi ( ClgState* clgs, InstrInfo* inode, IRAtom* whereTo )
{
   Event* evt;
   tl_assert(isIRAtom(whereTo));
   tl_assert(typeOfIRExpr(clgs->sbOut->tyenv, whereTo)
             == (sizeof(RegWord)==4 ? Ity_I32 : Ity_I64));
   if (!TG_(clo).simulate_branch) return;

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag       = Ev_Bi;
   evt->inode     = inode;
   evt->Ev.Bi.dst = whereTo;
   clgs->events_used++;
}

static
void addEvent_G ( ClgState* clgs, InstrInfo* inode )
{
   Event* evt;
   if (!TG_(clo).collect_bus) return;

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag       = Ev_G;
   evt->inode     = inode;
   clgs->events_used++;
}

/* Initialise or check (if already seen before) an InstrInfo for next insn.
   We only can set instr_offset/instr_size here. The required event set and
   resulting cost offset depend on events (Ir/Dr/Dw/Dm) in guest
   instructions. The event set is extended as required on flush of the event
   queue (when Dm events were determined), cost offsets are determined at
   end of BB instrumentation. */
static
InstrInfo* next_InstrInfo ( ClgState* clgs, UInt instr_size )
{
   InstrInfo* ii;
   tl_assert(clgs->ii_index < clgs->bb->instr_count);
   ii = &clgs->bb->instr[ clgs->ii_index ];

   if (clgs->seen_before) {
       TG_ASSERT(ii->instr_offset == clgs->instr_offset);
       TG_ASSERT(ii->instr_size == instr_size);
   }
   else {
       ii->instr_offset = clgs->instr_offset;
       ii->instr_size = instr_size;
       ii->cost_offset = 0;
       ii->eventset = 0;
   }

   clgs->ii_index++;
   clgs->instr_offset += instr_size;
   TG_(stat).distinct_instrs++;

   return ii;
}

// return total number of cost values needed for this BB
static
UInt update_cost_offsets( ClgState* clgs )
{
    Int i;
    InstrInfo* ii;
    UInt cost_offset = 0;

    TG_ASSERT(clgs->bb->instr_count == clgs->ii_index);
    for(i=0; i<clgs->ii_index; i++) {
	ii = &clgs->bb->instr[i];
	if (clgs->seen_before) {
	    TG_ASSERT(ii->cost_offset == cost_offset);
	} else
	    ii->cost_offset = cost_offset;
	cost_offset += ii->eventset ? ii->eventset->size : 0;
    }

    return cost_offset;
}

/*------------------------------------------------------------*/
/*--- Instrumentation                                      ---*/
/*------------------------------------------------------------*/

#if defined(VG_BIGENDIAN)
# define CLGEndness Iend_BE
#elif defined(VG_LITTLEENDIAN)
# define CLGEndness Iend_LE
#else
# error "Unknown endianness"
#endif

static
Addr IRConst2Addr(IRConst* con)
{
    Addr addr;

    if (sizeof(RegWord) == 4) {
	TG_ASSERT( con->tag == Ico_U32 );
	addr = con->Ico.U32;
    }
    else if (sizeof(RegWord) == 8) {
	TG_ASSERT( con->tag == Ico_U64 );
	addr = con->Ico.U64;
    }
    else
	VG_(tool_panic)("Tracegrind: invalid Addr type");

    return addr;
}

/* First pass over a BB to instrument, counting instructions and jumps
 * This is needed for the size of the BB struct to allocate
 *
 * Called from TG_(get_bb)
 */
void TG_(collectBlockInfo)(IRSB* sbIn,
			    /*INOUT*/ UInt* instrs,
			    /*INOUT*/ UInt* cjmps,
			    /*INOUT*/ Bool* cjmp_inverted)
{
    Int i;
    IRStmt* st;
    Addr instrAddr =0, jumpDst;
    UInt instrLen = 0;
    Bool toNextInstr = False;

    // Ist_Exit has to be ignored in preamble code, before first IMark:
    // preamble code is added by VEX for self modifying code, and has
    // nothing to do with client code
    Bool inPreamble = True;

    if (!sbIn) return;

    for (i = 0; i < sbIn->stmts_used; i++) {
	  st = sbIn->stmts[i];
	  if (Ist_IMark == st->tag) {
	      inPreamble = False;

	      instrAddr = st->Ist.IMark.addr;
	      instrLen  = st->Ist.IMark.len;

	      (*instrs)++;
	      toNextInstr = False;
	  }
	  if (inPreamble) continue;
	  if (Ist_Exit == st->tag) {
	      jumpDst = IRConst2Addr(st->Ist.Exit.dst);
	      toNextInstr =  (jumpDst == instrAddr + instrLen);

	      (*cjmps)++;
	  }
    }

    /* if the last instructions of BB conditionally jumps to next instruction
     * (= first instruction of next BB in memory), this is a inverted by VEX.
     */
    *cjmp_inverted = toNextInstr;
}

static
void addConstMemStoreStmt( IRSB* bbOut, UWord addr, UInt val, IRType hWordTy)
{
    addStmtToIRSB( bbOut,
		   IRStmt_Store(CLGEndness,
				IRExpr_Const(hWordTy == Ity_I32 ?
					     IRConst_U32( addr ) :
					     IRConst_U64( addr )),
				IRExpr_Const(IRConst_U32(val)) ));
}


/* add helper call to setup_bbcc, with pointer to BB struct as argument
 *
 * precondition for setup_bbcc:
 * - jmps_passed has number of cond.jumps passed in last executed BB
 * - current_bbcc has a pointer to the BBCC of the last executed BB
 *   Thus, if bbcc_jmpkind is != -1 (JmpNone),
 *     current_bbcc->bb->jmp_addr
 *   gives the address of the jump source.
 *
 * the setup does 2 things:
 * - trace call:
 *   * Unwind own call stack, i.e sync our ESP with real ESP
 *     This is for ESP manipulation (longjmps, C++ exec handling) and RET
 *   * For CALLs or JMPs crossing objects, record call arg +
 *     push are on own call stack
 *
 * - prepare for cache log functions:
 *   set current_bbcc to BBCC that gets the costs for this BB execution
 *   attached
 */
static
void addBBSetupCall(ClgState* clgs)
{
   IRDirty* di;
   IRExpr  *arg1, **argv;

   arg1 = mkIRExpr_HWord( (HWord)clgs->bb );
   argv = mkIRExprVec_1(arg1);
   di = unsafeIRDirty_0_N( 1, "setup_bbcc",
			      VG_(fnptr_to_fnentry)( & TG_(setup_bbcc) ),
			      argv);
   addStmtToIRSB( clgs->sbOut, IRStmt_Dirty(di) );
}


static
IRSB* TG_(instrument)( VgCallbackClosure* closure,
                        IRSB* sbIn,
			const VexGuestLayout* layout,
			const VexGuestExtents* vge,
                        const VexArchInfo* archinfo_host,
			IRType gWordTy, IRType hWordTy )
{
   Int        i;
   IRStmt*    st;
   Addr       origAddr;
   InstrInfo* curr_inode = NULL;
   ClgState   clgs;
   UInt       cJumps = 0;
   IRTypeEnv* tyenv = sbIn->tyenv;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   // No instrumentation if it is switched off
   if (! TG_(instrument_state)) {
       TG_DEBUG(5, "instrument(BB %#lx) [Instrumentation OFF]\n",
		 (Addr)closure->readdr);
       return sbIn;
   }

   TG_DEBUG(3, "+ instrument(BB %#lx)\n", (Addr)closure->readdr);

   /* Set up SB for instrumented IR */
   clgs.sbOut = deepCopyIRSBExceptStmts(sbIn);

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( clgs.sbOut, sbIn->stmts[i] );
      i++;
   }

   // Get the first statement, and origAddr from it
   TG_ASSERT(sbIn->stmts_used >0);
   TG_ASSERT(i < sbIn->stmts_used);
   st = sbIn->stmts[i];
   TG_ASSERT(Ist_IMark == st->tag);

   origAddr = st->Ist.IMark.addr + st->Ist.IMark.delta;
   TG_ASSERT(origAddr == st->Ist.IMark.addr
                          + st->Ist.IMark.delta);  // XXX: check no overflow

   /* Get BB struct (creating if necessary).
    * JS: The hash table is keyed with orig_addr_noredir -- important!
    * JW: Why? If it is because of different chasing of the redirection,
    *     this is not needed, as chasing is switched off in tracegrind
    */
   clgs.bb = TG_(get_bb)(origAddr, sbIn, &(clgs.seen_before));

   addBBSetupCall(&clgs);

   // Set up running state
   clgs.events_used = 0;
   clgs.ii_index = 0;
   clgs.instr_offset = 0;

   for (/*use current i*/; i < sbIn->stmts_used; i++) {

      st = sbIn->stmts[i];
      TG_ASSERT(isFlatIRStmt(st));

      switch (st->tag) {
	 case Ist_NoOp:
	 case Ist_AbiHint:
	 case Ist_Put:
	 case Ist_PutI:
	 case Ist_MBE:
	    break;

	 case Ist_IMark: {
            Addr   cia   = st->Ist.IMark.addr + st->Ist.IMark.delta;
            UInt   isize = st->Ist.IMark.len;
            TG_ASSERT(clgs.instr_offset == cia - origAddr);
	    // If Vex fails to decode an instruction, the size will be zero.
	    // Pretend otherwise.
	    if (isize == 0) isize = VG_MIN_INSTR_SZB;

	    // Sanity-check size.
	    tl_assert( (VG_MIN_INSTR_SZB <= isize && isize <= VG_MAX_INSTR_SZB)
		     || VG_CLREQ_SZB == isize );

	    // Init the inode, record it as the current one.
	    // Subsequent Dr/Dw/Dm events from the same instruction will
	    // also use it.
	    curr_inode = next_InstrInfo (&clgs, isize);

	    addEvent_Ir( &clgs, curr_inode );
	    break;
	 }

	 case Ist_WrTmp: {
	    IRExpr* data = st->Ist.WrTmp.data;
	    if (data->tag == Iex_Load) {
	       IRExpr* aexpr = data->Iex.Load.addr;
	       // Note also, endianness info is ignored.  I guess
	       // that's not interesting.
	       addEvent_Dr( &clgs, curr_inode,
			    sizeofIRType(data->Iex.Load.ty), aexpr );
	    }
	    break;
	 }

	 case Ist_Store: {
	    IRExpr* data  = st->Ist.Store.data;
	    IRExpr* aexpr = st->Ist.Store.addr;
	    addEvent_Dw( &clgs, curr_inode,
			 sizeofIRType(typeOfIRExpr(sbIn->tyenv, data)), aexpr );
	    break;
	 }

         case Ist_StoreG: {
            IRStoreG* sg   = st->Ist.StoreG.details;
            IRExpr*   data = sg->data;
            IRExpr*   addr = sg->addr;
            IRType    type = typeOfIRExpr(tyenv, data);
            tl_assert(type != Ity_INVALID);
            addEvent_D_guarded( &clgs, curr_inode,
                                sizeofIRType(type), addr, sg->guard,
                                True/*isWrite*/ );
            break;
         }

         case Ist_LoadG: {
            IRLoadG* lg       = st->Ist.LoadG.details;
            IRType   type     = Ity_INVALID; /* loaded type */
            IRType   typeWide = Ity_INVALID; /* after implicit widening */
            IRExpr*  addr     = lg->addr;
            typeOfIRLoadGOp(lg->cvt, &typeWide, &type);
            tl_assert(type != Ity_INVALID);
            addEvent_D_guarded( &clgs, curr_inode,
                                sizeofIRType(type), addr, lg->guard,
                                False/*!isWrite*/ );
            break;
         }

	 case Ist_Dirty: {
	    Int      dataSize;
	    IRDirty* d = st->Ist.Dirty.details;
	    if (d->mFx != Ifx_None) {
	       /* This dirty helper accesses memory.  Collect the details. */
	       tl_assert(d->mAddr != NULL);
	       tl_assert(d->mSize != 0);
	       dataSize = d->mSize;
	       // Large (eg. 28B, 108B, 512B on x86) data-sized
	       // instructions will be done inaccurately, but they're
	       // very rare and this avoids errors from hitting more
	       // than two cache lines in the simulation.
	       if (TG_(clo).simulate_cache && dataSize > TG_(min_line_size))
		  dataSize = TG_(min_line_size);
	       if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify)
		  addEvent_Dr( &clgs, curr_inode, dataSize, d->mAddr );
	       if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify)
		  addEvent_Dw( &clgs, curr_inode, dataSize, d->mAddr );
	    } else {
	       tl_assert(d->mAddr == NULL);
	       tl_assert(d->mSize == 0);
	    }
	    break;
	 }

         case Ist_CAS: {
            /* We treat it as a read and a write of the location.  I
               think that is the same behaviour as it was before IRCAS
               was introduced, since prior to that point, the Vex
               front ends would translate a lock-prefixed instruction
               into a (normal) read followed by a (normal) write. */
            Int    dataSize;
            IRCAS* cas = st->Ist.CAS.details;
            TG_ASSERT(cas->addr && isIRAtom(cas->addr));
            TG_ASSERT(cas->dataLo);
            dataSize = sizeofIRType(typeOfIRExpr(sbIn->tyenv, cas->dataLo));
            if (cas->dataHi != NULL)
               dataSize *= 2; /* since this is a doubleword-cas */
            addEvent_Dr( &clgs, curr_inode, dataSize, cas->addr );
            addEvent_Dw( &clgs, curr_inode, dataSize, cas->addr );
            addEvent_G(  &clgs, curr_inode );
            break;
         }

         case Ist_LLSC: {
            IRType dataTy;
            if (st->Ist.LLSC.storedata == NULL) {
               /* LL */
               dataTy = typeOfIRTemp(sbIn->tyenv, st->Ist.LLSC.result);
               addEvent_Dr( &clgs, curr_inode,
                            sizeofIRType(dataTy), st->Ist.LLSC.addr );
               /* flush events before LL, should help SC to succeed */
               flushEvents( &clgs );
            } else {
               /* SC */
               dataTy = typeOfIRExpr(sbIn->tyenv, st->Ist.LLSC.storedata);
               addEvent_Dw( &clgs, curr_inode,
                            sizeofIRType(dataTy), st->Ist.LLSC.addr );
               /* I don't know whether the global-bus-lock cost should
                  be attributed to the LL or the SC, but it doesn't
                  really matter since they always have to be used in
                  pairs anyway.  Hence put it (quite arbitrarily) on
                  the SC. */
               addEvent_G(  &clgs, curr_inode );
            }
            break;
         }

 	 case Ist_Exit: {
            Bool guest_exit, inverted;

            /* VEX code generation sometimes inverts conditional branches.
             * As Tracegrind counts (conditional) jumps, it has to correct
             * inversions. The heuristic is the following:
             * (1) Tracegrind switches off SB chasing and unrolling, and
             *     therefore it assumes that a candidate for inversion only is
             *     the last conditional branch in an SB.
             * (2) inversion is assumed if the branch jumps to the address of
             *     the next guest instruction in memory.
             * This heuristic is precalculated in TG_(collectBlockInfo)().
             *
             * Branching behavior is also used for branch prediction. Note that
             * above heuristic is different from what Cachegrind does.
             * Cachegrind uses (2) for all branches.
             */
            if (cJumps+1 == clgs.bb->cjmp_count)
                inverted = clgs.bb->cjmp_inverted;
            else
                inverted = False;

            // call branch predictor only if this is a branch in guest code
            guest_exit = (st->Ist.Exit.jk == Ijk_Boring) ||
                         (st->Ist.Exit.jk == Ijk_Call) ||
                         (st->Ist.Exit.jk == Ijk_Ret);

            if (guest_exit) {
                /* Stuff to widen the guard expression to a host word, so
                   we can pass it to the branch predictor simulation
                   functions easily. */
                IRType   tyW    = hWordTy;
                IROp     widen  = tyW==Ity_I32  ? Iop_1Uto32  : Iop_1Uto64;
                IROp     opXOR  = tyW==Ity_I32  ? Iop_Xor32   : Iop_Xor64;
                IRTemp   guard1 = newIRTemp(clgs.sbOut->tyenv, Ity_I1);
                IRTemp   guardW = newIRTemp(clgs.sbOut->tyenv, tyW);
                IRTemp   guard  = newIRTemp(clgs.sbOut->tyenv, tyW);
                IRExpr*  one    = tyW==Ity_I32 ? IRExpr_Const(IRConst_U32(1))
                                               : IRExpr_Const(IRConst_U64(1));

                /* Widen the guard expression. */
                addStmtToIRSB( clgs.sbOut,
                               IRStmt_WrTmp( guard1, st->Ist.Exit.guard ));
                addStmtToIRSB( clgs.sbOut,
                               IRStmt_WrTmp( guardW,
                                             IRExpr_Unop(widen,
                                                         IRExpr_RdTmp(guard1))) );
                /* If the exit is inverted, invert the sense of the guard. */
                addStmtToIRSB(
                        clgs.sbOut,
                        IRStmt_WrTmp(
                                guard,
                                inverted ? IRExpr_Binop(opXOR, IRExpr_RdTmp(guardW), one)
                                    : IRExpr_RdTmp(guardW)
                                    ));
                /* And post the event. */
                addEvent_Bc( &clgs, curr_inode, IRExpr_RdTmp(guard) );
            }

	    /* We may never reach the next statement, so need to flush
	       all outstanding transactions now. */
	    flushEvents( &clgs );

	    TG_ASSERT(clgs.ii_index>0);
	    if (!clgs.seen_before) {
	      TgJumpKind jk;

	      if      (st->Ist.Exit.jk == Ijk_Call) jk = jk_Call;
	      else if (st->Ist.Exit.jk == Ijk_Ret)  jk = jk_Return;
	      else {
		if (IRConst2Addr(st->Ist.Exit.dst) ==
		    origAddr + curr_inode->instr_offset + curr_inode->instr_size)
		  jk = jk_None;
		else
		  jk = jk_Jump;
	      }

	      clgs.bb->jmp[cJumps].instr = clgs.ii_index-1;
	      clgs.bb->jmp[cJumps].jmpkind = jk;
	    }

	    /* Update global variable jmps_passed before the jump
	     * A correction is needed if VEX inverted the last jump condition
	    */
	    UInt val = inverted ? cJumps+1 : cJumps;
	    addConstMemStoreStmt( clgs.sbOut,
				  (UWord) &TG_(current_state).jmps_passed,
				  val, hWordTy);
	    cJumps++;

	    break;
	 }

	 default:
	    tl_assert(0);
	    break;
      }

      /* Copy the original statement */
      addStmtToIRSB( clgs.sbOut, st );

      TG_DEBUGIF(5) {
	 VG_(printf)("   pass  ");
	 ppIRStmt(st);
	 VG_(printf)("\n");
      }
   }

   /* Deal with branches to unknown destinations.  Except ignore ones
      which are function returns as we assume the return stack
      predictor never mispredicts. */
   if ((sbIn->jumpkind == Ijk_Boring) || (sbIn->jumpkind == Ijk_Call)) {
      if (0) { ppIRExpr( sbIn->next ); VG_(printf)("\n"); }
      switch (sbIn->next->tag) {
         case Iex_Const:
            break; /* boring - branch to known address */
         case Iex_RdTmp:
            /* looks like an indirect branch (branch to unknown) */
            addEvent_Bi( &clgs, curr_inode, sbIn->next );
            break;
         default:
            /* shouldn't happen - if the incoming IR is properly
               flattened, should only have tmp and const cases to
               consider. */
            tl_assert(0);
      }
   }

   /* At the end of the bb.  Flush outstandings. */
   flushEvents( &clgs );

   /* Update global variable jmps_passed at end of SB.
    * As TG_(current_state).jmps_passed is reset to 0 in setup_bbcc,
    * this can be omitted if there is no conditional jump in this SB.
    * A correction is needed if VEX inverted the last jump condition
    */
   if (cJumps>0) {
      UInt jmps_passed = cJumps;
      if (clgs.bb->cjmp_inverted) jmps_passed--;
      addConstMemStoreStmt( clgs.sbOut,
			    (UWord) &TG_(current_state).jmps_passed,
			    jmps_passed, hWordTy);
   }
   TG_ASSERT(clgs.bb->cjmp_count == cJumps);
   TG_ASSERT(clgs.bb->instr_count == clgs.ii_index);

   /* Info for final exit from BB */
   {
     TgJumpKind jk;

     if      (sbIn->jumpkind == Ijk_Call) jk = jk_Call;
     else if (sbIn->jumpkind == Ijk_Ret)  jk = jk_Return;
     else {
       jk = jk_Jump;
       if ((sbIn->next->tag == Iex_Const) &&
	   (IRConst2Addr(sbIn->next->Iex.Const.con) ==
	    origAddr + clgs.instr_offset))
	 jk = jk_None;
     }
     clgs.bb->jmp[cJumps].jmpkind = jk;
     /* Instruction index of the call/ret at BB end
      * (it is wrong for fall-through, but does not matter) */
     clgs.bb->jmp[cJumps].instr = clgs.ii_index-1;
   }

   /* swap information of last exit with final exit if inverted */
   if (clgs.bb->cjmp_inverted) {
     TgJumpKind jk;
     UInt instr;

     jk = clgs.bb->jmp[cJumps].jmpkind;
     clgs.bb->jmp[cJumps].jmpkind = clgs.bb->jmp[cJumps-1].jmpkind;
     clgs.bb->jmp[cJumps-1].jmpkind = jk;
     instr = clgs.bb->jmp[cJumps].instr;
     clgs.bb->jmp[cJumps].instr = clgs.bb->jmp[cJumps-1].instr;
     clgs.bb->jmp[cJumps-1].instr = instr;
   }

   if (clgs.seen_before) {
       TG_ASSERT(clgs.bb->cost_count == update_cost_offsets(&clgs));
       TG_ASSERT(clgs.bb->instr_len == clgs.instr_offset);
   }
   else {
       clgs.bb->cost_count = update_cost_offsets(&clgs);
       clgs.bb->instr_len = clgs.instr_offset;
   }

   TG_DEBUG(3, "- instrument(BB %#lx): byteLen %u, CJumps %u, CostLen %u\n",
	     origAddr, clgs.bb->instr_len,
	     clgs.bb->cjmp_count, clgs.bb->cost_count);
   if (cJumps>0) {
       TG_DEBUG(3, "                     [ ");
       for (i=0;i<cJumps;i++)
	   TG_DEBUG(3, "%u ", clgs.bb->jmp[i].instr);
       TG_DEBUG(3, "], last inverted: %s \n",
		 clgs.bb->cjmp_inverted ? "yes":"no");
   }

  return clgs.sbOut;
}

/*--------------------------------------------------------------------*/
/*--- Discarding BB info                                           ---*/
/*--------------------------------------------------------------------*/

// Called when a translation is removed from the translation cache for
// any reason at all: to free up space, because the guest code was
// unmapped or modified, or for any arbitrary reason.
static
void tg_discard_superblock_info ( Addr orig_addr, VexGuestExtents vge )
{
    tl_assert(vge.n_used > 0);

   if (0)
      VG_(printf)( "discard_superblock_info: %p, %p, %llu\n",
                   (void*)orig_addr,
                   (void*)vge.base[0], (ULong)vge.len[0]);

   // Get BB info, remove from table, free BB info.  Simple!
   // When created, the BB is keyed by the first instruction address,
   // (not orig_addr, but eventually redirected address). Thus, we
   // use the first instruction address in vge.
   TG_(delete_bb)(vge.base[0]);
}


/*------------------------------------------------------------*/
/*--- TG_(fini)() and related function                     ---*/
/*------------------------------------------------------------*/



static
void unwind_thread(thread_info* t)
{
  /* unwind signal handlers */
  while(TG_(current_state).sig !=0)
    TG_(post_signal)(TG_(current_tid),TG_(current_state).sig);

  /* unwind regular call stack */
  while(TG_(current_call_stack).sp>0)
    TG_(pop_call_stack)();

  /* reset context and function stack for context generation */
  TG_(init_exec_state)( &TG_(current_state) );
  TG_(current_fn_stack).top = TG_(current_fn_stack).bottom;
}

static
void zero_state_cost(thread_info* t)
{
    TG_(zero_cost)( TG_(sets).full, TG_(current_state).cost );
}

void TG_(set_instrument_state)(const HChar* reason, Bool state)
{
  if (TG_(instrument_state) == state) {
    TG_DEBUG(2, "%s: instrumentation already %s\n",
	     reason, state ? "ON" : "OFF");
    return;
  }
  TG_(instrument_state) = state;
  TG_DEBUG(2, "%s: Switching instrumentation %s ...\n",
	   reason, state ? "ON" : "OFF");

  VG_(discard_translations_safely)( (Addr)0x1000, ~(SizeT)0xfff, "tracegrind");

  /* reset internal state: call stacks, simulator */
  TG_(forall_threads)(unwind_thread);
  TG_(forall_threads)(zero_state_cost);
  (*TG_(cachesim).clear)();

  if (VG_(clo_verbosity) > 1)
    VG_(message)(Vg_DebugMsg, "%s: instrumentation switched %s\n",
		 reason, state ? "ON" : "OFF");
}

/* helper for dump_state_togdb */
static void dump_state_of_thread_togdb(thread_info* ti)
{
    static FullCost sum = 0, tmp = 0;
    Int t, i;
    BBCC *from, *to;
    call_entry* ce;
    HChar *mcost;

    t = TG_(current_tid);
    TG_(init_cost_lz)( TG_(sets).full, &sum );
    TG_(copy_cost_lz)( TG_(sets).full, &tmp, ti->lastdump_cost );
    TG_(add_diff_cost)( TG_(sets).full, sum, ti->lastdump_cost,
			 ti->states.entry[0]->cost);
    TG_(copy_cost)( TG_(sets).full, ti->lastdump_cost, tmp );
    mcost = TG_(mappingcost_as_string)(TG_(dumpmap), sum);
    VG_(gdb_printf)("events-%d: %s\n", t, mcost);
    VG_(free)(mcost);
    VG_(gdb_printf)("frames-%d: %d\n", t, TG_(current_call_stack).sp);

    ce = 0;
    for(i = 0; i < TG_(current_call_stack).sp; i++) {
      ce = TG_(get_call_entry)(i);
      /* if this frame is skipped, we don't have counters */
      if (!ce->jcc) continue;

      from = ce->jcc->from;
      VG_(gdb_printf)("function-%d-%d: %s\n",t, i, from->cxt->fn[0]->name);
      VG_(gdb_printf)("calls-%d-%d: %llu\n",t, i, ce->jcc->call_counter);

      /* FIXME: EventSets! */
      TG_(copy_cost)( TG_(sets).full, sum, ce->jcc->cost );
      TG_(copy_cost)( TG_(sets).full, tmp, ce->enter_cost );
      TG_(add_diff_cost)( TG_(sets).full, sum,
			  ce->enter_cost, TG_(current_state).cost );
      TG_(copy_cost)( TG_(sets).full, ce->enter_cost, tmp );

      mcost = TG_(mappingcost_as_string)(TG_(dumpmap), sum);
      VG_(gdb_printf)("events-%d-%d: %s\n",t, i, mcost);
      VG_(free)(mcost);
    }
    if (ce && ce->jcc) {
      to = ce->jcc->to;
      VG_(gdb_printf)("function-%d-%d: %s\n",t, i, to->cxt->fn[0]->name );
    }
}

/* Dump current state */
static void dump_state_togdb(void)
{
    thread_info** th;
    int t;
    Int orig_tid = TG_(current_tid);

    VG_(gdb_printf)("instrumentation: %s\n",
		    TG_(instrument_state) ? "on":"off");
    if (!TG_(instrument_state)) return;

    VG_(gdb_printf)("executed-bbs: %llu\n", TG_(stat).bb_executions);
    VG_(gdb_printf)("executed-calls: %llu\n", TG_(stat).call_counter);
    VG_(gdb_printf)("distinct-bbs: %d\n", TG_(stat).distinct_bbs);
    VG_(gdb_printf)("distinct-calls: %d\n", TG_(stat).distinct_jccs);
    VG_(gdb_printf)("distinct-functions: %d\n", TG_(stat).distinct_fns);
    VG_(gdb_printf)("distinct-contexts: %d\n", TG_(stat).distinct_contexts);

    /* "events:" line. Given here because it will be dynamic in the future */
    HChar *evmap = TG_(eventmapping_as_string)(TG_(dumpmap));
    VG_(gdb_printf)("events: %s\n", evmap);
    VG_(free)(evmap);
    /* Total cost summary */

    /* threads */
    th = TG_(get_threads)();
    VG_(gdb_printf)("threads:");
    for(t=1;t<VG_N_THREADS;t++) {
	if (!th[t]) continue;
	VG_(gdb_printf)(" %d", t);
    }
    VG_(gdb_printf)("\n");
    VG_(gdb_printf)("current-tid: %d\n", orig_tid);
    TG_(forall_threads)(dump_state_of_thread_togdb);
}


static void print_monitor_help ( void )
{
   VG_(gdb_printf) ("\n");
   VG_(gdb_printf) ("tracegrind monitor commands:\n");
   VG_(gdb_printf) ("  status\n");
   VG_(gdb_printf) ("        print status\n");
   VG_(gdb_printf) ("  instrumentation [on|off]\n");
   VG_(gdb_printf) ("        get/set (if on/off given) instrumentation state\n");
   VG_(gdb_printf) ("\n");
}

/* return True if request recognised, False otherwise */
static Bool handle_gdb_monitor_command (ThreadId tid, const HChar *req)
{
   HChar* wcmd;
   HChar s[VG_(strlen)(req) + 1]; /* copy for strtok_r */
   HChar *ssaveptr;

   VG_(strcpy) (s, req);

   wcmd = VG_(strtok_r) (s, " ", &ssaveptr);
   switch (VG_(keyword_id) ("help status instrumentation",
                            wcmd, kwd_report_duplicated_matches)) {
   case -2: /* multiple matches */
      return True;
   case -1: /* not found */
      return False;
   case  0: /* help */
      print_monitor_help();
      return True;

   case 1: { /* status */
     HChar* arg = VG_(strtok_r) (0, " ", &ssaveptr);
     if (arg && (VG_(strcmp)(arg, "internal") == 0)) {
       /* internal interface to tracegrind_control */
       dump_state_togdb();
       return True;
     }

     if (!TG_(instrument_state)) {
       VG_(gdb_printf)("No status available as instrumentation is switched off\n");
     } else {
       // Status information to be improved ...
       thread_info** th = TG_(get_threads)();
       Int t, tcount = 0;
       for(t=1;t<VG_N_THREADS;t++)
	 if (th[t]) tcount++;
       VG_(gdb_printf)("%d thread(s) running.\n", tcount);
     }
     return True;
   }

   case 2: { /* instrumentation */
     HChar* arg = VG_(strtok_r) (0, " ", &ssaveptr);
     if (!arg) {
       VG_(gdb_printf)("instrumentation: %s\n",
		       TG_(instrument_state) ? "on":"off");
     }
     else
       TG_(set_instrument_state)("Command", VG_(strcmp)(arg,"off")!=0);
     return True;
   }

   default:
      tl_assert(0);
      return False;
   }
}

static
Bool TG_(handle_client_request)(ThreadId tid, UWord *args, UWord *ret)
{
   if (!VG_IS_TOOL_USERREQ('C','T',args[0])
       && VG_USERREQ__GDB_MONITOR_COMMAND   != args[0])
      return False;

   switch(args[0]) {
   case VG_USERREQ__TOGGLE_COLLECT:
     TG_(current_state).collect = !TG_(current_state).collect;
     TG_DEBUG(2, "Client Request: toggled collection state to %s\n",
	      TG_(current_state).collect ? "ON" : "OFF");
     *ret = 0;                 /* meaningless */
     break;

   case VG_USERREQ__ADD_MARKER:
     {
       const HChar *marker = (HChar*)args[1];
       TG_DEBUG(2, "Client Request: add marker '%s'\n", marker);
       TG_(trace_emit_marker)(tid, marker);
       *ret = 0;                 /* meaningless */
     }
     break;

   case VG_USERREQ__START_INSTRUMENTATION:
     TG_(set_instrument_state)("Client Request", True);
     *ret = 0;                 /* meaningless */
     break;

   case VG_USERREQ__STOP_INSTRUMENTATION:
     TG_(set_instrument_state)("Client Request", False);
     *ret = 0;                 /* meaningless */
     break;

   case VG_USERREQ__GDB_MONITOR_COMMAND: {
      Bool handled = handle_gdb_monitor_command (tid, (HChar*)args[1]);
      if (handled)
         *ret = 1;
      else
         *ret = 0;
      return handled;
   }
   case VG_USERREQ__DUMP_STATS:
   case VG_USERREQ__ZERO_STATS:
     TG_DEBUG(2, "Client Request: ignoring  %llx\n", (ULong)args[0]);
     *ret = 0;                 /* meaningless */
     break;

   default:
      VG_(message)(Vg_UserMsg,
                   "Warning: unknown tracegrind client request code %llx\n",
                   (ULong)args[0]);
      return False;
   }

   return True;
}


/* Syscall Timing.  syscalltime[tid] is the time at which thread tid last
   started a syscall.  */

/* struct vki_timespec syscalltime[VG_N_THREADS];
   Whatever the syscall we use to measure the syscall time, we convert to
   seconds and nanoseconds.  */
struct vki_timespec *syscalltime;
struct vki_timespec *syscallcputime;


static
void collect_time (struct vki_timespec *systime, struct vki_timespec *syscputime)
{
  switch (TG_(clo).collect_systime) {
    default: tl_assert (0);
    case systime_msec: {
      UInt ms_timer = VG_(read_millisecond_timer)();
      systime->tv_sec = ms_timer / 1000;
      systime->tv_nsec = (ms_timer % 1000) * 1000000L;
      break;
    }
    case systime_usec: {
      struct vki_timeval tv_now;
      VG_(gettimeofday)(&tv_now, NULL);
      systime->tv_sec = tv_now.tv_sec;
      systime->tv_nsec = tv_now.tv_usec * 1000;
      break;
    }
   case systime_nsec:
#  if defined(VGO_linux) || defined(VGO_solaris) || defined(VGO_freebsd)
      VG_(clock_gettime)(systime, VKI_CLOCK_MONOTONIC);
      VG_(clock_gettime)(syscputime, VKI_CLOCK_THREAD_CPUTIME_ID);

#  elif defined(VGO_darwin)
      tl_assert(0);
#  else
#     error "Unknown OS"
#  endif
      break;
  }
}

static
void TG_(pre_syscall)(ThreadId tid, UInt syscallno,
                      UWord* args, UInt nArgs)
{
  /* Collect time for systime tracking if enabled */
  if (TG_(clo).collect_systime != systime_no) {
    collect_time(&syscalltime[tid],
                 TG_(clo).collect_systime == systime_nsec ? &syscallcputime[tid] : NULL);
  }
}

/* Returns "after - before" in the unit as specified by --collect-systime.
   after is supposed to be >= before, and tv_nsec must be >= 0 and < One_Second_In_Nsec.  */
static
ULong vki_timespec_diff (struct vki_timespec after, struct vki_timespec before)
{
   vki_time_t diff_sec = after.tv_sec - before.tv_sec;
   long diff_nsec = after.tv_nsec - before.tv_nsec;
   ULong nsec_factor; // factor to convert the desired unit into nsec.

   if (diff_nsec < 0) {
      diff_sec--;
      diff_nsec += 1000000000ULL;
   }
  switch (TG_(clo).collect_systime) {
    case systime_no: tl_assert (0);
    case systime_msec: nsec_factor = 1000000ULL; break;
    case systime_usec: nsec_factor = 1000ULL; break;
    case systime_nsec: nsec_factor = 1ULL; break;
    default: tl_assert(0);
  }
  return ((ULong) diff_sec * 1000000000ULL + diff_nsec) / nsec_factor;
}

/* Check if syscall is a fork-like call that creates a new process */
static Bool is_fork_syscall(UInt syscallno)
{
#if defined(VGO_linux)
   return syscallno == __NR_clone
       || syscallno == __NR_fork
       || syscallno == __NR_vfork
#  if defined(__NR_clone3)
       || syscallno == __NR_clone3
#  endif
       ;
#else
   return False;  /* TODO: support other OSes */
#endif
}

static
void TG_(post_syscall)(ThreadId tid, UInt syscallno,
                       UWord* args, UInt nArgs, SysRes res)
{
  /* Handle fork/clone: emit FORK event with child PID.
     Skip if this was a thread-creating clone (CLONE_THREAD),
     since we emit THREAD_CREATE via track_pre_thread_ll_create instead. */
  if (is_fork_syscall(syscallno) && !sr_isError(res) && sr_Res(res) > 0) {
    Bool is_thread = False;
#if defined(VGO_linux)
    if (syscallno == __NR_clone && nArgs > 0)
      is_thread = (args[0] & VKI_CLONE_THREAD) != 0;
#  if defined(__NR_clone3)
    if (syscallno == __NR_clone3 && nArgs > 0) {
      /* clone3 first arg is pointer to struct clone_args;
         flags is the first field (ULong / __u64). */
      ULong flags = *(ULong*)(Addr)args[0];
      is_thread = (flags & VKI_CLONE_THREAD) != 0;
    }
#  endif
#endif
    if (!is_thread) {
      Int child_pid = (Int)sr_Res(res);
      TG_(trace_emit_fork)(tid, child_pid);
    }
  }

  /* Handle systime collection if enabled */
  if (TG_(clo).collect_systime != systime_no && TG_(current_state).bbcc) {
    Int o;
    struct vki_timespec ts_now;
    struct vki_timespec ts_cpunow;
    ULong diff;

    collect_time(&ts_now,
                 TG_(clo).collect_systime == systime_nsec ? &ts_cpunow : NULL);

    diff = vki_timespec_diff (ts_now, syscalltime[tid]);

    /* offset o is for "SysCount", o+1 for "SysTime",
       o+2 is (optionally) "SysCpuTime".  */
    o = fullOffset(EG_SYS);
    TG_ASSERT(o>=0);
    TG_DEBUG(0,"   Time (Off %d) for Syscall %u: %llu\n", o, syscallno,
              diff);

    if (!TG_(current_state).bbcc->skipped)
      TG_(init_cost_lz)(TG_(sets).full,
			&(TG_(current_state).bbcc->skipped));
    TG_(current_state).cost[o] ++;
    TG_(current_state).cost[o+1] += diff;
    TG_(current_state).bbcc->skipped[o] ++;
    TG_(current_state).bbcc->skipped[o+1] += diff;
    if (TG_(clo).collect_systime == systime_nsec) {
      diff = vki_timespec_diff (ts_cpunow, syscallcputime[tid]);
      TG_DEBUG(0,"   SysCpuTime (Off %d) for Syscall %u: %llu\n", o+2, syscallno,
                diff);
      TG_(current_state).cost[o+2] += diff;
      TG_(current_state).bbcc->skipped[o+2] += diff;
    }
  }
}

static UInt ULong_width(ULong n)
{
   UInt w = 0;
   while (n > 0) {
      n = n / 10;
      w++;
   }
   if (w == 0) w = 1;
   return w + (w-1)/3;   // add space for commas
}

static
void branchsim_printstat(int l1, int l2, int l3)
{
    static HChar fmt[128];    // large enough
    FullCost total;
    ULong Bc_total_b, Bc_total_mp, Bi_total_b, Bi_total_mp;
    ULong B_total_b, B_total_mp;

    total = TG_(total_cost);
    Bc_total_b  = total[ fullOffset(EG_BC)   ];
    Bc_total_mp = total[ fullOffset(EG_BC)+1 ];
    Bi_total_b  = total[ fullOffset(EG_BI)   ];
    Bi_total_mp = total[ fullOffset(EG_BI)+1 ];

    /* Make format string, getting width right for numbers */
    VG_(sprintf)(fmt, "%%s %%,%dllu  (%%,%dllu cond + %%,%dllu ind)\n",
                 l1, l2, l3);

    if (0 == Bc_total_b)  Bc_total_b = 1;
    if (0 == Bi_total_b)  Bi_total_b = 1;
    B_total_b  = Bc_total_b  + Bi_total_b;
    B_total_mp = Bc_total_mp + Bi_total_mp;

    VG_(umsg)("\n");
    VG_(umsg)(fmt, "Branches:     ",
              B_total_b, Bc_total_b, Bi_total_b);

    VG_(umsg)(fmt, "Mispredicts:  ",
              B_total_mp, Bc_total_mp, Bi_total_mp);

    VG_(umsg)("Mispred rate:  %*.1f%% (%*.1f%%     + %*.1f%%   )\n",
              l1, B_total_mp  * 100.0 / B_total_b,
              l2, Bc_total_mp * 100.0 / Bc_total_b,
              l3, Bi_total_mp * 100.0 / Bi_total_b);
}

static
void tg_print_stats(void)
{
   int BB_lookups =
     TG_(stat).full_debug_BBs +
     TG_(stat).fn_name_debug_BBs +
     TG_(stat).file_line_debug_BBs +
     TG_(stat).no_debug_BBs;

   /* Hash table stats */
   VG_(message)(Vg_DebugMsg, "Distinct objects: %d\n",
		TG_(stat).distinct_objs);
   VG_(message)(Vg_DebugMsg, "Distinct files:   %d\n",
		TG_(stat).distinct_files);
   VG_(message)(Vg_DebugMsg, "Distinct fns:     %d\n",
		TG_(stat).distinct_fns);
   VG_(message)(Vg_DebugMsg, "Distinct contexts:%d\n",
		TG_(stat).distinct_contexts);
   VG_(message)(Vg_DebugMsg, "Distinct BBs:     %d\n",
		TG_(stat).distinct_bbs);
   VG_(message)(Vg_DebugMsg, "Cost entries:     %u (Chunks %u)\n",
		TG_(costarray_entries), TG_(costarray_chunks));
   VG_(message)(Vg_DebugMsg, "Distinct BBCCs:   %d\n",
		TG_(stat).distinct_bbccs);
   VG_(message)(Vg_DebugMsg, "Distinct JCCs:    %d\n",
		TG_(stat).distinct_jccs);
   VG_(message)(Vg_DebugMsg, "Distinct skips:   %d\n",
		TG_(stat).distinct_skips);
   VG_(message)(Vg_DebugMsg, "BB lookups:       %d\n",
		BB_lookups);
   if (BB_lookups>0) {
      VG_(message)(Vg_DebugMsg, "With full      debug info:%3d%% (%d)\n",
		   TG_(stat).full_debug_BBs    * 100 / BB_lookups,
		   TG_(stat).full_debug_BBs);
      VG_(message)(Vg_DebugMsg, "With file/line debug info:%3d%% (%d)\n",
		   TG_(stat).file_line_debug_BBs * 100 / BB_lookups,
		   TG_(stat).file_line_debug_BBs);
      VG_(message)(Vg_DebugMsg, "With fn name   debug info:%3d%% (%d)\n",
		   TG_(stat).fn_name_debug_BBs * 100 / BB_lookups,
		   TG_(stat).fn_name_debug_BBs);
      VG_(message)(Vg_DebugMsg, "With no        debug info:%3d%% (%d)\n",
		   TG_(stat).no_debug_BBs      * 100 / BB_lookups,
		   TG_(stat).no_debug_BBs);
   }
   VG_(message)(Vg_DebugMsg, "BBCC Clones:       %d\n",
		TG_(stat).bbcc_clones);
   VG_(message)(Vg_DebugMsg, "BBs Retranslated:  %d\n",
		TG_(stat).bb_retranslations);
   VG_(message)(Vg_DebugMsg, "Distinct instrs:   %d\n",
		TG_(stat).distinct_instrs);

   VG_(message)(Vg_DebugMsg, "LRU Contxt Misses: %d\n",
		TG_(stat).cxt_lru_misses);
   VG_(message)(Vg_DebugMsg, "LRU BBCC Misses:   %d\n",
		TG_(stat).bbcc_lru_misses);
   VG_(message)(Vg_DebugMsg, "LRU JCC Misses:    %d\n",
		TG_(stat).jcc_lru_misses);
   VG_(message)(Vg_DebugMsg, "BBs Executed:      %llu\n",
		TG_(stat).bb_executions);
   VG_(message)(Vg_DebugMsg, "Calls:             %llu\n",
		TG_(stat).call_counter);
   VG_(message)(Vg_DebugMsg, "CondJMP followed:  %llu\n",
		TG_(stat).jcnd_counter);
   VG_(message)(Vg_DebugMsg, "Boring JMPs:       %llu\n",
		TG_(stat).jump_counter);
   VG_(message)(Vg_DebugMsg, "Recursive calls:   %llu\n",
		TG_(stat).rec_call_counter);
   VG_(message)(Vg_DebugMsg, "Returns:           %llu\n",
		TG_(stat).ret_counter);
}


static
void finish(void)
{
  HChar fmt[128];    // large enough
  Int l1, l2, l3;
  FullCost total;

  TG_DEBUG(0, "finish()\n");

  (*TG_(cachesim).finish)();

  /* pop all remaining items from CallStack for correct sum
   */
  TG_(forall_threads)(unwind_thread);

  TG_(compute_total_cost)();

  /* Close CSV trace output */
  TG_(trace_close_output)();

  if (VG_(clo_verbosity) == 0) return;

  if (VG_(clo_stats)) {
    VG_(message)(Vg_DebugMsg, "\n");
    tg_print_stats();
    VG_(message)(Vg_DebugMsg, "\n");
  }

  HChar *evmap = TG_(eventmapping_as_string)(TG_(dumpmap));
  VG_(message)(Vg_UserMsg, "Events    : %s\n", evmap);
  VG_(free)(evmap);
  HChar *mcost = TG_(mappingcost_as_string)(TG_(dumpmap), TG_(total_cost));
  VG_(message)(Vg_UserMsg, "Collected : %s\n", mcost);
  VG_(free)(mcost);
  VG_(message)(Vg_UserMsg, "\n");

  /* determine value widths for statistics */
  total = TG_(total_cost);
  l1 = ULong_width( total[fullOffset(EG_IR)] );
  l2 = l3 = 0;
  if (TG_(clo).simulate_cache) {
      l2 = ULong_width( total[fullOffset(EG_DR)] );
      l3 = ULong_width( total[fullOffset(EG_DW)] );
  }
  if (TG_(clo).simulate_branch) {
      int l2b = ULong_width( total[fullOffset(EG_BC)] );
      int l3b = ULong_width( total[fullOffset(EG_BI)] );
      if (l2b > l2) l2 = l2b;
      if (l3b > l3) l3 = l3b;
  }

  /* Make format string, getting width right for numbers */
  VG_(sprintf)(fmt, "%%s %%,%dllu\n", l1);

  /* Always print this */
  VG_(umsg)(fmt, "I   refs:     ", total[fullOffset(EG_IR)] );

  if (TG_(clo).simulate_cache)
      (*TG_(cachesim).printstat)(l1, l2, l3);

  if (TG_(clo).simulate_branch)
      branchsim_printstat(l1, l2, l3);

}


void TG_(fini)(Int exitcode)
{
  finish();
}


/*--------------------------------------------------------------------*/
/*--- Setup                                                        ---*/
/*--------------------------------------------------------------------*/

static void tg_start_client_code_callback ( ThreadId tid, ULong blocks_done )
{
   static ULong last_blocks_done = 0;

   if (0)
      VG_(printf)("%d R %llu\n", (Int)tid, blocks_done);

   /* throttle calls to TG_(run_thread) by number of BBs executed */
   if (blocks_done - last_blocks_done < 5000) return;
   last_blocks_done = blocks_done;

   TG_(run_thread)( tid );
}

/*
 * Called after fork() in the child process.
 * Reopens the trace file with the child's PID.
 */
static void tg_atfork_child(ThreadId tid)
{
   TG_(trace_reopen_child)();
}

static void tg_pre_thread_ll_create(ThreadId tid, ThreadId child)
{
    /* Skip Valgrind's internal scheduler thread (tid 0) creating the
       initial client thread -- that's not a user-visible thread creation. */
    if (tid == 0) return;
    TG_(trace_emit_thread_create)(tid, child);
}

static
void TG_(post_clo_init)(void)
{
   if (VG_(clo_vex_control).iropt_register_updates_default
       != VexRegUpdSpAtMemAccess) {
      TG_DEBUG(1, " Using user specified value for "
                "--vex-iropt-register-updates\n");
   } else {
      TG_DEBUG(1,
                " Using default --vex-iropt-register-updates="
                "sp-at-mem-access\n");
   }

   /* Always register syscall wrappers for fork/clone detection.
      Also handles systime collection if enabled. */
   VG_(needs_syscall_wrapper)(TG_(pre_syscall), TG_(post_syscall));

   if (TG_(clo).collect_systime != systime_no) {
      syscalltime = TG_MALLOC("cl.main.pci.1",
                               VG_N_THREADS * sizeof syscalltime[0]);
      for (UInt i = 0; i < VG_N_THREADS; ++i) {
         syscalltime[i].tv_sec = 0;
         syscalltime[i].tv_nsec = 0;
      }
      if (TG_(clo).collect_systime == systime_nsec) {
         syscallcputime = TG_MALLOC("cl.main.pci.2",
                                     VG_N_THREADS * sizeof syscallcputime[0]);
         for (UInt i = 0; i < VG_N_THREADS; ++i) {
            syscallcputime[i].tv_sec = 0;
            syscallcputime[i].tv_nsec = 0;
         }
      }
   }

   if (VG_(clo_px_file_backed) != VexRegUpdSpAtMemAccess) {
      TG_DEBUG(1, " Using user specified value for "
                "--px-file-backed\n");
   } else {
      TG_DEBUG(1,
                " Using default --px-file-backed="
                "sp-at-mem-access\n");
   }

   if (VG_(clo_vex_control).iropt_unroll_thresh != 0) {
      VG_(message)(Vg_UserMsg,
                   "tracegrind only works with --vex-iropt-unroll-thresh=0\n"
                   "=> resetting it back to 0\n");
      VG_(clo_vex_control).iropt_unroll_thresh = 0;   // cannot be overridden.
   }
   if (VG_(clo_vex_control).guest_chase) {
      VG_(message)(Vg_UserMsg,
                   "tracegrind only works with --vex-guest-chase=no\n"
                   "=> resetting it back to 'no'\n");
      VG_(clo_vex_control).guest_chase = False; // cannot be overridden.
   }

   TG_DEBUG(1, "  dump threads: %s\n", TG_(clo).separate_threads ? "Yes":"No");
   TG_DEBUG(1, "  call sep. : %d\n", TG_(clo).separate_callers);
   TG_DEBUG(1, "  rec. sep. : %d\n", TG_(clo).separate_recursions);

   (*TG_(cachesim).post_clo_init)();

   TG_(init_eventsets)();
   TG_(init_statistics)(& TG_(stat));
   TG_(init_cost_lz)( TG_(sets).full, &TG_(total_cost) );

   /* initialize hash tables */
   TG_(init_obj_table)();
   TG_(init_cxt_table)();
   TG_(init_bb_hash)();

   TG_(init_threads)();
   TG_(run_thread)(1);

   TG_(instrument_state) = TG_(clo).instrument_atstart;

   /* Open trace output file */
   TG_(trace_open_output)();

   /* Register fork handler to emit FORK events */
   VG_(atfork)(NULL, NULL, tg_atfork_child);

   if (VG_(clo_verbosity) > 0) {
      VG_(message)(Vg_UserMsg,
                   "Streaming trace output to tracegrind.out.%d\n",
                   VG_(getpid)());
   }
}

static
void TG_(pre_clo_init)(void)
{
    VG_(details_name)            ("Tracegrind");
    VG_(details_version)         (NULL);
    VG_(details_description)     ("a streaming trace cache profiler");
    VG_(details_copyright_author)("Copyright (C) 2026, and GNU GPL'd, "
				  "by CodSpeed Technology SAS. "
				  "Based on Callgrind by Josef Weidendorfer et al.");
    VG_(details_bug_reports_to)  (VG_BUGS_TO);
    VG_(details_avg_translation_sizeB) ( 500 );

    VG_(clo_vex_control).iropt_register_updates_default
       = VG_(clo_px_file_backed)
       = VexRegUpdSpAtMemAccess; // overridable by the user.

    VG_(clo_vex_control).iropt_unroll_thresh = 0;   // cannot be overridden.
    VG_(clo_vex_control).guest_chase = False;       // cannot be overridden.

    VG_(basic_tool_funcs)        (TG_(post_clo_init),
                                  TG_(instrument),
                                  TG_(fini));

    VG_(needs_superblock_discards)(tg_discard_superblock_info);


    VG_(needs_command_line_options)(TG_(process_cmd_line_option),
				    TG_(print_usage),
				    TG_(print_debug_usage));

    VG_(needs_client_requests)(TG_(handle_client_request));
    VG_(needs_print_stats)    (tg_print_stats);

    VG_(track_start_client_code)  ( & tg_start_client_code_callback );
    VG_(track_pre_deliver_signal) ( & TG_(pre_signal) );
    VG_(track_post_deliver_signal)( & TG_(post_signal) );
    VG_(track_pre_thread_ll_create)( & tg_pre_thread_ll_create );

    TG_(set_clo_defaults)();

}

VG_DETERMINE_INTERFACE_VERSION(TG_(pre_clo_init))

/*--------------------------------------------------------------------*/
/*--- end                                                   main.c ---*/
/*--------------------------------------------------------------------*/
