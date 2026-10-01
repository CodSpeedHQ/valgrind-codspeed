// CALLGRIND_REGISTER_DESC queues desc lines for the next dumped part only, and
// a fork child does not inherit the lines its parent queued.

#include <sys/wait.h>
#include <unistd.h>

#include "../callgrind.h"

int main(void)
{
   pid_t child;

   CALLGRIND_REGISTER_DESC("Benchmark pid: 42");
   CALLGRIND_REGISTER_DESC("Other: a\nb");
   CALLGRIND_DUMP_STATS_AT("first");

   CALLGRIND_DUMP_STATS_AT("second");

   CALLGRIND_REGISTER_DESC("Pending: kept");
   child = fork();
   if (child == 0) {
      CALLGRIND_DUMP_STATS_AT("child");
      _exit(0);
   }
   waitpid(child, 0, 0);
   CALLGRIND_DUMP_STATS_AT("third");

   return 0;
}
