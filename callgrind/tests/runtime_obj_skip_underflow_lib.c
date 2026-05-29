/* Library that triggers the call-stack-underflow leak channel in
 * callgrind obj-skip.
 *
 * Setup: recursive function in the skipped lib. Main calls in with
 * instrumentation OFF, so callgrind's call stack is never populated.
 * At the deepest frame, instrumentation is flipped ON. Each RET on
 * the way back then sees csp == 0, hits handleUnderflow, resets
 * cxt = 0, and force-pushes the current fn (which lives in the
 * skipped lib) as the new top context — leaking N times for an
 * N-deep stack.
 *
 * This is the same shape as Python 3.14's interpreter dispatch
 * leaks: deep recursive eval-loop frames where instrumentation was
 * started somewhere down the stack and every return pops past an
 * empty callgrind stack. */

#include "../callgrind.h"

volatile long sink;

__attribute__((noinline))
void skipme_recurse(int n)
{
    if (n == 0) {
        CALLGRIND_START_INSTRUMENTATION;
        return;
    }
    skipme_recurse(n - 1);
    sink += n;
}

__attribute__((noinline))
void skipme_run(int depth)
{
    skipme_recurse(depth);
    CALLGRIND_STOP_INSTRUMENTATION;
}
