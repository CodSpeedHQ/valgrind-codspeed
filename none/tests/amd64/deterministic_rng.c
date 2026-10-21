#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

enum {
   STATUS_FLAGS = 0x8d5,
   DRAW_COUNT = 2
};

#define REGISTER_SENTINEL UINT64_C(0x5555555555555555)

#define DRAW(name, instruction, operand) \
static void* name(void* unused) \
{ \
   (void)unused; \
   for (int i = 0; i < DRAW_COUNT; i++) { \
      uint64_t value = REGISTER_SENTINEL; \
      uint64_t flags; \
      __asm__ volatile("pushq $0x8d7; popfq; " instruction " " operand \
                       "; pushfq; popq %1" \
                       : "+r"(value), "=&r"(flags) : : "cc", "memory"); \
      assert((flags & STATUS_FLAGS) == 1); \
      printf(#name ": %016llx\n", (unsigned long long)value); \
   } \
   return NULL; \
}

DRAW(rdrand16, "rdrand", "%w0")
DRAW(rdrand32, "rdrand", "%k0")
DRAW(rdrand64, "rdrand", "%q0")
DRAW(rdseed16, "rdseed", "%w0")
DRAW(rdseed32, "rdseed", "%k0")
DRAW(rdseed64, "rdseed", "%q0")

int main(void)
{
   void* (*draws[])(void*) = {
      rdrand16, rdrand32, rdrand64, rdseed16, rdseed32, rdseed64
   };
   /* Fresh threads keep expected bytes independent of libc startup draws. */
   for (unsigned int i = 0; i < sizeof(draws) / sizeof(draws[0]); i++) {
      pthread_t thread;
      int err = pthread_create(&thread, NULL, draws[i], NULL);
      assert(err == 0);
      err = pthread_join(thread, NULL);
      assert(err == 0);
   }
   return 0;
}
