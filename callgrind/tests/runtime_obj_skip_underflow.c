/* Driver for the underflow-channel obj-skip leak reproducer. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include "../callgrind.h"

extern void skipme_run(int depth);

int main(void)
{
    Dl_info info;
    if (dladdr((void*)skipme_run, &info) == 0 || !info.dli_fname) {
        fprintf(stderr, "dladdr failed\n");
        return 1;
    }
    CALLGRIND_ADD_OBJ_SKIP(info.dli_fname);

    skipme_run(5);

    return 0;
}
