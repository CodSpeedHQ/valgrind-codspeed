// Companion shared library for objskip_seed_underflow.c — the "interpreter".
//
// Every function here is obj-skipped at runtime (the binary passes this .so's
// realpath to CALLGRIND_ADD_OBJ_SKIP before instrumentation starts), so none
// of these frames may ever appear in the output. `skip_drive` plays CPython's
// eval loop: it reaches the instrumentation toggles through extra skipped
// hops (like the ctypes/libffi dive under `clgctl.clg_start()`), then calls
// the measured workload back in the non-skipped binary.
//
// The two-hop dive matters: after the OFF->ON seed's innermost non-skipped
// frame (clg_begin_marker) pops, the returns hop2 -> hop1 -> skip_drive
// execute while the seeded context stack is one entry deep — the state that
// trips bbcc.c's base-sentinel/underflow misfire on arm64.

extern int clg_begin_marker(void);
extern int clg_end_marker(void);
extern int workload(int n);

__attribute__((noinline)) int skip_begin_hop2(void) {
    return clg_begin_marker() + 1;
}

__attribute__((noinline)) int skip_begin_hop1(void) {
    return skip_begin_hop2() + 1;
}

__attribute__((noinline)) int skip_end_hop2(void) {
    return clg_end_marker() + 1;
}

__attribute__((noinline)) int skip_end_hop1(void) {
    return skip_end_hop2() + 1;
}

__attribute__((noinline)) int skip_drive(int n) {
    int acc = skip_begin_hop1(); /* OFF->ON fires two skipped frames down */
    acc += workload(n);          /* the measured region */
    acc += skip_end_hop1();      /* ON->OFF fires two skipped frames down */
    return acc;
}
