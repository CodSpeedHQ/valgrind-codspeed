/* Reproducer for the seeded-shadow-stack "phantom root" bug.
 *
 * When CALLGRIND_START_INSTRUMENTATION fires inside a wrapper chain
 * that then unwinds, the seed pushes every native frame onto callgrind's
 * cxt with jcc=0. pop_call_stack only restores cxt when jcc!=0, so each
 * ret on the way back fails to pop the cxt — leaving the deepest
 * un-returned wrapper stuck on top, phantom-parenting every later call
 * from the real caller.
 *
 * We model a 3-deep wrapper chain (mirroring e.g. a Node.js binding:
 * macro -> C export -> N-API trampoline -> user code) so the fix is
 * exercised across multiple consecutive seeded pops, not just frame 0. */

#include "../callgrind.h"

volatile long sink;

__attribute__((noinline))
static void leaf(void) { sink++; }

__attribute__((noinline))
static void wrapper_inner(void) { CALLGRIND_START_INSTRUMENTATION; }

__attribute__((noinline))
static void wrapper_middle(void) { wrapper_inner(); }

__attribute__((noinline))
static void wrapper_outer(void) { wrapper_middle(); }

int main(void)
{
    wrapper_outer();
    leaf();
    CALLGRIND_STOP_INSTRUMENTATION;
    return 0;
}
