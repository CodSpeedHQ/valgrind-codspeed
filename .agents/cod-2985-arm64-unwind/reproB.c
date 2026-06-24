#include <stdio.h>
__attribute__((noinline)) long leaf_pure(int n) {
    long acc = 0;
    for (int i = 0; i < n; i++) acc += (i*31 ^ (i>>2));
    return acc;
}
__attribute__((noinline)) long build_node(int depth, int maxd) {
    long acc = leaf_pure(256);
    if (depth < maxd) for (int i = 0; i < 3; i++) acc += build_node(depth+1, maxd);
    return acc;
}
int main(void){ long t=0; for(int i=0;i<200;i++) t+=build_node(0,5); printf("%ld\n",t); return 0; }
