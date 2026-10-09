/* prctl(PR_GET_AUXV) must return the program's own auxv, the one on its
   stack that getauxval() reads, not the auxv of the Valgrind tool binary
   the kernel actually exec'd, and otherwise behave like the kernel.

   Run natively, --supported reports whether the kernel knows PR_GET_AUXV
   (Linux >= 6.4) and --kernel-size prints the size the kernel returns.
   The test itself takes that kernel size as its only argument. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#ifndef PR_GET_AUXV
#define PR_GET_AUXV 0x41555856
#endif

static const char *base_name(const char *path)
{
   const char *slash = strrchr(path, '/');
   return slash ? slash + 1 : path;
}

static int has_only_byte(const void *start, size_t len, unsigned char byte)
{
   const unsigned char *p = start;
   for (size_t i = 0; i < len; i++)
      if (p[i] != byte)
         return 0;
   return 1;
}

static void print_error(const char *what, long res, int err,
                        int expected_err, const char *expected_name)
{
   printf("%s: %s\n", what,
          res == -1 && err == expected_err ? expected_name : "not rejected");
}

int main(int argc, char **argv, char **envp)
{
   if (argc > 1 && strcmp(argv[1], "--supported") == 0)
      return prctl(PR_GET_AUXV, 0, 0, 0, 0) < 0;
   if (argc > 1 && strcmp(argv[1], "--kernel-size") == 0) {
      printf("%ld\n", (long)prctl(PR_GET_AUXV, 0, 0, 0, 0));
      return 0;
   }
   if (argc != 2) {
      fprintf(stderr, "usage: %s KERNEL_SIZE\n", argv[0]);
      return 2;
   }
   long kernel_size = strtol(argv[1], NULL, 10);

   /* The auxv starts right after the NULL that ends envp. */
   char **env_end = envp;
   while (*env_end != NULL)
      env_end++;
   unsigned long *stack_auxv = (unsigned long *)(env_end + 1);
   size_t stack_auxv_size = 2 * sizeof(unsigned long);
   for (const unsigned long *p = stack_auxv; p[0] != AT_NULL; p += 2)
      stack_auxv_size += 2 * sizeof(unsigned long);

   unsigned long buf[512];
   memset(buf, 0xaa, sizeof(buf));
   long size = prctl(PR_GET_AUXV, buf, sizeof(buf), 0, 0);
   if (size < 0) {
      perror("prctl(PR_GET_AUXV)");
      return 1;
   }

   for (const unsigned long *p = buf; p[0] != AT_NULL; p += 2)
      if (p[0] == AT_EXECFN)
         printf("AT_EXECFN: %s\n", base_name((const char *)p[1]));

   printf("size: %s\n", size == kernel_size ? "same as the kernel's"
                                            : "differs from the kernel's");

   int is_size_plausible = (size_t)size >= stack_auxv_size
                           && (size_t)size <= sizeof(buf);
   int matches_stack = is_size_plausible
                       && memcmp(buf, stack_auxv, stack_auxv_size) == 0;
   printf("matches the auxv on the stack: %s\n", matches_stack ? "yes" : "no");

   const char *bytes = (const char *)buf;
   int is_zero_padded = is_size_plausible
      && has_only_byte(bytes + stack_auxv_size, size - stack_auxv_size, 0)
      && has_only_byte(bytes + size, sizeof(buf) - size, 0xaa);
   printf("zero padding up to the returned size: %s\n",
          is_zero_padded ? "yes" : "no");

   /* The kernel answers from the auxv it saved at exec, so later writes
      to the copy on the stack do not show. */
   unsigned long first_value = stack_auxv[1];
   stack_auxv[1] = ~first_value;
   unsigned long later[512];
   long later_size = prctl(PR_GET_AUXV, later, sizeof(later), 0, 0);
   stack_auxv[1] = first_value;
   int is_saved_copy = later_size == size
                       && memcmp(later, buf, stack_auxv_size) == 0;
   printf("stack auxv modified: %s\n",
          is_saved_copy ? "saved vector returned" : "modified vector returned");

   unsigned long small[4], fill;
   memset(small, 0xaa, sizeof(small));
   memset(&fill, 0xaa, sizeof(fill));
   long small_size = prctl(PR_GET_AUXV, small, 2 * sizeof(unsigned long), 0, 0);
   int is_truncated = small_size == size
                      && memcmp(small, buf, 2 * sizeof(unsigned long)) == 0
                      && small[2] == fill && small[3] == fill;
   printf("short buffer: %s\n",
          is_truncated ? "full size returned, rest untouched" : "wrong");

   long res;
   errno = 0;
   res = prctl(PR_GET_AUXV, buf, sizeof(buf), 1, 0);
   print_error("nonzero arg4", res, errno, EINVAL, "EINVAL");

   errno = 0;
   res = prctl(PR_GET_AUXV, buf, sizeof(buf), 0, 1);
   print_error("nonzero arg5", res, errno, EINVAL, "EINVAL");

   errno = 0;
   res = prctl(PR_GET_AUXV, (void *)8, sizeof(buf), 0, 0);
   print_error("unmapped buffer", res, errno, EFAULT, "EFAULT");

   void *read_only = mmap(NULL, sizeof(buf), PROT_READ,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (read_only == MAP_FAILED) {
      perror("mmap");
      return 1;
   }
   errno = 0;
   res = prctl(PR_GET_AUXV, read_only, sizeof(buf), 0, 0);
   print_error("read-only buffer", res, errno, EFAULT, "EFAULT");

   return 0;
}
