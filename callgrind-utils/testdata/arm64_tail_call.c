// AArch64-focused reproducer for tail-call handling in Callgrind's shadow
// stack. Exercises two paired fixes: guest_arm64_toIR.c now classifies an
// unlinked `B` as Ijk_Boring (a tail call) instead of Ijk_Call, and
// bbcc.c's return matching must pop through the resulting chain of same-SP
// tail-call frames in one go once the real `ret` finally executes. Built
// with -O2 so `stage_a -> stage_b -> stage_c` compile to sibling calls
// (`b`, not `bl`) that reuse a single stack frame. The seed is threaded
// through a volatile global (rather than a compile-time constant literal)
// so GCC's interprocedural constant propagation can't clone/fold
// stage_a/stage_b/stage_c into `.constprop.0` variants or evaluate the
// chain down to a single `mov`+`ret` -- the tail-call shape must survive
// codegen for this fixture to exercise anything.
#include <callgrind.h>

volatile int g_seed = 5;

__attribute__((noinline)) static int stage_c(int n) {
    return n * 2 + 1;
}

__attribute__((noinline)) static int stage_b(int n) {
    return stage_c(n + 1);
}

__attribute__((noinline)) static int stage_a(int n) {
    return stage_b(n + 1);
}

__attribute__((noinline)) static int run_measured(int n) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    int result = stage_a(n);

    CALLGRIND_STOP_INSTRUMENTATION;
    return result;
}

__attribute__((noinline)) static int warmup(int n) {
    volatile int acc = 0;
    for (int i = 0; i < 2; i++) {
        acc += stage_a(n);
    }
    (void)acc;
    return run_measured(n);
}

__attribute__((noinline)) static int run_benchmark(int n) {
    return warmup(n);
}

int main(void) {
    volatile int result = run_benchmark(g_seed);
    (void)result;
    return 0;
}
