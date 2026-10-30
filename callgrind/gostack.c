/*--------------------------------------------------------------------*/
/*--- Callgrind                                                    ---*/
/*---                                                    gostack.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Callgrind, a Valgrind tool for call tracing.

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

/* Go goroutine stacks.
 *
 * Callgrind keeps one shadow call stack per thread and pops frames by
 * comparing stack pointers. The Go runtime runs many goroutines on one
 * thread, each on its own heap-allocated stack, and moves between them
 * without call/return pairs:
 *
 *  - runtime.gogo jumps into another goroutine (or a new one).
 *  - runtime.mcall leaves a goroutine for the thread's g0 stack, where the
 *    scheduler picks the next goroutine.
 *  - runtime.systemstack and runtime.morestack run a function on g0 and
 *    then come back to the calling goroutine.
 *
 * Comparing SPs across these stacks pops unrelated frames, and frames of one
 * goroutine end up under another one. So for Go programs each goroutine gets
 * its own shadow call stack, keyed by its g pointer (held in a fixed register
 * by the Go ABI). gogo and mcall swap shadow stacks. systemstack and
 * morestack keep the goroutine's shadow stack, so work done on g0 for the
 * goroutine stays under its caller, and tag the frames that run on g0 so the
 * SP comparison never pops a goroutine frame from g0, or the other way round.
 *
 * Only the functions above are looked at. They are found by name, so this
 * costs nothing for programs without a Go runtime.
 *
 * Only amd64 is supported for now. Other architectures keep the plain
 * SP-based shadow stack.
 */

#include "global.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_guest.h"

#if defined(VGA_amd64)
/* ABIInternal keeps the current g in R14 */
#  define GO_G_REG guest_R14
/* Bytes a call pushes: the return address */
#  define GO_CALL_PUSH 8
#endif

/* Tag of frames that run on g0 for a goroutine (systemstack, morestack).
 * Never a valid g pointer. */
#define GO_TAG_SYSTEM ((UWord)1)

/* Initial entries of a new goroutine's shadow stacks: goroutines are many and
 * mostly shallow, the stacks grow on demand. */
#define GO_STACK_INITIAL_ENTRIES 16

Bool  CLG_(go_runtime) = False;
/* g whose shadow stack is the current call stack, 0 if not known yet */
UWord CLG_(go_owner) = 0;
/* Tag of the stack the running frame is on */
UWord CLG_(go_cur) = 0;
/* Function that jumps back to its own entry after morestack, see resume() */
fn_node* CLG_(go_restart) = 0;

/* A suspended goroutine's shadow stack. The first two fields are the
 * VgHashNode header. */
typedef struct _go_stack go_stack;
struct _go_stack {
  go_stack* next;
  UWord key;          /* g pointer */

  call_stack calls;
  fn_stack fns;
  Context* cxt;
  BBCC* bbcc;
  BBCC* nonskipped;
  Int jmps_passed;
  FullCost suspend_cost; /* cost counters when the goroutine was left */
  fn_node* root;         /* function marked active again on resume */
};

#if defined(GO_G_REG)
typedef struct {
  const HChar* name;
  UInt kind;
} go_switch_fn;

/* Names with and without the ".abi0" suffix Go 1.17 added to assembly
 * functions. "gogo" is the static target runtime.gogo.abi0 jumps to. */
static const go_switch_fn go_switch_fns[] = {
  { "runtime.gogo.abi0",        GO_SWITCH_GOGO },
  { "runtime.gogo",             GO_SWITCH_GOGO },
  { "gogo",                     GO_SWITCH_GOGO },
  { "runtime.mcall",            GO_SWITCH_MCALL },
  { "runtime.systemstack.abi0", GO_SWITCH_NESTED },
  { "runtime.systemstack",      GO_SWITCH_NESTED },
  { "runtime.morestack.abi0",   GO_SWITCH_MORESTACK },
  { "runtime.morestack",        GO_SWITCH_MORESTACK },
  /* vDSO calls run on g0 */
  { "runtime.nanotime1.abi0",   GO_SWITCH_NESTED },
  { "runtime.nanotime1",        GO_SWITCH_NESTED },
  { "runtime.walltime.abi0",    GO_SWITCH_NESTED },
  { "runtime.walltime",         GO_SWITCH_NESTED },
  { "runtime.walltime1.abi0",   GO_SWITCH_NESTED },
  { "runtime.walltime1",        GO_SWITCH_NESTED },
  { "runtime.vgetrandom1",      GO_SWITCH_NESTED },
  /* cgo calls run on g0 */
  { "runtime.asmcgocall.abi0",  GO_SWITCH_NESTED },
  { "runtime.asmcgocall",       GO_SWITCH_NESTED },
};
#endif

