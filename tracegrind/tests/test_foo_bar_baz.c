#include "tracegrind.h"

static int __attribute__((noinline)) baz(int n) {
    return n * 2;
}

static int __attribute__((noinline)) bar(int n) {
    return baz(n) + 1;
}

static int __attribute__((noinline)) foo(int n) {
    return bar(n) + bar(n + 1);
}

int main(void) {
    TRACEGRIND_ADD_MARKER("start");
    TRACEGRIND_START_INSTRUMENTATION;
    int result = foo(3);
    TRACEGRIND_STOP_INSTRUMENTATION;
    TRACEGRIND_ADD_MARKER("end");

    return result != (baz(3) + 1 + baz(4) + 1);
}
