#include "tracegrind.h"
#include <stdexcept>

/*
 * Test: C++ exception unwinding through regular (non-inlined) functions.
 *
 * catcher() calls thrower(), which calls do_throw().
 * do_throw() throws an exception that unwinds back through thrower()
 * to catcher()'s catch block. Verifies the call stack is properly
 * maintained across exception unwinding.
 *
 * Call chain:  catcher -> thrower -> do_throw (throws)
 */

static void __attribute__((noinline)) do_throw(int x) {
    if (x > 0)
        throw std::runtime_error("boom");
}

static int __attribute__((noinline)) thrower(int n) {
    volatile int x = n;
    do_throw(x);
    return x;
}

static int __attribute__((noinline)) catcher(int n) {
    try {
        return thrower(n);
    } catch (const std::exception&) {
        return -1;
    }
}

int main() {
    volatile int input = 5;
    TRACEGRIND_ADD_MARKER("start");
    TRACEGRIND_START_INSTRUMENTATION;
    int result = catcher(input);
    TRACEGRIND_STOP_INSTRUMENTATION;
    TRACEGRIND_ADD_MARKER("end");
    return result != -1;
}
