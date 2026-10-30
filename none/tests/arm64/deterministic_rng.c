#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

enum {
   NZCV_ALL = 0xf0000000,
   NZCV_SUCCESS = 0,
   DRAW_COUNT = 2
};

#define REGISTER_SENTINEL UINT64_C(0x5555555555555555)

/* Use the generic register names so the test assembles without +rng. */
#define DRAW(name, sysreg) \
static void* name(void* unused) \
{ \
   (void)unused; \
   for (int i = 0; i < DRAW_COUNT; i++) { \
      uint64_t value = REGISTER_SENTINEL; \
      uint64_t nzcv; \
      __asm__ volatile("msr nzcv, %2; mrs %0, " sysreg "; mrs %1, nzcv" \
                       : "=&r"(value), "=&r"(nzcv) \
                       : "r"((uint64_t)NZCV_ALL) : "cc"); \
      assert((nzcv & NZCV_ALL) == NZCV_SUCCESS); \
      printf(#name ": %016llx\n", (unsigned long long)value); \
   } \
   return NULL; \
}

DRAW(rndr, "s3_3_c2_c4_0")
DRAW(rndrrs, "s3_3_c2_c4_1")

int main(void)
{
   void* (*draws[])(void*) = { rndr, rndrrs };
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
