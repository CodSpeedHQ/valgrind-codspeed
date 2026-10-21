/* libc startup draws advance the main thread's stream. Use fresh threads
   for libc-independent expected bytes; AT_RANDOM has its own stream. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

enum {
   AT_RANDOM_LEN = 16,
   GETRANDOM_LEN = 32,
   URANDOM_LEN = 32,
   URANDOM_DUP_LEN = 8,
   READV_PART_LEN = 8,
   READV_PARTS = 2
};

static void fail(const char* name, int err)
{
   printf("%s: error %d\n", name, err);
   exit(1);
}

static void print_hex(const char* label, const unsigned char* p, size_t n)
{
   printf("%s: ", label);
   for (size_t i = 0; i < n; i++)
      printf("%02x", p[i]);
   printf("\n");
}

static void getrandom_full(unsigned char* buf, size_t n)
{
   size_t got = 0;
   while (got < n) {
      ssize_t r = getrandom(buf + got, n - got, 0);
      if (r < 0) {
         if (errno == EINTR)
            continue;
         fail("getrandom", errno);
      }
      got += (size_t)r;
   }
}

static void read_full(const char* name, int fd, unsigned char* buf, size_t n)
{
   size_t got = 0;
   while (got < n) {
      ssize_t r = read(fd, buf + got, n - got);
      if (r < 0) {
         if (errno == EINTR)
            continue;
         fail(name, errno);
      }
      if (r == 0)
         fail(name, EIO);
      got += (size_t)r;
   }
}

static int open_urandom(const char* name)
{
   int fd = open("/dev/urandom", O_RDONLY);
   if (fd < 0)
      fail(name, errno);
   return fd;
}

static void expect_errno(const char* label, ssize_t r, int want)
{
   if (r < 0 && errno == want)
      printf("%s: ok\n", label);
   else
      printf("%s: r=%zd errno=%d, want errno %d\n", label, r, errno, want);
}

static void expect_count(const char* label, ssize_t r, ssize_t want)
{
   if (r == want)
      printf("%s: ok\n", label);
   else
      printf("%s: r=%zd errno=%d, want %zd\n", label, r, errno, want);
}

enum {
   BAD_GRND_FLAG = 0x80,
   BAD_RWF_FLAG = 0x40000000,
   ATOMIC_RWF_FLAG = 0x40,
   TOO_MANY_IOVS = 1025,
   WORD = sizeof(size_t)
};

/* Served syscalls must fail like the kernel: EFAULT on a read-only
   buffer, EBADF on descriptors not opened for reading, EINVAL on bad
   flags, vector counts and negative offsets. Vector reads copy the
   vector first and report a partial count when a later buffer faults. */
