#include <stdlib.h>
#include <string.h>
char *volatile g;
__attribute__((noinline)) void warmup(void){ for(int i=0;i<5;i++){char*p=(char*)malloc(64);memset(p,i,64);g=p;free(p);} }
__attribute__((noinline)) long leaf(int n){ char*b=(char*)malloc(n); g=b; memset(b,1,n); long a=b[0]+b[n-1]; free(b); return a; }
__attribute__((noinline)) long rec(int d){ long a=leaf(32); if(d>0) a+=rec(d-1); return a; }
int main(void){ warmup(); return (int)rec(3); }