void CLG_(go_classify_fn)(fn_node* fn)
{
#if defined(GO_G_REG)
  UInt i;

  for (i = 0; i < sizeof(go_switch_fns) / sizeof(go_switch_fns[0]); i++) {
    if (VG_(strcmp)(fn->name, go_switch_fns[i].name) != 0) continue;

    /* A plain "gogo" only counts once the Go runtime was seen; its only
     * caller, runtime.gogo.abi0, always runs first. */
    if (!CLG_(go_runtime) && fn->name[0] == 'g') return;

    fn->go_switch = go_switch_fns[i].kind;
    CLG_(go_runtime) = True;
    return;
  }
#endif
}

static fn_node* callee_fn(call_entry* ce)
{
  return ce->jcc ? ce->jcc->to->cxt->fn[0] : 0;
}

#if defined(GO_G_REG)

static UWord read_g(void)
{
  UWord g;
  VG_(get_shadow_regs_area)(CLG_(current_tid), (UChar*)&g, 0,
                            offsetof(VexGuestArchState, GO_G_REG),
                            sizeof(g));
  return g;
}

/* Tag of the frame that runs after the top frame returns */
static UWord tag_below_top(void)
{
  Int csp = CLG_(current_call_stack).sp;
  UWord tag = csp > 0 ? CLG_(current_call_stack).entry[csp-1].go_tag : 0;
  return tag ? tag : CLG_(go_owner);
}

/* Add <dir> to the active counts of the functions on the current call stack,
 * the inverse of what push/pop_call_stack do: a suspended goroutine's frames
 * must not count as recursion of the running one. */
static void adjust_active(Int dir)
{
  Int i;

  for (i = 0; i < CLG_(current_call_stack).sp; i++) {
    call_entry* ce = &CLG_(current_call_stack).entry[i];
    fn_node* to_fn = callee_fn(ce);
    UInt* pdepth;

    if (!to_fn) continue;
    if (CLG_(clo).skip_direct_recursion &&
        ce->jcc->from->cxt->fn[0] == to_fn) continue;

    pdepth = CLG_(get_fn_entry)(to_fn->number);
    if (dir < 0 && *pdepth == 0) continue;
    *pdepth += dir;
  }
}

/* The function a call stack started in has no call entry, but callgrind marks
 * it active (see the recursion level in setup_bbcc). Clear that mark when the
 * stack is suspended; returns the function to mark again on resume. */
static fn_node* uncount_root(void)
{
  Context* cxt = CLG_(current_call_stack).sp > 0
                 ? CLG_(current_call_stack).entry[0].cxt
                 : CLG_(current_state).cxt;
  fn_node* fn = cxt ? cxt->fn[0] : 0;
  UInt* pdepth;

  if (!fn) return 0;
  pdepth = CLG_(get_fn_entry)(fn->number);
  if (*pdepth == 0) return 0;
  (*pdepth)--;
  return fn;
}

/* Unused call stack arrays, kept in go_stack nodes linked by <next>. Cost
 * arrays hanging off call entries come from a pool and cannot be freed, so
 * the arrays of finished goroutines are reused instead of freed. */
static go_stack* spare_stacks = 0;

/* Suspended goroutines by g pointer. Global: goroutines move between
 * threads. */
static VgHashTable* go_stacks = 0;

/* Move the current call stack and context into a new go_stack. */
static go_stack* stack_out(UWord key)
{
  go_stack* s = (go_stack*) CLG_MALLOC("cl.gostack.so.1", sizeof(go_stack));

  adjust_active(-1);
  s->root = uncount_root();

  s->key = key;
  CLG_(copy_current_call_stack)(&s->calls);
  CLG_(copy_current_fn_stack)(&s->fns);
  s->cxt         = CLG_(current_state).cxt;
  s->bbcc        = CLG_(current_state).bbcc;
  s->nonskipped  = CLG_(current_state).nonskipped;
  s->jmps_passed = CLG_(current_state).jmps_passed;
  s->suspend_cost = (FullCost) CLG_MALLOC("cl.gostack.so.2",
                                          CLG_(sets).full->size * sizeof(ULong));
  CLG_(copy_cost)(CLG_(sets).full, s->suspend_cost, CLG_(current_state).cost);
  return s;
}

