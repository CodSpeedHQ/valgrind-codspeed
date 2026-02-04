/*--------------------------------------------------------------------*/
/*--- Tracegrind                                                    ---*/
/*---                                               ct_callstack.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Tracegrind, a Valgrind tool for call tracing.

   Copyright (C) 2002-2017, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)

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

#include "global.h"

/*------------------------------------------------------------*/
/*--- Call stack, operations                               ---*/
/*------------------------------------------------------------*/

/* Stack of current thread. Gets initialized when switching to 1st thread.
 *
 * The artificial call stack is an array of call_entry's, representing
 * stack frames of the executing program. 
 * Array call_stack and call_stack_esp have same size and grow on demand.
 * Array call_stack_esp holds SPs of corresponding stack frames.
 *
 */

#define N_CALL_STACK_INITIAL_ENTRIES 500

call_stack TG_(current_call_stack);

void TG_(init_call_stack)(call_stack* s)
{
  Int i;

  TG_ASSERT(s != 0);

  s->size = N_CALL_STACK_INITIAL_ENTRIES;   
  s->entry = (call_entry*) TG_MALLOC("cl.callstack.ics.1",
                                      s->size * sizeof(call_entry));
  s->sp = 0;
  s->entry[0].cxt = 0; /* for assertion in push_cxt() */

  for(i=0; i<s->size; i++) s->entry[i].enter_cost = 0;
}

call_entry* TG_(get_call_entry)(Int sp)
{
  TG_ASSERT(sp <= TG_(current_call_stack).sp);
  return &(TG_(current_call_stack).entry[sp]);
}

void TG_(copy_current_call_stack)(call_stack* dst)
{
  TG_ASSERT(dst != 0);

  dst->size  = TG_(current_call_stack).size;
  dst->entry = TG_(current_call_stack).entry;
  dst->sp    = TG_(current_call_stack).sp;
}

void TG_(set_current_call_stack)(call_stack* s)
{
  TG_ASSERT(s != 0);

  TG_(current_call_stack).size  = s->size;
  TG_(current_call_stack).entry = s->entry;
  TG_(current_call_stack).sp    = s->sp;
}


static __inline__
void ensure_stack_size(Int i)
{
  Int oldsize;
  call_stack *cs = &TG_(current_call_stack);

  if (i < cs->size) return;

  oldsize = cs->size;
  cs->size *= 2;
  while (i > cs->size) cs->size *= 2;

  cs->entry = (call_entry*) VG_(realloc)("cl.callstack.ess.1",
                                         cs->entry,
					 cs->size * sizeof(call_entry));

  for(i=oldsize; i<cs->size; i++)
    cs->entry[i].enter_cost = 0;

  TG_(stat).call_stack_resizes++;
 
  TG_DEBUGIF(2)
    VG_(printf)("        call stack enlarged to %u entries\n",
		TG_(current_call_stack).size);
}



/* Called when function entered nonrecursive */
static void function_entered(fn_node* fn)
{
  TG_ASSERT(fn != 0);

#if TG_ENABLE_DEBUG
  if (fn->verbosity >=0) {
    Int old = TG_(clo).verbose;
    TG_(clo).verbose = fn->verbosity;
    fn->verbosity = old;
    VG_(message)(Vg_DebugMsg, 
		 "Entering %s: Verbosity set to %d\n",
		 fn->name, TG_(clo).verbose);
  }
#endif		
	    
  if (fn->dump_before) {
    HChar trigger[VG_(strlen)(fn->name) + 20];
    VG_(sprintf)(trigger, "--dump-before=%s", fn->name);
    TG_(dump_profile)(trigger, True);
  }
  else if (fn->zero_before) {
    TG_(zero_all_cost)(True);
  }

  if (fn->toggle_collect) {
    TG_(current_state).collect = !TG_(current_state).collect;
    TG_DEBUG(2,"   entering %s: toggled collection state to %s\n",
	     fn->name,
	     TG_(current_state).collect ? "ON" : "OFF");
  }
}	

/* Called when function left (no recursive level active) */
static void function_left(fn_node* fn)
{
  TG_ASSERT(fn != 0);

  if (fn->dump_after) {
    HChar trigger[VG_(strlen)(fn->name) + 20];
    VG_(sprintf)(trigger, "--dump-after=%s", fn->name);
    TG_(dump_profile)(trigger, True);
  }
  if (fn->toggle_collect) {
    TG_(current_state).collect = !TG_(current_state).collect;
    TG_DEBUG(2,"   leaving %s: toggled collection state to %s\n",
	     fn->name,
	     TG_(current_state).collect ? "ON" : "OFF");
  }

#if TG_ENABLE_DEBUG
  if (fn->verbosity >=0) {
    Int old = TG_(clo).verbose;
    TG_(clo).verbose = fn->verbosity;
    fn->verbosity = old;
    VG_(message)(Vg_DebugMsg, 
		 "Leaving %s: Verbosity set back to %d\n",
		 fn->name, TG_(clo).verbose);
  }
#endif		
}


