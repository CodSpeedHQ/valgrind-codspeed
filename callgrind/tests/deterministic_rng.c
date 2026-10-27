/* libc startup draws advance the main thread's stream. Use fresh threads
   for libc-independent expected bytes; AT_RANDOM has its own stream. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
   READV_PARTS = 2,
   UUID_TEXT_LEN = 37,
   UUID_SPLIT = 10
};

static const char uuid_path[]    = "/proc/sys/kernel/random/uuid";
static const char boot_id_path[] = "/proc/sys/kernel/random/boot_id";

static unsigned char boot_id_text[UUID_TEXT_LEN];

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

/* A UUID string is lowercase hex with hyphens at fixed positions, version 4
   at text offset 14 and an RFC variant at offset 19; proc files add \n. */
static int uuid_pos_ok(size_t pos, unsigned char c)
{
   if (pos == UUID_TEXT_LEN - 1)
      return c == '\n';
   if (pos == 8 || pos == 13 || pos == 18 || pos == 23)
      return c == '-';
   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static int uuid_window_ok(size_t offset, const unsigned char* p, size_t n)
{
   size_t i;
   for (i = 0; i < n; i++)
      if (!uuid_pos_ok(offset + i, p[i]))
         return 0;
   return 1;
}

static int uuid_text_ok(const unsigned char* p)
{
   if (p[14] != '4')
      return 0;
   if (p[19] != '8' && p[19] != '9' && p[19] != 'a' && p[19] != 'b')
      return 0;
   return uuid_window_ok(0, p, UUID_TEXT_LEN);
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

static void run_fresh_thread(const char* label, void* (*fn)(void*), void* arg)
{
   pthread_t t;
   int       err = pthread_create(&t, NULL, fn, arg);
   if (err)
      fail(label, err);
   err = pthread_join(t, NULL);
   if (err)
      fail(label, err);
}

enum proc_read_kind {
   ProcRead,
   ProcPread,
   ProcReadv,
   ProcPreadv,
   ProcPreadv2,
   ProcCurrent,
   ProcReadKinds
};

static ssize_t
proc_read(enum proc_read_kind kind, int fd, unsigned char* buf, size_t split)
{
   struct iovec vec[] = {{buf, split}, {buf + split, UUID_TEXT_LEN - split}};
   switch (kind) {
   case ProcRead:
      return read(fd, buf, UUID_TEXT_LEN);
   case ProcPread:
      return pread(fd, buf, UUID_TEXT_LEN, 0);
   case ProcReadv:
      return readv(fd, vec, 2);
   case ProcPreadv:
      return preadv(fd, vec, 2, 0);
   case ProcPreadv2:
      return preadv2(fd, vec, 2, 0, 0);
   case ProcCurrent:
      return preadv2(fd, vec, 2, -1, 0);
   default:
      abort();
   }
}

static int proc_open(const char* path)
{
   int fd = open(path, O_RDONLY);
   if (fd < 0)
      fail(path, errno);
   return fd;
}

/* Full reads on a fresh thread give libc-independent expected bytes.
   Errors and EOF between draws must not advance the stream. */
static void* proc_uuid_thread(void* arg)
{
   unsigned char buf[UUID_TEXT_LEN];
   long          page = sysconf(_SC_PAGESIZE);
   void* ro = mmap(NULL, page, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   int   fd = proc_open(uuid_path);
   (void)arg;
   assert(ro != MAP_FAILED);
   for (enum proc_read_kind kind = ProcRead; kind < ProcReadKinds; kind++) {
      assert(lseek(fd, 0, SEEK_SET) == 0);
      assert(read(fd, ro, UUID_TEXT_LEN) == -1 && errno == EFAULT);
      assert(lseek(fd, 0, SEEK_SET) == 0);
      assert(proc_read(kind, fd, buf, UUID_TEXT_LEN) == UUID_TEXT_LEN);
      assert(uuid_text_ok(buf));
      printf("proc_uuid_%d: %.*s", kind, UUID_TEXT_LEN, buf);
      int positional =
         kind == ProcPread || kind == ProcPreadv || kind == ProcPreadv2;
      assert(lseek(fd, 0, SEEK_CUR) == (positional ? 0 : UUID_TEXT_LEN));
      assert(pread(fd, buf, sizeof(buf), UUID_TEXT_LEN) == 0);
      if (!positional)
         assert(read(fd, buf, sizeof(buf)) == 0);
   }
   close(fd);
   munmap(ro, page);

   fd = proc_open(boot_id_path);
   read_full("boot_id", fd, boot_id_text, UUID_TEXT_LEN);
   printf("boot_id: %.*s", UUID_TEXT_LEN, boot_id_text);
   assert(lseek(fd, 0, SEEK_SET) == 0);
   read_full("boot_id_repeat", fd, buf, UUID_TEXT_LEN);
   assert(memcmp(buf, boot_id_text, UUID_TEXT_LEN) == 0);
   close(fd);
   return NULL;
}

/* Older kernels return EOF at nonzero offsets; newer ones return a slice. */
static void* proc_uuid_offsets_thread(void* arg)
{
   unsigned char buf[UUID_TEXT_LEN + 8], bid[UUID_TEXT_LEN];
   int           fd = proc_open(uuid_path), fd2 = dup(fd);
   ssize_t       r;
   (void)arg;
   assert(fd2 >= 0);
   assert(read(fd, buf, UUID_SPLIT) == UUID_SPLIT);
   assert(uuid_window_ok(0, buf, UUID_SPLIT));
   r = read(fd2, buf, sizeof(buf));
   assert(r == 0 || (r == UUID_TEXT_LEN - UUID_SPLIT &&
                     uuid_window_ok(UUID_SPLIT, buf, r)));
   assert(lseek(fd, 0, SEEK_CUR) == UUID_SPLIT + r);
   close(fd2);

   for (enum proc_read_kind kind = ProcReadv; kind < ProcReadKinds; kind++) {
      assert(lseek(fd, 0, SEEK_SET) == 0);
      r = proc_read(kind, fd, buf, UUID_SPLIT);
      assert((r == UUID_TEXT_LEN && uuid_text_ok(buf)) ||
             (r == UUID_SPLIT && uuid_window_ok(0, buf, r)));
   }
   const int offsets[] = {4, UUID_TEXT_LEN - 1, UUID_TEXT_LEN};
   for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
      r = pread(fd, buf, sizeof(buf), offsets[i]);
      assert(r == 0 || (r == UUID_TEXT_LEN - offsets[i] &&
                        uuid_window_ok(offsets[i], buf, r)));
   }
   expect_errno("proc_uuid_pread_negative", pread(fd, buf, 1, -2), EINVAL);
   close(fd);
   fd = open(uuid_path, O_PATH);
   assert(fd >= 0);
   expect_errno("proc_uuid_opath", read(fd, buf, 1), EBADF);
   close(fd);

   fd = proc_open(boot_id_path);
   read_full("boot_id_other_thread", fd, bid, UUID_TEXT_LEN);
   assert(memcmp(bid, boot_id_text, UUID_TEXT_LEN) == 0);
   /* The output overwrites the iovec that the kernel already imported. */
   struct iovec vec[] = {{NULL, sizeof(size_t)},
                         {buf, UUID_TEXT_LEN - sizeof(size_t)}};
   vec[0].iov_base    = &vec[0].iov_len;
   assert(lseek(fd, 0, SEEK_SET) == 0);
   r = readv(fd, vec, 2);
   assert(r == UUID_TEXT_LEN || r == sizeof(size_t));
   assert(memcmp(&vec[0].iov_len, bid, sizeof(size_t)) == 0);
   assert(r != UUID_TEXT_LEN ||
          memcmp(buf, bid + sizeof(size_t), r - sizeof(size_t)) == 0);
   fd2 = proc_open(uuid_path);
   read_full("uuid_between_boot_reads", fd2, buf, UUID_TEXT_LEN);
   close(fd2);
   assert(lseek(fd, 0, SEEK_SET) == 0);
   read_full("boot_id_read_order", fd, buf, UUID_TEXT_LEN);
   assert(memcmp(bid, buf, UUID_TEXT_LEN) == 0);
   close(fd);
   puts("proc_uuid_offsets: ok");
   return NULL;
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
   run_fresh_thread("proc_uuid", proc_uuid_thread, NULL);
   run_fresh_thread("proc_uuid_offsets", proc_uuid_offsets_thread, NULL);
   return 0;
}
