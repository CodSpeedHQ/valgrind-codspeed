/* Minimal C reproducer for the runtime obj-skip leak: a fn from a
 * skipped object ends up as a top-level fn= block in the callgrind
 * output when it is the first BB instrumented after START.
 *
 * Strategy: register the lib for skip, then call into the lib BEFORE
 * starting instrumentation. The lib itself calls
 * CALLGRIND_START_INSTRUMENTATION mid-function, so the first BB
 * processed by callgrind lives in the skipped object — which trips
 * the (cxt == 0) push_cxt path that ignores the skip flag. */

#include <stdio.h>
#include "../callgrind.h"

extern void skipme_run(int n);
extern const char* skipme_self_path(void);

int main(void)
{
    /* Resolve the lib's path from *inside* the lib: taking &skipme_run
     * here gives a PLT stub in the main binary, whose dladdr returns
     * the main binary path — registering the wrong object for skip. */
    const char* path = skipme_self_path();
    if (!path) {
        fprintf(stderr, "skipme_self_path failed\n");
        return 1;
    }
    CALLGRIND_ADD_OBJ_SKIP(path);

    skipme_run(1000);

    return 0;
}
