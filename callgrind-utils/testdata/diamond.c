// Fixture: a diamond graph where `bottom` is a shared callee reached via two
// paths: `main -> top -> {left, right} -> bottom`. Exercises a node with two
// distinct incoming edges. See recursion.c for the conventions.

#include <callgrind.h>

static int bottom(int n) {
    return n * 2;
}

static int left(int n) {
    return bottom(n) + 1;
}

static int right(int n) {
    return bottom(n) + 2;
}

static int top(int n) {
    return left(n) + right(n);
}

int main(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    volatile int sink = top(5);
    (void)sink;

    CALLGRIND_STOP_INSTRUMENTATION;
    return 0;
}
