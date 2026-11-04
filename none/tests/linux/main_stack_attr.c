/* glibc's pthread_getattr_np reports the main thread stack as
   RLIMIT_STACK minus the startup data above __libc_stack_end.  Runtimes
   such as CPython size their stack guard from it, so the range must
   cover SP and be close to the limit, as natively.

   "main_stack_attr N" checks itself, then re-executes with N extra
   arguments to grow the startup pointer tables. */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

enum {
   MIB = 1024 * 1024,
   STACK_LIMIT = 8 * MIB,
   /* The startup image above the initial SP is a few KiB natively. */
   MAX_STARTUP_OVERHEAD = 1 * MIB,
};

/* Argument of the re-executed child; a numeric argument marks the
   parent, so the child does not re-execute. */
static char CHILD_ARG[] = "x";

static int reexec_with_args(const char *self, long count)
{
   char **argv = calloc(count + 2, sizeof(*argv));
   long i;

   if (argv == NULL)
      return 1;
   argv[0] = (char *)self;
   for (i = 0; i < count; i++)
      argv[i + 1] = CHILD_ARG;
   execv(self, argv);
   perror("execv");
   return 1;
}

int main(int argc, char **argv)
{
   struct rlimit rl;
   pthread_attr_t attr;
   void *stack_addr;
   size_t stack_size;
   rlim_t limit = STACK_LIMIT;
   uintptr_t sp = (uintptr_t)&rl;

   if (getrlimit(RLIMIT_STACK, &rl) != 0) {
      perror("getrlimit");
      return 1;
   }
   if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < limit)
      limit = rl.rlim_max;
   rl.rlim_cur = limit;
   if (setrlimit(RLIMIT_STACK, &rl) != 0) {
      perror("setrlimit");
      return 1;
   }

   if (pthread_getattr_np(pthread_self(), &attr) != 0
       || pthread_attr_getstack(&attr, &stack_addr, &stack_size) != 0) {
      fprintf(stderr, "pthread_getattr_np failed\n");
      return 1;
   }
   pthread_attr_destroy(&attr);

   if (sp < (uintptr_t)stack_addr
       || sp >= (uintptr_t)stack_addr + stack_size) {
      printf("SP %#lx outside reported stack [%#lx, %#lx)\n",
             (unsigned long)sp, (unsigned long)stack_addr,
             (unsigned long)((uintptr_t)stack_addr + stack_size));
      return 1;
   }
   if (stack_size + MAX_STARTUP_OVERHEAD < limit) {
      printf("reported stack size %zu KiB, limit %lu KiB\n",
             stack_size / 1024, (unsigned long)(limit / 1024));
      return 1;
   }

   printf("Main thread stack attributes cover SP and match RLIMIT_STACK.\n");

   if (argc == 2 && strcmp(argv[1], CHILD_ARG) != 0) {
      fflush(stdout);
      return reexec_with_args(argv[0], strtol(argv[1], NULL, 10));
   }
   return 0;
}
