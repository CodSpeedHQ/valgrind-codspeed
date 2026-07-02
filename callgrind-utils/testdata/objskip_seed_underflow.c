// Minimal cross-arch reproduction of the arm64 OFF->ON seeding-underflow
// cascade behind the python_fractal_* test failures (full analysis:
// .agents/docs/arm64-python-seeding-underflow-analysis.md).
//
// Call shape, distilled from `python3 -X perf` + ctypes:
//
//   main -> trampoline_call            [asm: FP chain maintained, NO CFI]
//     -> skip_drive                    [obj-skipped .so, "eval loop"]
//       -> skip_begin_hop1 -> skip_begin_hop2      [obj-skipped "ffi dive"]
//         -> clg_begin_marker          [this binary: START fires here]
//       <- returns climb back through the skipped hops
//       -> workload -> leaf_mix        [this binary: the measured region]
//       -> skip_end_hop1 -> skip_end_hop2 -> clg_end_marker   [STOP]
//
// The asm trampoline mimics CPython's -X perf JIT trampolines: it maintains
// the frame-pointer chain but has no .eh_frame FDE. Valgrind's aarch64
// unwinder is CFI-only (m_stacktrace.c), so the OFF->ON seed stops AT the
// trampoline and the seeded context stack is exactly
// [trampoline_call, clg_begin_marker] — one entry deep once
// clg_begin_marker's frame pops. On the next return, bbcc.c's underflow test
// misreads the fn-stack base sentinel as a signal-separation marker,
// handleUnderflow ignores fn->skip, and the skipped hops leak into the graph
// as named, inverted, full-cost nodes. On x86_64 the unwinder's FP fallback
// walks past the trampoline into main/libc, the context stack stays deeper
// than one, and the output is clean — the correct shape on every arch:
//
//   trampoline_call;workload;leaf_mix     (skipped frames folded away)
#include <callgrind.h>
#include <limits.h>
#include <stdlib.h>

extern int skip_drive(int n);
int trampoline_call(int n);

// CPython-trampoline-shaped hop: frame record maintained (so the x86_64
// FP-fallback unwinder can walk through it), but no .cfi_* directives, so no
// FDE is emitted and the CFI-only aarch64 unwinder must stop here.
#if defined(__aarch64__)
__asm__(
    ".text\n"
    ".globl trampoline_call\n"
    ".type trampoline_call, %function\n"
    "trampoline_call:\n"
    "    stp x29, x30, [sp, #-16]!\n"
    "    mov x29, sp\n"
    "    bl skip_drive\n"
    "    ldp x29, x30, [sp], #16\n"
    "    ret\n"
    ".size trampoline_call, .-trampoline_call\n");
#elif defined(__x86_64__)
__asm__(
    ".text\n"
    ".globl trampoline_call\n"
    ".type trampoline_call, @function\n"
    "trampoline_call:\n"
    "    pushq %rbp\n"
    "    movq %rsp, %rbp\n"
    "    call skip_drive@PLT\n"
    "    popq %rbp\n"
    "    ret\n"
    ".size trampoline_call, .-trampoline_call\n");
#else
#error "objskip_seed_underflow: unsupported architecture"
#endif

// Innermost frame of the OFF->ON transition, reached through the skipped
// hops — the clg_start() twin.
__attribute__((noinline)) int clg_begin_marker(void) {
    CALLGRIND_START_INSTRUMENTATION;
    return 1;
}

__attribute__((noinline)) int clg_end_marker(void) {
    CALLGRIND_STOP_INSTRUMENTATION;
    return 1;
}

__attribute__((noinline)) int leaf_mix(int v) {
    return (int)(((unsigned)v * 2654435761u) >> 16);
}

// The measured region, called from the skipped library — must fold as a
// direct child of trampoline_call.
__attribute__((noinline)) int workload(int n) {
    int acc = 1;
    for (int i = 0; i < n; i++)
        acc += leaf_mix(acc + i);
    return acc;
}

int main(int argc, char **argv) {
    // argv[1] = path of the companion .so; register by realpath, exactly as
    // pytest-codspeed / fractal.py do (Callgrind keys obj-skip on the mapped
    // object path).
    char resolved[PATH_MAX];
    if (argc > 1 && realpath(argv[1], resolved))
        CALLGRIND_ADD_OBJ_SKIP(resolved);
    int r = trampoline_call(512);
    return r > 0 ? 0 : 1;
}
