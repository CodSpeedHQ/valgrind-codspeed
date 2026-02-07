#include "tracegrind.h"

/*
 * Test: deep recursion (100 levels).
 *
 * recurse() calls itself 100 times, then returns back through
 * all frames. Verifies the call stack handles deep nesting and
 * produces balanced ENTER/EXIT pairs.
 */

static int __attribute__((noinline)) recurse(int depth) {
    volatile int d = depth;
    if (d <= 0)
        return 0;
    return recurse(d - 1) + 1;
}

int main(void) {
    volatile int input = 100;
    TRACEGRIND_ADD_MARKER("start");
    TRACEGRIND_START_INSTRUMENTATION;
    int result = recurse(input);
    TRACEGRIND_STOP_INSTRUMENTATION;
    TRACEGRIND_ADD_MARKER("end");
    return result != 100;
}
