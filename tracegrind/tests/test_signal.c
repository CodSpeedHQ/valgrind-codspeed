#include "tracegrind.h"
#include <signal.h>
#include <string.h>

/*
 * Test: signal handler interrupting normal function execution.
 *
 * caller() raises SIGALRM to itself. The signal handler (handler_fn)
 * runs, then execution returns to caller(). Verifies the call stack
 * is properly maintained across signal delivery.
 */

static volatile sig_atomic_t got_signal = 0;

static void __attribute__((noinline)) handler_fn(int sig)
{
   (void)sig;
   got_signal = 1;
}

static int __attribute__((noinline)) caller(int n)
{
   volatile int x = n;
   raise(SIGALRM);
   return x + 1;
}

int main(void)
{
   struct sigaction sa;
   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = handler_fn;
   sigaction(SIGALRM, &sa, NULL);

   volatile int input = 5;
   TRACEGRIND_ADD_MARKER("start");
   TRACEGRIND_START_INSTRUMENTATION;
   int result = caller(input);
   TRACEGRIND_STOP_INSTRUMENTATION;
   TRACEGRIND_ADD_MARKER("end");
   return (result != 6) || !got_signal;
}