/* Make <s>'s call stack and context current and free <s>. */
static void stack_in(go_stack* s)
{
  Int i;

  CLG_(set_current_call_stack)(&s->calls);
  CLG_(set_current_fn_stack)(&s->fns);
  CLG_(current_state).cxt         = s->cxt;
  CLG_(current_state).bbcc        = s->bbcc;
  CLG_(current_state).nonskipped  = s->nonskipped;
  CLG_(current_state).jmps_passed = s->jmps_passed;

  /* The time the goroutine was suspended is not part of its frames'
   * inclusive cost. Not CLG_(add_diff_cost): it moves <old> to <new>. */
  for (i = 0; i < CLG_(current_call_stack).sp; i++) {
    call_entry* ce = &CLG_(current_call_stack).entry[i];
    Int e;
    if (!ce->jcc || !ce->enter_cost) continue;
    for (e = 0; e < CLG_(sets).full->size; e++)
      ce->enter_cost[e] += CLG_(current_state).cost[e] - s->suspend_cost[e];
  }

  adjust_active(1);
  if (s->root) (*CLG_(get_fn_entry)(s->root->number))++;

  VG_(free)(s->suspend_cost);
  VG_(free)(s);
}

/* Install an empty call stack for a goroutine run for the first time. */
static void stack_fresh(void)
{
  go_stack* spare = spare_stacks;
  call_stack cs;
  fn_stack fs;
  UInt i;

  if (spare) {
    spare_stacks = spare->next;
    cs = spare->calls;
    fs = spare->fns;
    VG_(free)(spare);
  }
  else {
    cs.size  = GO_STACK_INITIAL_ENTRIES;
    cs.entry = (call_entry*) CLG_MALLOC("cl.gostack.sf.1",
                                        cs.size * sizeof(call_entry));
    for (i = 0; i < cs.size; i++) cs.entry[i].enter_cost = 0;

    fs.size   = GO_STACK_INITIAL_ENTRIES;
    fs.bottom = (fn_node**) CLG_MALLOC("cl.gostack.sf.2",
                                       fs.size * sizeof(fn_node*));
  }
  cs.sp = 0;
  cs.entry[0].cxt = 0;
  fs.top = fs.bottom;
  fs.bottom[0] = 0;

  CLG_(set_current_call_stack)(&cs);
  CLG_(set_current_fn_stack)(&fs);
  CLG_(current_state).cxt         = 0;
  CLG_(current_state).bbcc        = 0;
  CLG_(current_state).nonskipped  = 0;
  CLG_(current_state).jmps_passed = 0;
}

/* Keep the arrays of <s> for reuse and free the rest of it, without popping
 * its frames: their goroutine is gone. */
static void stack_free(go_stack* s)
{
  VG_(free)(s->suspend_cost);
  s->suspend_cost = 0;
  s->next = spare_stacks;
  spare_stacks = s;
}

/* Pop all frames of the current call stack and keep its arrays for reuse.
 * A call stack must be installed again afterwards. */
static void drop_current_stack(void)
{
  go_stack* s = (go_stack*) CLG_MALLOC("cl.gostack.dcs.1", sizeof(go_stack));

  while (CLG_(current_call_stack).sp > 0)
    CLG_(pop_call_stack)();
  uncount_root();

  CLG_(copy_current_call_stack)(&s->calls);
  CLG_(copy_current_fn_stack)(&s->fns);
  s->suspend_cost = 0;
  s->next = spare_stacks;
  spare_stacks = s;
}

/* A goroutine stops at a call into mcall or morestack and is resumed with
 * SP where that call left it. The Go runtime moves goroutine stacks when
 * they grow or shrink, so shift the shadow frames by the same distance. */
static void relocate(Addr sp)
{
  Int csp = CLG_(current_call_stack).sp;
  call_entry* top;
  fn_node* fn;
  Addr expected;
  Int i;

  if (csp == 0) return;
  top = &CLG_(current_call_stack).entry[csp-1];
  fn = callee_fn(top);
  if (!fn || fn->go_switch == GO_SWITCH_NONE) return;

  expected = top->sp + GO_CALL_PUSH;
  if (sp == expected) return;

  for (i = 0; i < csp; i++)
    CLG_(current_call_stack).entry[i].sp += sp - expected;
}

/* Continue the current goroutine at <sp>, where it stopped in mcall or
 * morestack. A function that ran out of stack calls morestack from its
 * prologue and, once the stack has grown, jumps back to its own entry. Mark
 * that function so the jump does not count as a call of itself. */
static void resume(Addr sp)
{
  static const HChar morestack_prefix[] = "runtime.morestack";
  fn_node* fn;
  Int i;

  relocate(sp);

  i = CLG_(current_call_stack).sp - 1;
  if (i < 0) return;
  fn = callee_fn(&CLG_(current_call_stack).entry[i]);
  if (!fn || fn->go_switch != GO_SWITCH_MORESTACK) return;

  /* Below morestack, and morestack_noctxt jumping to it */
  for (; i >= 0; i--) {
    fn = callee_fn(&CLG_(current_call_stack).entry[i]);
    if (!fn || VG_(strncmp)(fn->name, morestack_prefix,
                            sizeof(morestack_prefix) - 1) != 0) break;
  }
  CLG_(go_restart) = i >= 0 ? fn : 0;
}