/* Push call on call stack.
 *
 * Increment the usage count for the function called.
 * A jump from <from> to <to>, with <sp>.
 * If <skip> is true, this is a call to a function to be skipped;
 * for this, we set jcc = 0.
 */
void TG_(push_call_stack)(BBCC* from, UInt jmp, BBCC* to, Addr sp, Bool skip)
{
    jCC* jcc;
    UInt* pdepth;
    call_entry* current_entry;
    Addr ret_addr;

    /* Ensure a call stack of size <current_sp>+1.
     * The +1 is needed as push_cxt will store the
     * context at [current_sp]
     */
    ensure_stack_size(TG_(current_call_stack).sp +1);
    current_entry = &(TG_(current_call_stack).entry[TG_(current_call_stack).sp]);

    if (skip) {
	jcc = 0;
    }
    else {
	fn_node* to_fn = to->cxt->fn[0];

	if (TG_(current_state).nonskipped) {
	    /* this is a jmp from skipped to nonskipped */
	    TG_ASSERT(TG_(current_state).nonskipped == from);
	}

	/* As push_cxt() has to be called before push_call_stack if not
	 * skipping, the old context should already be saved on the stack */
	TG_ASSERT(current_entry->cxt != 0);
	TG_(copy_cost_lz)( TG_(sets).full, &(current_entry->enter_cost),
			   TG_(current_state).cost );

	jcc = TG_(get_jcc)(from, jmp, to);
	TG_ASSERT(jcc != 0);

	pdepth = TG_(get_fn_entry)(to_fn->number);
	if (TG_(clo).skip_direct_recursion) {
	    /* only increment depth if another function is called */
	  if (jcc->from->cxt->fn[0] != to_fn) (*pdepth)++;
	}
	else (*pdepth)++;

	if (*pdepth>1)
	  TG_(stat).rec_call_counter++;
	
	jcc->call_counter++;
	TG_(stat).call_counter++;

	if (*pdepth == 1) function_entered(to_fn);
    }

    /* return address is only is useful with a real call;
     * used to detect RET w/o CALL */
    if (from->bb->jmp[jmp].jmpkind == jk_Call) {
      UInt instr = from->bb->jmp[jmp].instr;
      ret_addr = bb_addr(from->bb) +
	from->bb->instr[instr].instr_offset +
	from->bb->instr[instr].instr_size;
    }
    else
      ret_addr = 0;

    /* put jcc on call stack */
    current_entry->jcc = jcc;
    current_entry->sp = sp;
    current_entry->ret_addr = ret_addr;
    current_entry->nonskipped = TG_(current_state).nonskipped;

    TG_(current_call_stack).sp++;

    /* To allow for above assertion we set context of next frame to 0 */
    TG_ASSERT(TG_(current_call_stack).sp < TG_(current_call_stack).size);
    current_entry++;
    current_entry->cxt = 0;

    if (!skip)
	TG_(current_state).nonskipped = 0;
    else if (!TG_(current_state).nonskipped) {
	/* a call from nonskipped to skipped */
	TG_(current_state).nonskipped = from;
	if (!TG_(current_state).nonskipped->skipped) {
	  TG_(init_cost_lz)( TG_(sets).full,
			     &TG_(current_state).nonskipped->skipped);
	  TG_(stat).distinct_skips++;
	}
    }

#if TG_ENABLE_DEBUG
    TG_DEBUGIF(0) {
	if (TG_(clo).verbose<2) {
	  if (jcc && jcc->to && jcc->to->bb) {
	    const HChar spaces[][41] = {
                                  "   .   .   .   .   .   .   .   .   .   .",
				  "  .   .   .   .   .   .   .   .   .   . ",
				  " .   .   .   .   .   .   .   .   .   .  ",
				  ".   .   .   .   .   .   .   .   .   .   " };

	    int s = TG_(current_call_stack).sp;
	    UInt* pars = (UInt*) sp;

	    BB* bb = jcc->to->bb;
	    if (s>40) s=40;
	    VG_(printf)("%s> %s(0x%x, 0x%x, ...) [%s / %#lx]\n", spaces[s%4]+40-s, bb->fn->name,
                        pars ? pars[1]:0,
			pars ? pars[2]:0,
			bb->obj->name + bb->obj->last_slash_pos,
			(UWord)bb->offset);
	  }
	}
	else if (TG_(clo).verbose<4) {
	    VG_(printf)("+ %2d ", TG_(current_call_stack).sp);
	    TG_(print_short_jcc)(jcc);
	    VG_(printf)(", SP %#lx, RA %#lx\n", sp, ret_addr);
	}
	else {
	    VG_(printf)("  Pushed ");
	    TG_(print_stackentry)(3, TG_(current_call_stack).sp-1);
	}
    }
#endif

}


