// AArch64 reproducer for the production "free calls X" misattribution: after a
// tail-called `free()` returns, its return is misclassified as a fresh call
// back into the caller, so `free` becomes a phantom parent (`caller'2`) of the
// post-free work. This is the symptom arm64_free_during_recursion.c and
// fractal_alloc.rs hit amid a full benchmark ("free showing up as a parent of
// analyze_fractal_tree, stealing ~13% of the run time").
//
// This shares its ROOT CAUSE with arm64_plt_phantom_recursion.c -- it is the
// same stale-`nonskipped` bug -- but surfaces on a *tail-called libc free*,
// which is why it is worth pinning as its own fixture. The two triggers that
// must combine (verified by ablation: removing either makes the phantom vanish):
//
//   1. `caller` calls `malloc` inside the measured region. That is a hop
//      `caller -> PLT stub -> libc malloc` into another ELF object, which
//      Callgrind treats as a skipped region; when malloc returns,
//      `current_state.nonskipped = caller` is left dangling (see
//      arm64_plt_phantom_recursion.c for the mechanism). The next `bl dealloc1`
//      then has its shadow-stack return address computed from the *malloc* call
//      site instead of the `bl dealloc1` site (bbcc.c's
//      `FIXME: take the real passed count from shadow stack`).
//   2. `free` is reached through two thin tail-call wrappers
//      (`dealloc1 -> dealloc2 -> free`). At -O2 each `return f(p);` is a tail
//      branch `b f`, which Callgrind emulates as a call with `ret_addr == 0`.
//
// When `free` returns to `caller`, the return matcher walks the shadow stack
// looking for a frame whose recorded return address matches: the `free`/
// `dealloc2` frames have `ret_addr == 0` and `dealloc1`'s recorded address is
// the *wrong* (malloc-site) one from (1), so nothing matches. The return is
// misclassified "RET w/o CALL" and re-promoted to a call into `caller` ->
// phantom `caller'2`, under which `post_free_work` is misattributed.
//
// AArch64-specific for the same reason as arm64_plt_phantom_recursion.c: on x86
// the return is detected by SP movement regardless of the recorded address.
// Built at -O2 by tests/snapshot.rs; libc frames redact to `???`. The `malloc`
// must stay inside the measured region (it is trigger 1); a direct `free` with
// no tail wrappers, or a tail-called `free` without the preceding `malloc`,
// both profile cleanly.
#include <callgrind.h>
#include <stdlib.h>

// Two thin dealloc wrappers, each a pure tail call, mirroring the
// `dealloc_wrapper1 -> dealloc_wrapper2 -> free` chain seen in production.
__attribute__((noinline)) static void dealloc2(void *p) { free(p); }
__attribute__((noinline)) static void dealloc1(void *p) { dealloc2(p); }

// Ordinary work `caller` does after the free returns; the bug misattributes it
// under the phantom `caller'2` instead of directly under `caller`.
__attribute__((noinline)) static int post_free_work(int x) {
    volatile int acc = x;
    for (int i = 0; i < 8; i++) {
        acc += i * x;
    }
    return acc % 97;
}

// Non-recursive. Any `caller'2` clone in the snapshot is the bug.
__attribute__((noinline)) static int caller(void) {
    void *p = malloc(64);     // PLT hop -> libc; leaves `nonskipped` dangling
    volatile char *c = p;
    c[0] = 1;
    dealloc1(p);              // bl dealloc1 -> tail chain -> free (emulated calls)
    return post_free_work(3); // misattributed under phantom caller'2
}

__attribute__((noinline)) static int run_measured(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    int result = caller();

    CALLGRIND_STOP_INSTRUMENTATION;
    return result;
}

__attribute__((noinline)) static int warmup(void) {
    volatile int acc = 0;
    for (int i = 0; i < 2; i++) {
        acc += caller();
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
