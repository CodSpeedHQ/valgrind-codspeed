#include "tracegrind.h"

/* Force inlining - these should NOT appear as ENTER/EXIT in the trace */
static inline __attribute__((always_inline)) int inlined_add(int a, int b)
{
   return a + b;
}

static inline __attribute__((always_inline)) int inlined_mul(int a, int b)
{
   return a * b;
}

/* Prevent inlining - these SHOULD appear as ENTER/EXIT in the trace */
static int __attribute__((noinline)) not_inlined_work(int n)
{
   return inlined_add(n, inlined_mul(n, 2));
}

int main(void)
{
   TRACEGRIND_ADD_MARKER("start");
   TRACEGRIND_START_INSTRUMENTATION;
   int result = not_inlined_work(5);
   TRACEGRIND_STOP_INSTRUMENTATION;
   TRACEGRIND_ADD_MARKER("end");

   return result != 15;
}
