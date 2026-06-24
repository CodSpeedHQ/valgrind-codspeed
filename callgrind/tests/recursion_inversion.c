/* Regression test for the AArch64 callgrind missed-return bug.
 *
 * On AArch64 the call instruction (bl/blr) does not move SP: the return
 * address goes into the link register, not onto the stack. A callee reached
 * this way therefore records its caller's SP as its own shadow-stack entry SP,
 * so when it returns the return-target SP is *equal* to the entry SP. Such an
 * "equal-SP" entry frame sits beneath the SP-lower frames of any sub-calls the
 * callee makes (e.g. libc malloc reached through the PLT). When the callee
 * returns, CLG_(unwind_call_stack) used to let those SP-lower sub-call frames
 * consume the minpops budget that bounds the equal-SP pops, so the callee's
 * own entry frame was never popped. The stale frame kept the callee's context
 * active, mis-attributing the caller's continuation to the callee (inverted
 * call edges such as "malloc -> driftsort_main") and fabricating recursion
 * clones of non-recursive functions.
 *
 * Here leaf_alloc() is NOT recursive but calls libc malloc/free (reached via
 * the PLT). rec_parent() is genuinely recursive and calls leaf_alloc(). With
 * the bug, callgrind records leaf_alloc/malloc as a *parent* of rec_parent and
 * emits a fabricated leaf_alloc'2 recursion clone. The fix keeps the call
 * graph correct: rec_parent -> leaf_alloc -> malloc, leaf_alloc with no clone.
 */

#include <stdlib.h>
#include <string.h>
#include "../callgrind.h"

char *volatile g_sink;

/* Warm the allocator before measuring so the one-off cold arena/TLS init does
 * not dominate; matches how a real benchmark reaches steady state. */
__attribute__((noinline)) static void warmup(void)
{
    for (int i = 0; i < 5; i++) {
        char *p = (char *)malloc(64);
        memset(p, i, 64);
        g_sink = p;
        free(p);
    }
}

/* Non-recursive leaf that allocates via libc (PLT-dispatched emulated call). */
__attribute__((noinline)) static long leaf_alloc(int n)
{
    char *b = (char *)malloc(n);
    g_sink = b;
    memset(b, 1, n);
    long acc = b[0] + b[n - 1];
    free(b);
    return acc;
}

/* Genuinely recursive parent that calls the allocating leaf at every level. */
__attribute__((noinline)) static long rec_parent(int d)
{
    long acc = leaf_alloc(32);
    if (d > 0)
        acc += rec_parent(d - 1);
    return acc;
}

int main(void)
{
    warmup();
    CALLGRIND_START_INSTRUMENTATION;
    long r = rec_parent(4);
    CALLGRIND_STOP_INSTRUMENTATION;
    return (int)(r & 0x7f);
}
