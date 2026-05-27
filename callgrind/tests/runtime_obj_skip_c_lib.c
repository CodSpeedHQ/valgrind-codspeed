/* Library that lives in a separate ELF object so the main binary
 * can register its path for runtime obj-skip.
 *
 * skipme_run() flips instrumentation on from *inside* the skipped
 * object, then calls skipme_func. This is the trigger for the
 * `current_state.cxt == 0` push path in setup_bbcc: the very first
 * BB after instrumentation start lives in a skipped object, so the
 * (cxt==0) clause force-pushes a skipped fn as the new top context
 * and it leaks into the dump as a top-level fn= block. */

#include "../callgrind.h"

volatile long sink;

__attribute__((noinline))
void skipme_func(int n)
{
    for (int i = 0; i < n; i++) sink += i;
}

__attribute__((noinline))
void skipme_run(int n)
{
    CALLGRIND_START_INSTRUMENTATION;
    skipme_func(n);
    CALLGRIND_STOP_INSTRUMENTATION;
}
