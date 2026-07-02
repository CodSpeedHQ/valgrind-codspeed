// Fixture for callgrind-utils snapshot tests.
//
// A small, pure-compute call graph: direct recursion (`fib` -> `fib`) plus two
// helper edges (`compute` -> `fib`, `compute` -> `square`) under `main`.
//
// Mirrors how CodSpeed drives a benchmark: instrumentation is off at startup
// (run with `--instr-atstart=no`), so loader/libc-start frames are excluded,
// then turned on around the measured region. Build with `-g -O0` so the
// functions are real (no inlining) and carry debug names.
//
// Requires the in-repo Callgrind client-request header:
//   cc -g -O0 -I callgrind -I include ...

#include <callgrind.h>

static int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

static int square(int n) {
    return n * n;
}

static int compute(int n) {
    return fib(n) + square(n);
}

int main(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    volatile int sink = compute(8);
    (void)sink;

    CALLGRIND_STOP_INSTRUMENTATION;
    return 0;
}
