/* Reproducer for the seeded-shadow-stack "phantom root" bug.
 *
 * When CALLGRIND_START_INSTRUMENTATION fires inside a function that then
 * returns, the seed pushes that function onto callgrind's cxt chain with
 * jcc=0. pop_call_stack only restores cxt when jcc!=0, so after the ret
 * the cxt is stuck with that function on top, and any subsequent call
 * from the real caller appears as a child of it. */

#include "../callgrind.h"

volatile long sink;

__attribute__((noinline))
static void leaf(void) { sink++; }

__attribute__((noinline))
static void start_and_return(void)
{
    CALLGRIND_START_INSTRUMENTATION;
}

int main(void)
{
    start_and_return();
    leaf();
    CALLGRIND_STOP_INSTRUMENTATION;
    return 0;
}
