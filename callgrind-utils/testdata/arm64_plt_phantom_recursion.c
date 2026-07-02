// AArch64 reproducer for the "non-recursive function appears recursive"
// callstack bug (a phantom `foo'2` recursion clone). This is the minimal
// distillation of fractal.rs's full-trace `complex_fractal_benchmark'2`.
//
// Trigger sequence, all inside `outer`:
//   1. `memset` a stack buffer. At -O2 this is a real `bl memset@plt`, i.e. a
//      hop `outer -> PLT stub -> libc memset` that crosses into another ELF
//      object. Callgrind treats the PLT hop as a *skipped region*, so memset's
//      shadow-stack frame stores `nonskipped = outer`. When memset returns,
//      `pop_call_stack` restores `current_state.nonskipped = outer` and nothing
//      clears it again.
//   2. An ordinary `bl leaf`. In the delayed-push path, because `nonskipped` is
//      still set, bbcc.c overrides the call's `from`/`passed` with the stale
//      `nonskipped` BB (the `FIXME: take the real passed count` line), so
//      `leaf`'s recorded return address is computed from the *memset* call site
//      instead of the `bl leaf` site.
//   3. When `leaf` returns, that wrong return address does not match, so the
//      return is misclassified "RET w/o CALL" and re-promoted to a fresh call
//      back into `outer`'s body -> phantom `outer'2`. Everything `outer` does
//      after `leaf` (here `sibling`) is then misattributed under `outer'2`.
//
// This is AArch64-specific: on x86 `call`/`ret` move SP, so the return is
// detected by SP alone regardless of the corrupted return address. On AArch64
// `bl`/`ret` leave SP unchanged across the call boundary, so Callgrind relies
// entirely on the (here wrong) return-address match.
//
// `leaf` and `sibling` are deliberately NON-recursive, so any `'2` clone in the
// snapshot is unambiguously the bug. Built at -O2 by tests/snapshot.rs.
#include <callgrind.h>
#include <string.h>

// Big enough (and volatile) that the compiler emits a real `bl memset@plt`
// rather than inlining the clear.
#define BUF_BYTES 512

__attribute__((noinline)) static int leaf(const volatile char *buf) {
    int acc = 0;
    for (int i = 0; i < BUF_BYTES; i += 64) {
        acc += buf[i];
    }
    return acc + 7;
}

__attribute__((noinline)) static int sibling(int x) {
    return x * 2 + 1;
}

// Non-recursive. Mirrors Rust's `complex_fractal_benchmark`, whose
// `let mut pool = Pool::new()` emits the same leading `bl memset@plt`.
__attribute__((noinline)) static int outer(void) {
    volatile char buf[BUF_BYTES];
    memset((void *)buf, 0, sizeof buf); // bl memset@plt -> libc (skipped region)
    int a = leaf(buf);                  // ordinary bl; its return is misdetected
    int b = sibling(a);                 // misattributed under phantom outer'2
    return a + b;
}

__attribute__((noinline)) static int run_measured(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    int result = outer();

    CALLGRIND_STOP_INSTRUMENTATION;
    return result;
}

__attribute__((noinline)) static int warmup(void) {
    volatile int acc = 0;
    for (int i = 0; i < 2; i++) {
        acc += outer();
    }
    (void)acc;
    return run_measured();
}

__attribute__((noinline)) static int run_benchmark(void) {
    return warmup();
}

int main(void) {
    volatile int result = run_benchmark();
    (void)result;
    return 0;
}