/* Pop the frames on top that run on g0 for the current goroutine. */
static void pop_nested_frames(void)
{
  for (;;) {
    Int csp = CLG_(current_call_stack).sp;
    UWord tag;
    if (csp == 0) return;
    tag = CLG_(current_call_stack).entry[csp-1].go_tag;
    if (tag == 0 || tag == CLG_(go_owner)) return;
    CLG_(pop_call_stack)();
  }
}

/* Suspend the current call stack under its goroutine. If the goroutine is
 * not known (no switch function was entered yet), its frames are popped.
 *
 * The scheduler may run inside a nested g0 call (newstack preempting the
 * goroutine), so pop those g0 frames: the goroutine resumes below them. */
static void leave_current(void)
{
  if (!go_stacks) go_stacks = VG_(HT_construct)("cl.gostack.lc.1");

  if (CLG_(go_owner)) {
    pop_nested_frames();
    VG_(HT_add_node)(go_stacks, stack_out(CLG_(go_owner)));
    return;
  }
  drop_current_stack();
}

/* A goroutine can be descheduled while it runs on g0 for itself, without
 * mcall: newstack preempts it and the scheduler finds nothing else to run,
 * so the thread sleeps with the goroutine's call stack still current. Take
 * that call stack when another thread resumes the goroutine. */
static go_stack* steal_from_other_thread(UWord g)
{
  thread_info** threads = CLG_(get_threads)();
  ThreadId me = CLG_(current_tid);
  ThreadId tid;
  go_stack* s;

  for (tid = 1; tid < VG_N_THREADS; tid++) {
    thread_info* t = threads[tid];
    if (!t || tid == me || t->go_owner != g) continue;

    CLG_(switch_thread)(tid);
    if (CLG_(current_state).sig > 0) {
      /* Not reachable below a signal handler; forget the goroutine */
      CLG_(go_owner) = CLG_(go_cur) = 0;
      CLG_(switch_thread)(me);
      return 0;
    }
    pop_nested_frames();
    s = stack_out(g);
    stack_fresh();
    CLG_(go_owner) = CLG_(go_cur) = 0;
    CLG_(switch_thread)(me);
    return s;
  }
  return 0;
}

/* gogo jumps to the goroutine <g>, starting it if <bb> is a function entry. */
static Bool gogo_to(UWord g, BB* bb, Addr sp)
{
  go_stack* s;

  if (g == 0 || g == GO_TAG_SYSTEM) return False;

  if (g == CLG_(go_owner)) {
    /* Back to the goroutine whose stack we are on, after it ran on g0
     * (morestack -> newstack -> gogo). */
    pop_nested_frames();
    resume(sp);
    CLG_(go_cur) = tag_below_top();
    return False;
  }

  leave_current();

  s = VG_(HT_remove)(go_stacks, g);
  if (!s) s = steal_from_other_thread(g);
  if (s && bb->is_entry) {
    /* g was reused for a new goroutine; the old one exited */
    stack_free(s);
    s = 0;
  }

  if (s) {
    stack_in(s);
    resume(sp);
  }
  else
    stack_fresh();

  CLG_(go_owner) = g;
  CLG_(go_cur) = tag_below_top();
  return True;
}

/* mcall runs a function on g0 that never returns; g0 starts over from the
 * bottom of its stack. */
static void mcall_to(UWord g0)
{
  go_stack* s;

  if (g0 == 0 || g0 == GO_TAG_SYSTEM || g0 == CLG_(go_owner)) return;

  leave_current();

  /* Pop what g0 was doing when it last left (the scheduler up to gogo) */
  s = VG_(HT_remove)(go_stacks, g0);
  if (s) {
    stack_in(s);
    drop_current_stack();
  }
  stack_fresh();

  CLG_(go_owner) = g0;
  CLG_(go_cur) = g0;
}

#endif /* GO_G_REG */

Bool CLG_(go_frame_blocks_unwind)(call_entry* ce, fn_node* running)
{
  fn_node* fn;

  /* Still inside the switch function: its SP is on either stack */
  fn = callee_fn(ce);
  if (fn && fn == running && fn->go_switch != GO_SWITCH_NONE) return True;

  /* A frame of another stack */
  return ce->go_tag && CLG_(go_cur) && ce->go_tag != CLG_(go_cur);
}

