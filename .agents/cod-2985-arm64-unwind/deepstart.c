#include <stdlib.h>
#include <string.h>
#include "callgrind.h"
char *volatile g;
__attribute__((noinline)) long inner(int n){
    CALLGRIND_START_INSTRUMENTATION;        /* start DEEP: inner/mid/outer/main seeded */
    char *p=(char*)malloc(n); g=p; memset(p,1,n);
    long a=p[0]+p[n-1]; free(p);
    return a;                               /* return crosses the seeded 'inner' frame */
}
__attribute__((noinline)) long mid(int n){ long r=inner(n); return r+(n&1); }
__attribute__((noinline)) long outer(int n){ long r=mid(n); return r+(n&2); }
int main(void){
    { char*p=(char*)malloc(64); g=p; free(p); }   /* warm arena before START (uninstrumented) */
    long r=outer(64);
    CALLGRIND_STOP_INSTRUMENTATION;
    return (int)(r & 0x7f);
}
