#include "tracegrind.h"

/* Force inlining - with --read-inline-info=yes these should produce
 * ENTER_INLINED / EXIT_INLINED events in the trace */
static inline __attribute__((always_inline)) int inlined_work(int a, int b) {
    /* Make the function large enough to span multiple basic blocks
     * so at least one BB boundary falls inside inlined code */
    int result = 0;
    if (a > 0) {
        result = a * b;
    } else {
        result = a + b;
    }
    return result;
}

/* Prevent inlining - SHOULD appear as ENTER/EXIT */
static int __attribute__((noinline)) not_inlined_caller(int n) {
    /* Use volatile to prevent constant propagation */
    volatile int x = n;
    return inlined_work(x, x + 1);
}

int main(void) {
    volatile int input = 3;
    TRACEGRIND_ADD_MARKER("start");
    TRACEGRIND_START_INSTRUMENTATION;
    int result = not_inlined_caller(input);
    TRACEGRIND_STOP_INSTRUMENTATION;
    TRACEGRIND_ADD_MARKER("end");
    return result != 12;
}
