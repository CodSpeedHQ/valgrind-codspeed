# CodSpeed Valgrind Changelog

This file documents changes made to Valgrind for CodSpeed integration, beyond the baseline Valgrind distribution.

## Features


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
