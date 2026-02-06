#include "tracegrind.h"

static int __attribute__((noinline)) fibo(int n) {
    if (n <= 1) return n;
    return fibo(n - 1) + fibo(n - 2);
}

int main(void) {
    /* Instrumentation is off (--instr-atstart=no).
       Only the fibo(2) call will be traced. */
    TRACEGRIND_ADD_MARKER("before-fibo");
    TRACEGRIND_START_INSTRUMENTATION;
    int result = fibo(2);
    TRACEGRIND_STOP_INSTRUMENTATION;
    TRACEGRIND_ADD_MARKER("after-fibo");

    return result != 1;
}
