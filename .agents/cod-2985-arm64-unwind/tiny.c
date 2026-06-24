#include <stdlib.h>
#include <string.h>
__attribute__((noinline)) long leaf(int n) {
    char *b = malloc(n);     /* PLT -> libc malloc */
    memset(b, 1, n);
    long acc = b[0] + b[n-1];
    free(b);
    return acc;              /* return into caller -- is this missed? */
}
__attribute__((noinline)) long caller(void){ return leaf(32) + 7; }
int main(void){ return (int)caller(); }
