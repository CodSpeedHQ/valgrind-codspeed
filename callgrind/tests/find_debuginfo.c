/* Exercise VG_(find_DebugInfo) including the filename fallback for
   executable code that lives outside the section named ".text".

   Functions placed in orphan sections "warm_code" and "cold_code" end up
   in their own ELF sections (PROGBITS, AX) inside the same R-E LOAD as
   .text but *outside* di->text_avma/text_size. Calls into them force
   VG_(find_DebugInfo) past the primary text-range check and into the
   am_find_nsegment + filename match path added in commit 938e424b1. */

static int hot_fn(int x) { return x + 1; }

__attribute__((noinline, section("warm_code")))
static int warm_fn(int x) { return x * 3 + 7; }

__attribute__((noinline, section("cold_code")))
static int cold_fn(int x) { return x ^ 0x5a; }

int main(void)
{
   int y = 0;
   for (int i = 0; i < 1000; i++)
      y = hot_fn(y);
   y = warm_fn(y);
   y = cold_fn(y);
   return y & 0xff;
}
