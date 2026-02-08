#include "tracegrind.h"

/* Inner inlined function.
 * With --read-inline-info=yes, should produce ENTER_INLINED / EXIT_INLINED
 * events with fn=inner_inline. */
static inline __attribute__((always_inline)) int inner_inline(int a)
{
   int result;
   if (a > 0) {
      result = a * 3;
   } else {
      result = a + 1;
   }
   return result;
}

/* Outer inlined function - calls inner_inline.
 * Should produce ENTER_INLINED events for both outer_inline and inner_inline,
 * showing nested inline transitions.
 * Uses volatile stores in both branches to prevent the compiler from
 * converting the if-else to a branchless cmov. */
static inline __attribute__((always_inline)) int outer_inline(int a, int b)
{
   volatile int x;
   if (a > b) {
      x = a - b;
   } else {
      x = b - a;
   }
   int y = inner_inline(x);
   return y + a;
}

/* Non-inlined caller */
static int __attribute__((noinline)) caller(int n)
{
   volatile int x = n;
   return outer_inline(x, x + 1);
}

int main(void)
{
   volatile int input = 5;
   TRACEGRIND_ADD_MARKER("start");
   TRACEGRIND_START_INSTRUMENTATION;
   int result = caller(input);
   TRACEGRIND_STOP_INSTRUMENTATION;
   TRACEGRIND_ADD_MARKER("end");
   /* caller(5) -> outer_inline(5, 6): x=1, inner_inline(1)=3, 3+5=8 */
   return result != 8;
}
