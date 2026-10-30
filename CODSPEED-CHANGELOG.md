# CodSpeed Valgrind Changelog

This file documents changes made to Valgrind for CodSpeed integration, beyond the baseline Valgrind distribution.

## Fixes

### Cache simulation: suppress the host L3 warning for explicit LL settings

Callgrind and Cachegrind no longer print the warning about using the host's
last-level cache when `--LL` (or its `--L2` alias) supplies the cache configuration.
The warning remains when LL is auto-detected. Cache geometry and simulation
results are unchanged.

### Callgrind: no crash on signals handled on an alternate stack

Callgrind no longer aborts with `Assertion 'sigNum == CLG_(current_state).sig'
failed` when a signal handler runs on a `sigaltstack` placed above the
interrupted stack. The stack-pointer based unwinding mistook such a handler for
a `longjmp` out of the handler. Go programs hit this on every preemption signal
(`SIGURG`) and during garbage collection.

## Features

### Fixed Linux startup stack layout

Reduce simulated cache-miss variation from argument and environment sizes
by keeping the Linux initial stack pointer at a fixed offset from the stack top.

### Deterministic randomness: `--deterministic-rng`

**Feature**: Added `--deterministic-rng=no|yes` (default `no`). When enabled, randomness the client reads from the kernel or the CPU comes from a fixed-seed PRNG, so it is identical across runs.

**Motivation**: Programs seed hash tables and similar structures from startup randomness. A different seed changes bucket collisions and probe lengths in HashMap-heavy benchmarks, which shows up as instruction-count noise under Callgrind.

**How it works**:
- `AT_RANDOM` auxv bytes (glibc stack canary and pointer guard, Go runtime hash seed) come from a dedicated stream.
- `getrandom(2)` (Rust `RandomState`, CPython hash seed, OpenSSL, glibc `arc4random`) is served from the PRNG.
- `read`, `pread` and `readv` on `/dev/random` and `/dev/urandom` are served from the PRNG. The device is identified by resolving the fd through `/proc/self/fd`, so symlinks to the devices are covered.
- Reads of `/proc/sys/kernel/random/uuid` return lowercase UUIDv4 text from the thread's stream; `/proc/sys/kernel/random/boot_id` uses a fixed seed-derived UUID independent of thread or read order. This covers `read`, `pread`, `readv`, `preadv` and `preadv2`. The kernel preserves offsets, short reads, EOF and errors; only returned bytes are replaced. UUIDs are not cached across short reads. Errors and empty reads consume no PRNG state.
- On amd64, `RDRAND` and `RDSEED` draw from the calling thread's PRNG stream and always report success. CPUID still reports them only when the host CPU supports them.
- Each client thread has its own splitmix64 stream, keyed by a fixed built-in seed and the thread's creation index. The N-th created thread always draws the same bytes regardless of scheduling. Output depends only on the order of draws within each thread.
- A forked child continues its parent's streams, so parent and child draw identical bytes. Under `--trace-children=yes` each exec'd program restarts from the seed.

**Usage**:
```bash
valgrind --tool=callgrind --deterministic-rng=yes ./your_program
```

**Limitations**:
- Time (`clock_gettime`, `rdtsc`), pids and tids are not made deterministic.
- Only `/proc/sys/kernel/random/uuid` and `/proc/sys/kernel/random/boot_id` are covered among the files under `/proc/sys/kernel/random/`.
- Fds to `/dev/urandom` or the two procfs files inherited across `exec` are not covered.
- The seed is fixed; there is no option to change it.
- Each thread draws from its own stream, so the bytes a call returns depend on which thread makes it. If a runtime moves work between threads based on timing (thread pools, work stealing, Go goroutines), the results can still differ between runs. `boot_id` is the exception: it is derived from the seed alone, not from a thread's stream.

### Skip `.plt.sec` in `--skip-plt`

**Feature**: Added support for skipping `.plt.sec` sections when using the `--skip-plt` option.

