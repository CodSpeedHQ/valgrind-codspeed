/* Regression test for the AArch64 seeded-frame return off-by-one.
 *
 * When instrumentation is turned on mid-run (CALLGRIND_START_INSTRUMENTATION,
 * as CodSpeed does for scoped profiling), CLG_(reconstruct_call_stack_from_native)
 * seeds a shadow-stack frame for every native caller frame, taking the return
 * address from VG_(get_StackTrace). That API reports caller frames at the *last
 * byte of the call instruction* (return_PC - 1), not the return PC. setup_bbcc's
 * return matcher compares against bb_addr(return-target) == the return PC, so the
 * seeded ret_addr was off by one. On AArch64 a `ret` lands at SP equal to the
 * seeded frame's entry SP, so the match relies on ret_addr; the off-by-one made
 * the return fail, get demoted to a jump and re-promoted to a call — inverting the
 * edge across the seeded frame (e.g. `mid -> outer`, callee shown calling caller).
 *
 * Here instrumentation starts deep inside inner(); the calls outer->mid->inner
 * predate measurement (they are seeded), so during measurement only the *returns*
 * inner->mid->outer->main happen. A correct build records no call edges among
 * inner/mid/outer (only inner's real malloc/free calls); the buggy build records a
 * spurious callee->caller edge.
 */

#include <stdlib.h>
#include <string.h>
#include "../callgrind.h"

char *volatile g_sink;

__attribute__((noinline)) long inner(int n)
{
    CALLGRIND_START_INSTRUMENTATION;        /* start deep: inner/mid/outer/main seeded */
    char *p = (char *)malloc(n);
    g_sink = p;
    memset(p, 1, n);
    long a = p[0] + p[n - 1];
    free(p);
    return a;                               /* return crosses the seeded frames */
}

__attribute__((noinline)) long mid(int n)   { long r = inner(n); return r + (n & 1); }
__attribute__((noinline)) long outer(int n) { long r = mid(n);   return r + (n & 2); }

int main(void)
{
    char *p = (char *)malloc(64);           /* warm the arena before START (uninstrumented) */
    g_sink = p;
    free(p);
    long r = outer(64);
    CALLGRIND_STOP_INSTRUMENTATION;
    return (int)(r & 0x7f);
}
