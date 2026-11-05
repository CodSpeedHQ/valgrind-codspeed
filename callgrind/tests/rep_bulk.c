/*--------------------------------------------------------------------*/
/*--- Regression test for the CodSpeed REP bulk cost model.        ---*/
/*--------------------------------------------------------------------*/

/* Each case is one callgrind part: ZERO_STATS, one rep instruction,
 * DUMP_STATS. The post-processing script checks the emitted counters and
 * that no new event names appear, with the cache geometry pinned by the
 * .vgtest file (64 byte lines). */


#if defined(__x86_64__)

#include "../callgrind.h"

#define COPY_SZ  10240u  /* 160 x 64B lines */
#define BACK_SZ   4096u
#define STOS_SZ   4096u
#define SELF_SZ   4096u
#define FLUSH_SZ (256u * 1024u)

static unsigned char srcbuf[COPY_SZ + 128] __attribute__((aligned(64)));
static unsigned char dstbuf[COPY_SZ + 128] __attribute__((aligned(64)));
static volatile unsigned char flushbuf[FLUSH_SZ] __attribute__((aligned(64)));

/* Evict the source/destination lines from D1. */
static void flush_d1(void)
{
    unsigned i;
    for (i = 0; i < FLUSH_SZ; i += 64)
        flushbuf[i] = (unsigned char)i;
}

static int verify_copy(void)
{
    unsigned i;
    for (i = 0; i < COPY_SZ; i++)
        if (dstbuf[i] != srcbuf[i])
            return 0;
    return 1;
}

/* Read every line of both copy buffers, so they are the most recently used
 * lines in D1.  The compiler may lower buffer initialization to a libc call
 * whose first use runs lazy symbol binding in ld.so; those loads can evict
 * buffer lines when the copy buffers already fill most ways of a set.  Every
 * load feeds the sink: Valgrind drops loads whose result is unused. */
static volatile unsigned warm_sink;

__attribute__((noinline))
static void warm_buffers(void)
{
    unsigned i, acc = 0;
    for (i = 0; i < COPY_SZ; i += 64)
        acc += srcbuf[i] + dstbuf[i];
    warm_sink = acc;
}

__attribute__((noinline))
static void measure_copy(unsigned char *d, const unsigned char *s, unsigned long n)
{
    CALLGRIND_ZERO_STATS;
    __asm__ __volatile__("rep movsb"
                         : "+D"(d), "+S"(s), "+c"(n)
                         : "a"(0u)
                         : "memory");
    CALLGRIND_DUMP_STATS;
}

int main(void)
{
    unsigned char *d;
    const unsigned char *s;
    unsigned long n;
    unsigned i;

    for (i = 0; i < COPY_SZ; i++) {
        srcbuf[i] = (unsigned char)(i * 7u);
        dstbuf[i] = 0;
    }
    /* Make both copy buffers D1-resident before the first measured part so
     * the "hot" cases really have no misses. */
    warm_buffers();

    /* part 0: one-byte forward copy (single element) */
    measure_copy(dstbuf, srcbuf, 1);

    /* part 1: hot 10 KiB forward copy, both buffers 64B-aligned */
    measure_copy(dstbuf, srcbuf, COPY_SZ);
    if (!verify_copy())
        return 2;

    /* part 2: zero-length rep (no element may be charged) */
    measure_copy(dstbuf, srcbuf, 0);
    if (!verify_copy())
        return 4;

    /* part 3: backward overlapping move (memmove pattern) */
    for (i = 0; i < BACK_SZ; i++)
        dstbuf[i] = (unsigned char)(i * 13u + 1u);
    d = dstbuf + 8 + BACK_SZ - 1; s = dstbuf + BACK_SZ - 1; n = BACK_SZ;
    CALLGRIND_ZERO_STATS;
    __asm__ __volatile__("std\n\trep movsb\n\tcld"
                         : "+D"(d), "+S"(s), "+c"(n)
                         : "a"(0u)
                         : "memory", "cc");
    CALLGRIND_DUMP_STATS;
    for (i = 0; i < BACK_SZ; i++)
        if (dstbuf[i + 8] != (unsigned char)(i * 13u + 1u))
            return 5;

    /* part 4: hot rep stosb */
    d = dstbuf; s = srcbuf; n = STOS_SZ;
    CALLGRIND_ZERO_STATS;
    __asm__ __volatile__("rep stosb"
                         : "+D"(d), "+S"(s), "+c"(n)
                         : "a"((unsigned char)0x5a)
                         : "memory");
    CALLGRIND_DUMP_STATS;
    for (i = 0; i < STOS_SZ; i++)
        if (dstbuf[i] != 0x5a)
            return 6;

    /* part 5: self copy (source and destination are the same buffer) */
    measure_copy(dstbuf, dstbuf, SELF_SZ);
    for (i = 0; i < SELF_SZ; i++)
        if (dstbuf[i] != 0x5a)
            return 7;

    /* part 6: cold 10 KiB forward copy after flushing D1 */
    flush_d1();
    measure_copy(dstbuf, srcbuf, COPY_SZ);
    if (!verify_copy())
        return 3;

    return 0;
}

#else /* !__x86_64__ */

int main(void)
{
    return 0;
}

#endif
