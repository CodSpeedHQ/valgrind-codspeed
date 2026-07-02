// Callgrind client-request shim for the Python fixture (`recursion.py`).
//
// The CALLGRIND_* client requests are inline-asm sequences, so they can't be
// issued from pure Python. The Python fixture loads this shared library via
// `ctypes` and calls these entry points to drive instrumentation, mirroring
// what pytest-codspeed's instrument-hooks does: skip the Python runtime objects
// at runtime, then START/ZERO around the measured region and STOP after.
//
// Build (shared, against the in-repo client-request headers):
//   cc -g -O0 -shared -fPIC -I callgrind -I include ...

#include <callgrind.h>

// Add an object file to Callgrind's obj-skip list at runtime. Matching is exact
// against the mapped object path, so the caller passes a realpath (same as
// instrument-hooks' `callgrind_add_obj_skip`).
void clg_add_obj_skip(const char *path) {
    CALLGRIND_ADD_OBJ_SKIP(path);
}

void clg_start(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;
}

void clg_stop(void) {
    CALLGRIND_STOP_INSTRUMENTATION;
}
