// Fixture: a linear call chain `main -> a -> b -> c` (no recursion, no shared
// callees). See recursion.c for the instrumentation/build conventions.

#include <callgrind.h>

static int c(int n) {
    return n + 1;
}

static int b(int n) {
    return c(n) + 1;
}

static int a(int n) {
    return b(n) + 1;
}

int main(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    volatile int sink = a(5);
    (void)sink;

    CALLGRIND_STOP_INSTRUMENTATION;
    return 0;
}
