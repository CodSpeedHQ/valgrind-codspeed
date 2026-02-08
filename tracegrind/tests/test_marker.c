#include "tracegrind.h"

static int compute(int n)
{
   int sum = 0;
   for (int i = 0; i < n; i++)
      sum += i * i;
   return sum;
}

int main(void)
{
   TRACEGRIND_ADD_MARKER("start-work");
   int result = compute(1000);
   TRACEGRIND_ADD_MARKER("end-work");
   return result == 0;
}
