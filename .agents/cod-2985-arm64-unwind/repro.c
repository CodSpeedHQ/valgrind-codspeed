#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* deep function that allocates via libc malloc (reached through PLT) */
__attribute__((noinline)) long inner_sort_like(int n) {
    /* allocate + touch, like driftsort's scratch buffer */
    char *buf = (char*)malloc(n);
    memset(buf, n & 0xff, n);
    long acc = 0;
    for (int i = 0; i < n; i++) acc += buf[i];
    free(buf);
    return acc;
}

__attribute__((noinline)) long build_node(int depth, int maxd) {
    long acc = inner_sort_like(256);
    if (depth < maxd) {
        for (int i = 0; i < 3; i++)
            acc += build_node(depth + 1, maxd);
    }
    return acc;
}

int main(void) {
    long total = 0;
    for (int i = 0; i < 200; i++) total += build_node(0, 5);
    printf("%ld\n", total);
    return 0;
}
