/* Minimal C reproducer for the runtime obj-skip leak: a fn from a
 * skipped object ends up as a top-level fn= block in the callgrind
 * output when it is the first BB instrumented after START.
 *
 * Strategy: register the lib for skip, then call into the lib BEFORE
 * starting instrumentation. The lib itself calls
 * CALLGRIND_START_INSTRUMENTATION mid-function, so the first BB
 * processed by callgrind lives in the skipped object — which trips
 * the (cxt == 0) push_cxt path that ignores the skip flag. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include "../callgrind.h"

extern void skipme_run(int n);

int main(void)
{
    Dl_info info;
    if (dladdr((void*)skipme_run, &info) == 0 || !info.dli_fname) {
        fprintf(stderr, "dladdr failed\n");
        return 1;
    }
    CALLGRIND_ADD_OBJ_SKIP(info.dli_fname);

    skipme_run(1000);

    return 0;
}
