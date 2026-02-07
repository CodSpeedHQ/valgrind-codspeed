#include "tracegrind.h"
#include <setjmp.h>

/*
 * Test: longjmp unwinding multiple call frames.
 *
 * outer() calls middle(), which calls inner().
 * inner() does longjmp back to outer(), skipping middle()'s return.
 * Verifies tracegrind properly unwinds the call stack on non-local jumps.
 *
 * Call chain:  outer -> middle -> inner (longjmp back to outer)
 */

static jmp_buf env;

static void __attribute__((noinline)) inner(int n) {
    volatile int x = n * 2;
    (void)x;
    longjmp(env, 42);
}

static void __attribute__((noinline)) middle(int n) {
    volatile int x = n + 1;
    inner(x);
    /* never reached */
    x = x + 1;
}

static int __attribute__((noinline)) outer(int n) {
    int val = setjmp(env);
    if (val == 0) {
        middle(n);
        /* never reached */
        return -1;
    }
    return val;
}

int main(void) {
    volatile int input = 5;
    TRACEGRIND_ADD_MARKER("start");
    TRACEGRIND_START_INSTRUMENTATION;
    int result = outer(input);
    TRACEGRIND_STOP_INSTRUMENTATION;
    TRACEGRIND_ADD_MARKER("end");
    return result != 42;
}