void CLG_(go_after_pop)(void)
{
#if defined(GO_G_REG)
  CLG_(go_cur) = tag_below_top();
#endif
}

/* Called for every BB of a Go program before the jump into <bb> is
 * classified. Returns GO_SYNC_SWAPPED if the current call stack was swapped
 * for another goroutine's, so the caller must reload its view of the previous
 * BB, and GO_SYNC_RESTART if the jump into <bb> must not become a call. */
Int CLG_(go_sync)(BB* bb, BB* last_bb, Addr sp)
{
#if defined(GO_G_REG)
  fn_node* fn = bb->fn;
  fn_node* last_fn = last_bb ? last_bb->fn : 0;
  fn_node* restart = CLG_(go_restart);
  ClgJumpKind jmpkind;

  /* Go never switches goroutines inside a signal handler */
  if (CLG_(current_state).sig > 0) return GO_SYNC_NONE;

  if (restart) {
    /* Still in the restarting function until it jumps to its entry */
    if (fn != restart || bb->is_entry) CLG_(go_restart) = 0;
    if (fn == restart && bb->is_entry && last_fn == restart)
      return GO_SYNC_RESTART;
  }

  if (fn->go_switch != GO_SWITCH_NONE && fn != last_fn) {
    /* Learn which goroutine we are on: the g register is valid when a
     * switch function is entered */
    if (CLG_(go_owner) == 0)
      CLG_(go_owner) = CLG_(go_cur) = read_g();
    return GO_SYNC_NONE;
  }

  if (!last_fn || last_fn->go_switch == GO_SWITCH_NONE || fn == last_fn)
    return GO_SYNC_NONE;

  jmpkind = last_bb->jmp[CLG_(current_state).jmps_passed].jmpkind;
  if (jmpkind == jk_Return) return GO_SYNC_NONE;

  switch (last_fn->go_switch) {
  case GO_SWITCH_GOGO:
    return gogo_to(read_g(), bb, sp) ? GO_SYNC_SWAPPED : GO_SYNC_NONE;
  case GO_SWITCH_MCALL:
    mcall_to(read_g());
    return GO_SYNC_SWAPPED;
  case GO_SWITCH_NESTED:
  case GO_SWITCH_MORESTACK:
    /* A call into the function to run on g0. Returning from it is handled
     * by the pop. */
    CLG_(go_cur) = GO_TAG_SYSTEM;
    return GO_SYNC_NONE;
  default:
    return GO_SYNC_NONE;
  }
#else
  return GO_SYNC_NONE;
#endif
}

void CLG_(go_init_thread)(thread_info* t)
{
  t->go_owner = 0;
  t->go_cur = 0;
  t->go_restart = 0;
}

void CLG_(go_save_thread)(thread_info* t)
{
  t->go_owner = CLG_(go_owner);
  t->go_cur = CLG_(go_cur);
  t->go_restart = CLG_(go_restart);
}

void CLG_(go_load_thread)(thread_info* t)
{
  CLG_(go_owner) = t->go_owner;
  CLG_(go_cur) = t->go_cur;
  CLG_(go_restart) = t->go_restart;
}

/* The current thread's call stack was popped: start over without a known
 * goroutine. */
void CLG_(go_unwind_thread)(void)
{
  CLG_(go_owner) = 0;
  CLG_(go_cur) = 0;
  CLG_(go_restart) = 0;
}

/* Pop the frames of all suspended goroutines. */
void CLG_(go_unwind_all)(void)
{
#if defined(GO_G_REG)
  call_stack cs;
  fn_stack fs;
  exec_state saved;
  go_stack* s;
  UInt i, n;
  UWord* keys;

  if (!go_stacks || VG_(HT_count_nodes)(go_stacks) == 0) return;

  CLG_(copy_current_call_stack)(&cs);
  CLG_(copy_current_fn_stack)(&fs);
  saved = CLG_(current_state);

  n = VG_(HT_count_nodes)(go_stacks);
  keys = (UWord*) CLG_MALLOC("cl.gostack.ua.1", n * sizeof(UWord));
  VG_(HT_ResetIter)(go_stacks);
  for (i = 0; i < n; i++) keys[i] = ((go_stack*)VG_(HT_Next)(go_stacks))->key;

  for (i = 0; i < n; i++) {
    s = VG_(HT_remove)(go_stacks, keys[i]);
    stack_in(s);
    drop_current_stack();
  }
  VG_(free)(keys);

  CLG_(set_current_call_stack)(&cs);
  CLG_(set_current_fn_stack)(&fs);
  CLG_(current_state) = saved;
#endif
}