/* Pop call stack and update inclusive sums.
 * Returns modified fcc.
 *
 * If the JCC becomes inactive, call entries are freed if possible
 */
void TG_(pop_call_stack)(void)
{
    jCC* jcc;
    Int depth = 0;
    call_entry* lower_entry;

    if (TG_(current_state).sig >0) {
	/* Check if we leave a signal handler; this can happen when
	 * calling longjmp() in the handler */
	TG_(run_post_signal_on_call_stack_bottom)();
    }

    lower_entry =
	&(TG_(current_call_stack).entry[TG_(current_call_stack).sp-1]);

    TG_DEBUG(4,"+ pop_call_stack: frame %d, jcc %p\n", 
		TG_(current_call_stack).sp, lower_entry->jcc);

    /* jCC item not any more on real stack: pop */
    jcc = lower_entry->jcc;
    TG_(current_state).nonskipped = lower_entry->nonskipped;

    if (jcc) {
	fn_node* to_fn  = jcc->to->cxt->fn[0];
	UInt* pdepth =  TG_(get_fn_entry)(to_fn->number);
	if (TG_(clo).skip_direct_recursion) {
	    /* only decrement depth if another function was called */
	  if (jcc->from->cxt->fn[0] != to_fn) (*pdepth)--;
	}
	else (*pdepth)--;
	depth = *pdepth;

	/* add cost difference to sum */
	if ( TG_(add_diff_cost_lz)( TG_(sets).full, &(jcc->cost),
				    lower_entry->enter_cost,
				    TG_(current_state).cost) ) {
	    
	  /* only count this call if it attributed some cost.
	   * the ret_counter is used to check if a BBCC dump is needed.
	   */
	  jcc->from->ret_counter++;
	}
	TG_(stat).ret_counter++;

	/* restore context */
	TG_(current_state).cxt  = lower_entry->cxt;
	TG_(current_fn_stack).top =
	  TG_(current_fn_stack).bottom + lower_entry->fn_sp;
	TG_ASSERT(TG_(current_state).cxt != 0);

	if (depth == 0) function_left(to_fn);
    }

    /* To allow for an assertion in push_call_stack() */
    lower_entry->cxt = 0;

    TG_(current_call_stack).sp--;

#if TG_ENABLE_DEBUG
    TG_DEBUGIF(1) {
	if (TG_(clo).verbose<4) {
	    if (jcc) {
		/* popped JCC target first */
		VG_(printf)("- %2d %#lx => ",
			    TG_(current_call_stack).sp,
			    bb_addr(jcc->to->bb));
		TG_(print_addr)(bb_jmpaddr(jcc->from->bb));
		VG_(printf)(", SP %#lx\n",
			    TG_(current_call_stack).entry[TG_(current_call_stack).sp].sp);
		TG_(print_cost)(10, TG_(sets).full, jcc->cost);
	    }
	    else
		VG_(printf)("- %2d [Skipped JCC], SP %#lx\n",
			    TG_(current_call_stack).sp,
			    TG_(current_call_stack).entry[TG_(current_call_stack).sp].sp);
	}
	else {
	    VG_(printf)("  Popped ");
	    TG_(print_stackentry)(7, TG_(current_call_stack).sp);
	    if (jcc) {
		VG_(printf)("       returned to ");
		TG_(print_addr_ln)(bb_jmpaddr(jcc->from->bb));
	    }
	}
    }
#endif

}


/* Unwind enough CallStack items to sync with current stack pointer.
 * Returns the number of stack frames unwinded.
 */
Int TG_(unwind_call_stack)(Addr sp, Int minpops)
{
    Int csp;
    Int unwind_count = 0;
    TG_DEBUG(4,"+ unwind_call_stack(sp %#lx, minpops %d): frame %d\n",
	      sp, minpops, TG_(current_call_stack).sp);

    /* We pop old stack frames.
     * For a call, be p the stack address with return address.
     *  - call_stack_esp[] has SP after the CALL: p-4
     *  - current sp is after a RET: >= p
     */
    
    while( (csp=TG_(current_call_stack).sp) >0) {
	call_entry* top_ce = &(TG_(current_call_stack).entry[csp-1]);

	if ((top_ce->sp < sp) ||
	    ((top_ce->sp == sp) && minpops>0)) {

	    minpops--;
	    unwind_count++;
	    TG_(pop_call_stack)();
	    csp=TG_(current_call_stack).sp;
	    continue;
	}
	break;
    }

    TG_DEBUG(4,"- unwind_call_stack\n");
    return unwind_count;
}
