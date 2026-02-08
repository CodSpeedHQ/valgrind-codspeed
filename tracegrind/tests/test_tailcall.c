#include "tracegrind.h"

/*
 * Test: tail call optimization.
 *
 * chain_a() tail-calls chain_b(), which tail-calls chain_c().
 * At -O2, the compiler should optimize these into JMP instructions
 * rather than CALL+RET. Verifies tracegrind handles sibling calls.
 *
 * Call chain:  chain_a --(tail call)--> chain_b --(tail call)--> chain_c
 */

static int __attribute__((noinline)) chain_c(int n) { return n + 3; }

static int __attribute__((noinline)) chain_b(int n) { return chain_c(n + 2); }

static int __attribute__((noinline)) chain_a(int n) { return chain_b(n + 1); }

int main(void)
{
   volatile int input = 10;
   TRACEGRIND_ADD_MARKER("start");
   TRACEGRIND_START_INSTRUMENTATION;
   int result = chain_a(input);
   TRACEGRIND_STOP_INSTRUMENTATION;
   TRACEGRIND_ADD_MARKER("end");
   return result != 16;
}
