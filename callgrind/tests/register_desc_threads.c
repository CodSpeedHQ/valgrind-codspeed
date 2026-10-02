// With --separate-threads=yes, a part has one section per thread: every
// section of the next dumped part carries the queued desc lines, and the part
// after it carries none of them.

#include <pthread.h>
#include <unistd.h>

#include "../callgrind.h"

static int to_main[2], to_worker[2];

// gives the calling thread a nonzero delta, so its section is not skipped
static unsigned long work(void)
{
   volatile unsigned long sink = 0;
   int i;

   for (i = 0; i < 100000; i++)
      sink += i;
   return sink;
}

static void *worker(void *arg)
{
   char c = 0;
   int i;

   for (i = 0; i < 2; i++) {
      work();
      write(to_main[1], &c, 1);
      read(to_worker[0], &c, 1);
   }
   return 0;
}

int main(void)
{
   pthread_t t;
   char c = 0;

   pipe(to_main);
   pipe(to_worker);
   pthread_create(&t, 0, worker, 0);

   work();
   read(to_main[0], &c, 1);
   CALLGRIND_REGISTER_DESC("Benchmark pid: 42");
   CALLGRIND_DUMP_STATS_AT("first");

   write(to_worker[1], &c, 1);
   work();
   read(to_main[0], &c, 1);
   CALLGRIND_DUMP_STATS_AT("second");

   write(to_worker[1], &c, 1);
   pthread_join(t, 0);
   return 0;
}
