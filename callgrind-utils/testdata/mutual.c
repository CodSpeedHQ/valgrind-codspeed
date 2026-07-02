// Fixture: mutual recursion `is_even <-> is_odd`, forming a two-function cycle
// reached from `main`. Exercises cyclic call topology. See recursion.c for the
// instrumentation/build conventions.

#include <callgrind.h>

static int is_odd(int n);

static int is_even(int n) {
    return n == 0 ? 1 : is_odd(n - 1);
}

static int is_odd(int n) {
    return n == 0 ? 0 : is_even(n - 1);
}

int main(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    volatile int sink = is_even(6);
    (void)sink;

    CALLGRIND_STOP_INSTRUMENTATION;
    return 0;
}
