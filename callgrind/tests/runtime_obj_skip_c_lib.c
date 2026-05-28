/* Library that lives in a separate ELF object so the main binary
 * can register its path for runtime obj-skip.
 *
 * skipme_run() flips instrumentation on from *inside* the skipped
 * object, then calls skipme_func. This is the trigger for the
 * `current_state.cxt == 0` push path in setup_bbcc: the very first
 * BB after instrumentation start lives in a skipped object, so the
 * (cxt==0) clause force-pushes a skipped fn as the new top context
 * and it leaks into the dump as a top-level fn= block. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include "../callgrind.h"

volatile long sink;

/* Returns the path of the .so this code is compiled into. Done inside
 * the lib because taking &skipme_run from the main binary yields a
 * PLT stub address whose dladdr resolves to the main binary, not the
 * lib — which would register the wrong object for skip. */
const char* skipme_self_path(void)
{
    Dl_info info;
    if (dladdr((void*)&skipme_self_path, &info) == 0) return 0;
    return info.dli_fname;
}

__attribute__((noinline))
void skipme_func(int n)
{
    for (int i = 0; i < n; i++) sink += i;
}

__attribute__((noinline))
void skipme_run(int n)
{
    CALLGRIND_START_INSTRUMENTATION;
    skipme_func(n);
    CALLGRIND_STOP_INSTRUMENTATION;
}