**Motivation**: The `.plt.sec` section is a specialized variant of the Procedure Linkage Table (PLT) used in systems with Intel Control-flow Enforcement Technology (CET). When profiling applications using libraries compiled with it (e.g. libpython), symbols in `.plt.sec` were not skipped by the `--skip-plt` option, causing unresolved addresses to appear in profiling data (e.g., `_PySequence_Tuple` at 0x48ce4a0). Both `.plt` and `.plt.sec` sections contain only jumps to the actual function implementations, and do not have any symbols associated with those wrapper functions.

**How it works**:
- Extends the existing PLT-skipping logic to also recognize and skip `.plt.sec` sections
- Whenever we see a function inside the `.plt.sec` section, skip it

**Usage**: Enable `--skip-plt=yes` and both `.plt` and `.plt.sec` will be skipped.

### Callgrind: Inline Function Tracking

**Feature**: Added `cfni=` (call file name inline) markers to Callgrind output to track execution within inlined functions.

**Usage**: Enable with the `--read-inline-info=yes` flag.

**How it works**:
- Reads DWARF inline location table (`DW_TAG_inlined_subroutine`) from debug info
- Outputs `cfni=function_name` when entering an inlined function
- Outputs `cfni=???` when leaving inline code and returning to non-inlined code
- Tracks transitions between different inlined functions
- Works seamlessly with file context markers (`fi=`, `fe=`)

**Example 1: Same-file inlining**

Source code:
```c
static inline int compute_sum(int x) {
    int sum = 0;
    for (int i = 0; i < x; i++) {
        sum += i;
    }
    return sum;
}

int main() {
    int result = compute_sum(10);  // This call will be inlined
    printf("result=%d\n", result);
    return 0;
}
```

Callgrind output (excerpt):
```
fn=main
12 1
-1 1
cfni=compute_sum        ← Entering inlined function
-6 3
-1 3
+1 4
+1 1
-1 3
cfni=???                ← Leaving inlined function
+13 1
+1 3
cfn=printf             ← Regular function call (not inlined)
```

**Example 2: Cross-file inlining**

Source files:
```c
// helper.h
static inline int add_five(int x) {
    return x + 5;
}

// main.c
#include "helper.h"
int main() {
    int result = add_five(10);  // Inlined from helper.h
    printf("result=%d\n", result);
    return 0;
}
```

Callgrind output (excerpt):
```
fn=main
fi=helper.h             ← File context changes to header
cfni=add_five           ← Entering inlined function from header
+2 1
fe=main.c               ← Returning to original file
cfni=???                ← Leaving inlined function
+1 3
cfn=printf
```

### Run executables with file capabilities: `--allow-file-caps`

**Feature**: Added `--allow-file-caps=patt1,patt2,...` to run executables that carry file capabilities, which Valgrind otherwise refuses.

**Motivation**: Valgrind loads the client executable itself, so the kernel never grants it its file capabilities, and Valgrind refuses to run such executables (`Permission denied`, exit code 126) rather than run them without the capabilities they expect. Some executables carry capabilities that only part of their code needs, and run fine without them.

**How it works**:
- An executable whose name matches one of the patterns is run although it carries file capabilities. It runs without them.
- Patterns use the same syntax as `--trace-children-skip`: comma-separated, with `?` and `*` wildcards.
- The option applies to the client executable and to traced children.
- Setuid and setgid executables are still refused.

**Usage**:
```bash
valgrind --tool=callgrind --trace-children=yes --allow-file-caps=/usr/local/bin/my-tool ./your_program
```

### Callgrind: Object-Level Function Skipping

**Feature**: Added `--obj-skip=<object>` command-line option to exclude entire objects (shared libraries or executables) from profiling.

**Motivation**: When profiling applications, it's often necessary to focus on specific parts of the codebase while excluding standard libraries or third-party dependencies. The existing `--fn-skip=<function>` option works at the function level, but requires listing every function individually. For large libraries with hundreds of functions, this becomes impractical. The `--obj-skip` option allows skipping all functions from a given object file in one command.

**Usage**:
```bash
valgrind --tool=callgrind --obj-skip=/lib/x86_64-linux-gnu/libc.so.6 ./your_program
```

Multiple objects can be skipped by repeating the option:
```bash
valgrind --tool=callgrind \
    --obj-skip=/lib/x86_64-linux-gnu/libc.so.6 \
    --obj-skip=/lib/x86_64-linux-gnu/libpthread.so.0 \
    ./your_program
```
