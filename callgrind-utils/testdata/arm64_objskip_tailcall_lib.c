// Shared library half of arm64_objskip_tailcall.c. Everything here will be
// marked skip=True via --obj-skip=<this .so>. `skipped_entry` is called
// (real `bl`) from the non-skipped main executable, then tail-calls
// `skipped_relay` (still inside this same skipped object), which itself
// tail-calls back OUT into `visible_target` in the main executable --
// mirroring the shape callgrind's obj-skip splicing is supposed to handle
// (attribute the call directly to the real, non-skipped caller), but with
// the skipped side of the chain built from emulated (tail-called) frames.
extern int visible_target(int seed);

__attribute__((noinline)) static int skipped_relay(int seed) {
    // Two REAL (non-tail) calls out to non-skipped code, with skipped-side
    // work interleaved, then a final tail call out -- stresses the
    // `passed = bbcc->bb->cjmp_count` approximation in bbcc.c's
    // "call from skipped to nonskipped" splice across repeated
    // skip/nonskip transitions within one skipped frame's lifetime.
    int a = visible_target(seed);
    int b = visible_target(seed + a);
    return visible_target(seed + a + b);
}

__attribute__((noinline)) int skipped_entry(int seed) {
    return skipped_relay(seed + 1);
}
