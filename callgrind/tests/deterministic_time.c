/* Properties of the virtual clock that hold on every host and compiler:
   the clocks agree with each other, sleeps move them forward instead of
   waiting, and identical work takes identical time. Absolute values are
   not pinned because the instruction count before main() depends on
   libc. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

enum {
   NS_PER_SEC = 1000000000,
   EPOCH_SECS = 1704067200, /* 2024-01-01T00:00:00Z */
   SLEEP_NS   = 100 * 1000 * 1000,
   /* Allowance for the instructions around a clock read. */
   SLACK_NS               = 1000 * 1000,
   WORK_ITERATIONS        = 100000,
   BACK_TO_BACK_MAX_TICKS = 64,
};

static void check(const char* name, int ok)
{
   printf("%s: %s\n", name, ok ? "ok" : "FAIL");
}

static int64_t now_ns(clockid_t clk)
{
   struct timespec ts;
   if (clock_gettime(clk, &ts) != 0) {
      perror("clock_gettime");
      return -1;
   }
   return ts.tv_sec * (int64_t)NS_PER_SEC + ts.tv_nsec;
}

static int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

static int within_slack(int64_t measured, int64_t expected)
{
   return measured >= expected && measured < expected + SLACK_NS;
}

static uint64_t read_tsc(void)
{
#if defined(__x86_64__)
   uint32_t lo, hi;
   __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
   return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
   uint64_t v;
   __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
   return v;
#else
   return 0;
#endif
}

static volatile uint64_t sink;

static int64_t timed_work(void)
{
   int64_t  start = now_ns(CLOCK_MONOTONIC);
   uint64_t acc   = 0;
   for (unsigned i = 0; i < WORK_ITERATIONS; i++)
      acc += (uint64_t)i * i;
   sink = acc;
   return now_ns(CLOCK_MONOTONIC) - start;
}

static void check_clocks(void)
{
   struct timeval tv;
   int64_t        mono = now_ns(CLOCK_MONOTONIC);

   check("realtime starts at the fixed epoch",
         now_ns(CLOCK_REALTIME) / NS_PER_SEC == EPOCH_SECS);
   check("gettimeofday agrees with realtime",
         gettimeofday(&tv, NULL) == 0 && tv.tv_sec == EPOCH_SECS);
   check("time agrees with realtime", time(NULL) == EPOCH_SECS);
   check("monotonic advances with work",
         now_ns(CLOCK_MONOTONIC) > mono &&
            within_slack(now_ns(CLOCK_MONOTONIC) - mono, 0));
   check("boottime agrees with monotonic",
         within_slack(now_ns(CLOCK_BOOTTIME) - mono, 0));
   check("process cputime agrees with monotonic",
         within_slack(now_ns(CLOCK_PROCESS_CPUTIME_ID) - mono, 0));
}

static void check_sleeps(void)
{
   struct timespec req = {0, SLEEP_NS};
   struct timespec deadline;
   int64_t         before;

   before = now_ns(CLOCK_MONOTONIC);
   check("nanosleep succeeds", nanosleep(&req, NULL) == 0);
   check("nanosleep advances the clock",
         within_slack(now_ns(CLOCK_MONOTONIC) - before, SLEEP_NS));

   clock_gettime(CLOCK_REALTIME, &deadline);
   deadline.tv_sec += 1;
   before = now_ns(CLOCK_REALTIME);
   check("absolute sleep succeeds",
         clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &deadline, NULL) == 0);
   check("absolute sleep reaches its deadline",
         within_slack(now_ns(CLOCK_REALTIME) - before, NS_PER_SEC));

   deadline.tv_sec -= 10;
   before = now_ns(CLOCK_REALTIME);
   check("sleep until a past deadline returns at once",
         clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &deadline, NULL) == 0 &&
            within_slack(now_ns(CLOCK_REALTIME) - before, 0));
}

static void check_tsc(void)
{
   struct timespec req         = {0, SLEEP_NS};
   uint64_t        tsc_before  = read_tsc();
   int64_t         mono_before = now_ns(CLOCK_MONOTONIC);
   int64_t         mono_after, tsc_delta;
   uint64_t        first, second;

   nanosleep(&req, NULL);
   mono_after = now_ns(CLOCK_MONOTONIC);
   tsc_delta  = (int64_t)(read_tsc() - tsc_before);
   check("tsc ticks once per virtual nanosecond",
         abs64(tsc_delta - (mono_after - mono_before)) < SLACK_NS);

   /* Both reads sit in one superblock; the second must still see the
      instructions between them. */
   first  = read_tsc();
   second = read_tsc();
   check("back-to-back tsc reads advance",
         second > first && second - first < BACK_TO_BACK_MAX_TICKS);
}

static void check_errors(void)
{
   struct timespec bad = {0, NS_PER_SEC};
   struct timespec ts;

   check("nanosleep rejects tv_nsec out of range",
         nanosleep(&bad, NULL) == -1 && errno == EINVAL);
   check("nanosleep rejects an unmapped request",
         syscall(SYS_nanosleep, (void*)8, NULL) == -1 && errno == EFAULT);
   check("clock_gettime rejects an unmapped result",
         syscall(SYS_clock_gettime, CLOCK_MONOTONIC, (void*)8) == -1 &&
            errno == EFAULT);
   check("unknown clock ids reach the kernel",
         clock_gettime(12345, &ts) == -1 && errno == EINVAL);
}

int main(void)
{
   check_clocks();
   check_sleeps();
   check_tsc();
   timed_work(); /* resolve PLT entries outside the comparison */
   check("identical work takes identical time", timed_work() == timed_work());
   check_errors();
   return 0;
}