static void check_errors(void)
{
   long page = sysconf(_SC_PAGESIZE);
   unsigned char* ro = mmap(NULL, page, PROT_READ,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   unsigned char buf[1];
   int fd;

   if (ro == MAP_FAILED)
      fail("mmap", errno);
   expect_errno("getrandom_readonly", getrandom(ro, page, 0), EFAULT);
   fd = open_urandom("urandom_readonly");
   expect_errno("urandom_readonly", read(fd, ro, page), EFAULT);
   close(fd);
   munmap(ro, page);

   fd = open("/dev/urandom", O_WRONLY);
   if (fd < 0)
      fail("urandom_wronly", errno);
   expect_errno("urandom_wronly", read(fd, buf, sizeof(buf)), EBADF);
   close(fd);

   fd = open("/dev/urandom", O_PATH);
   if (fd < 0)
      fail("urandom_opath", errno);
   expect_errno("urandom_opath", read(fd, buf, sizeof(buf)), EBADF);
   close(fd);

   expect_errno("getrandom_badflag", getrandom(buf, sizeof(buf), BAD_GRND_FLAG),
                EINVAL);
   expect_errno("getrandom_random_insecure",
                getrandom(buf, sizeof(buf), GRND_RANDOM | GRND_INSECURE),
                EINVAL);

   static struct iovec many[TOO_MANY_IOVS];
   struct iovec self[1];
   struct iovec partial[2];
   size_t words[WORD];
   int i;

   fd = open_urandom("readv_errors");
   for (i = 0; i < TOO_MANY_IOVS; i++) {
      many[i].iov_base = buf;
      many[i].iov_len = 0;
   }
   expect_errno("readv_too_many", readv(fd, many, TOO_MANY_IOVS), EINVAL);
   expect_errno("readv_negative_count",
                syscall(SYS_readv, fd, many, (unsigned long)-1), EINVAL);
   many[0].iov_len = (size_t)-1;
   expect_errno("readv_negative_len", readv(fd, many, 1), EINVAL);

   self[0].iov_base = &self[0].iov_len;
   self[0].iov_len = WORD;
   expect_count("readv_self_overlap", readv(fd, self, 1), WORD);

   partial[0].iov_base = words;
   partial[0].iov_len = WORD;
   partial[1].iov_base = (void*)WORD;
   partial[1].iov_len = WORD;
   expect_count("readv_partial", readv(fd, partial, 2), WORD);

   expect_errno("pread_negative_offset", pread(fd, words, WORD, -2), EINVAL);
   expect_errno("preadv_negative_offset", preadv(fd, partial, 1, -2), EINVAL);
   expect_count("preadv2_current_offset", preadv2(fd, partial, 1, -1, 0), WORD);

   expect_errno("preadv2_badflag",
                syscall(SYS_preadv2, fd, partial, 1, 0UL, 0UL, BAD_RWF_FLAG),
                EOPNOTSUPP);
   expect_errno("preadv2_atomic",
                syscall(SYS_preadv2, fd, partial, 1, 0UL, 0UL, ATOMIC_RWF_FLAG),
                EOPNOTSUPP);
   expect_errno("preadv2_badflag_negative_len",
                syscall(SYS_preadv2, fd, many, 1, 0UL, 0UL, BAD_RWF_FLAG),
                EINVAL);
   expect_errno("preadv2_badflag_negative_offset",
                preadv2(fd, partial, 1, -2, BAD_RWF_FLAG), EINVAL);
   expect_count("preadv2_dsync",
                syscall(SYS_preadv2, fd, partial, 1, 0UL, 0UL, RWF_DSYNC),
                WORD);
   expect_count("preadv2_badflag_zero_count",
                syscall(SYS_preadv2, fd, partial, 0, 0UL, 0UL, BAD_RWF_FLAG),
                0);
   partial[0].iov_len = 0;
   expect_count("preadv2_badflag_empty",
                syscall(SYS_preadv2, fd, partial, 1, 0UL, 0UL, BAD_RWF_FLAG),
                0);
   close(fd);
}

typedef void (*draw_fn)(unsigned char* buf);

static void draw_getrandom(unsigned char* buf)
{
   getrandom_full(buf, GETRANDOM_LEN);
}

static void draw_urandom(unsigned char* buf)
{
   int fd = open_urandom("urandom");
   struct iovec iov = { buf, URANDOM_LEN };
   /* Rejection must leave the expected bytes below unchanged. */
   if (syscall(SYS_preadv2, fd, &iov, 1, 0UL, 0UL, BAD_RWF_FLAG) != -1
       || errno != EOPNOTSUPP)
      fail("preadv2_badflag_stream", errno);
   read_full("urandom", fd, buf, URANDOM_LEN);
   close(fd);
}

static void draw_urandom_dup(unsigned char* buf)
{
   int fd = open_urandom("urandom_dup");
   int fd2 = dup(fd);
   if (fd2 < 0)
      fail("urandom_dup", errno);
   close(fd);
   read_full("urandom_dup", fd2, buf, URANDOM_DUP_LEN);
   close(fd2);
}

static void draw_readv(unsigned char* buf)
{
   int fd = open_urandom("readv");
   struct iovec iov[READV_PARTS] = {
      { buf, READV_PART_LEN },
      { buf + READV_PART_LEN, READV_PART_LEN },
   };
   size_t total = READV_PARTS * READV_PART_LEN;
   ssize_t r = readv(fd, iov, READV_PARTS);
   if (r < 0)
      fail("readv", errno);
   if ((size_t)r != total)
      fail("readv", EIO);
   close(fd);
}

typedef struct {
   draw_fn draw;
   unsigned char* buf;
} draw_job;

static void* draw_thread(void* arg)
{
   draw_job* job = arg;
   job->draw(job->buf);
   return NULL;
}

static void draw_on_fresh_thread(const char* label, draw_fn draw, size_t n)
{
   unsigned char buf[GETRANDOM_LEN];
   draw_job job = { draw, buf };
   pthread_t t;
   int err = pthread_create(&t, NULL, draw_thread, &job);
   if (err)
      fail(label, err);
   err = pthread_join(t, NULL);
   if (err)
      fail(label, err);
   print_hex(label, buf, n);
}


int main(void)
{
   check_errors();
   print_hex("at_random", (const unsigned char*)getauxval(AT_RANDOM),
             AT_RANDOM_LEN);
   draw_on_fresh_thread("getrandom", draw_getrandom, GETRANDOM_LEN);
   draw_on_fresh_thread("urandom", draw_urandom, URANDOM_LEN);
   draw_on_fresh_thread("urandom_dup", draw_urandom_dup, URANDOM_DUP_LEN);
   draw_on_fresh_thread("readv", draw_readv, READV_PARTS * READV_PART_LEN);
   return 0;
}
