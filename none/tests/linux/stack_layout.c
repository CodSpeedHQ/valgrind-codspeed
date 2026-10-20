#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum { PROBE_ARGC = 3, MAX_VALUES = 8, MAX_LENGTH = 4096, NAME_SIZE = 32 };

static const struct {
   int argc, arglen, envc, envlen;
} cases[] = {
   { 0, 0, 0, 0 },
   { 1, 0, 0, 0 },
   { 1, 15, 0, 0 },
   { 1, 16, 0, 0 },
   { 1, MAX_LENGTH, 0, 0 },
   { MAX_VALUES, 0, 0, 0 },
   { 0, 0, 1, 0 },
   { 0, 0, 1, 15 },
   { 0, 0, 1, 16 },
   { 0, 0, 1, MAX_LENGTH },
   { 0, 0, MAX_VALUES, 0 },
   { MAX_VALUES, MAX_LENGTH, MAX_VALUES, MAX_LENGTH },
};

static const char env_prefix[] = "STACK_LAYOUT_";
static const char executable[] = "./stack_layout";

static void fill_value(char *value, int length, int index)
{
   static const char pattern[] = "a = Z";
   int i;
   for (i = 0; i < length; i++)
      value[i] = pattern[(i + index) % (sizeof(pattern) - 1)];
   value[length] = '\0';
}

static void probe(int argc, char **argv, char **envp)
{
   int c = atoi(argv[2]);
   int i, found = 0;
   char expected[MAX_LENGTH + 1];
   char name[NAME_SIZE];
   uintptr_t initial_sp;

   assert(c >= 0 && c < sizeof(cases) / sizeof(cases[0]));
   assert(argc == PROBE_ARGC + cases[c].argc);
   assert(strcmp(argv[0], executable) == 0);
   assert(argv[argc] == NULL);
   for (i = 0; i < cases[c].argc; i++) {
      fill_value(expected, cases[c].arglen, i);
      assert(strcmp(argv[PROBE_ARGC + i], expected) == 0);
   }
   assert(getenv("LC_ALL") && strcmp(getenv("LC_ALL"), "C") == 0);
   for (i = 0; envp[i]; i++) {
      const char *value;
      if (strncmp(envp[i], env_prefix, sizeof(env_prefix) - 1) != 0)
         continue;
      assert(found < cases[c].envc);
      snprintf(name, sizeof(name), "%s%d", env_prefix, found);
      fill_value(expected, cases[c].envlen, found);
      assert(strncmp(envp[i], name, strlen(name)) == 0);
      assert(envp[i][strlen(name)] == '=');
      assert(strcmp(envp[i] + strlen(name) + 1, expected) == 0);
      value = getenv(name);
      assert(value && strcmp(value, expected) == 0);
      found++;
   }
   assert(found == cases[c].envc);

   /* Linux starts with argc immediately before argv. libc passes the original
      argv to main, so this recovers the entry SP without arch-specific asm. */
   initial_sp = (uintptr_t)argv - sizeof(*argv);
   assert(write(STDOUT_FILENO, &initial_sp, sizeof(initial_sp))
          == sizeof(initial_sp));
}

static uintptr_t run_case(unsigned c)
{
   char arg_values[MAX_VALUES][MAX_LENGTH + 1];
   char env_values[MAX_VALUES][NAME_SIZE + MAX_LENGTH + 1];
   char *args[PROBE_ARGC + MAX_VALUES + 1];
   char *env[MAX_VALUES + 2];
   char case_number[NAME_SIZE];
   int i, fds[2], status;
   pid_t child;
   FILE *output;
   uintptr_t initial_sp;

   snprintf(case_number, sizeof(case_number), "%02u", c);
   args[0] = (char *)executable;
   args[1] = "--probe";
   args[2] = case_number;
   for (i = 0; i < cases[c].argc; i++) {
      fill_value(arg_values[i], cases[c].arglen, i);
      args[PROBE_ARGC + i] = arg_values[i];
   }
   args[PROBE_ARGC + cases[c].argc] = NULL;
   env[0] = "LC_ALL=C";
   for (i = 0; i < cases[c].envc; i++) {
      int prefix_length = snprintf(env_values[i], NAME_SIZE, "%s%d=",
                                   env_prefix, i);
      fill_value(env_values[i] + prefix_length, cases[c].envlen, i);
      env[i + 1] = env_values[i];
   }
   env[cases[c].envc + 1] = NULL;

   assert(pipe(fds) == 0);
   child = fork();
   assert(child >= 0);
   if (child == 0) {
      close(fds[0]);
      assert(dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
      close(fds[1]);
      execve(executable, args, env);
      perror("execve");
      _exit(1);
   }
   close(fds[1]);
   output = fdopen(fds[0], "r");
   assert(output);
   assert(fread(&initial_sp, sizeof(initial_sp), 1, output) == 1);
   assert(fgetc(output) == EOF);
   fclose(output);
   assert(waitpid(child, &status, 0) == child);
   assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
   return initial_sp;
}

int main(int argc, char **argv, char **envp)
{
   uintptr_t initial_sp;
   unsigned c;

   if (argc >= PROBE_ARGC && strcmp(argv[1], "--probe") == 0) {
      probe(argc, argv, envp);
      return 0;
   }
   assert(argc == 1);
   initial_sp = run_case(0);
   for (c = 1; c < sizeof(cases) / sizeof(cases[0]); c++) {
      uintptr_t actual = run_case(c);
      if (actual != initial_sp) {
         fprintf(stderr, "case %u: initial SP changed from %#" PRIxPTR
                         " to %#" PRIxPTR "\n", c, initial_sp, actual);
         return 1;
      }
   }
   puts("Initial SP is fixed; arguments and environment are unchanged.");
   return 0;
}
