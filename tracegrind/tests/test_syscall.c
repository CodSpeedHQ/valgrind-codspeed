#include "tracegrind.h"
#include <unistd.h>
#include <fcntl.h>

static int __attribute__((noinline)) do_getpid(void) {
    return getpid();
}

static void __attribute__((noinline)) do_write(int fd) {
    const char msg[] = "hello\n";
    write(fd, msg, sizeof(msg) - 1);
}

static void __attribute__((noinline)) caller(int fd) {
    do_getpid();
    do_write(fd);
}

int main(void) {
    int fd = open("/dev/null", O_WRONLY);
    TRACEGRIND_ADD_MARKER("start");
    TRACEGRIND_START_INSTRUMENTATION;
    caller(fd);
    TRACEGRIND_STOP_INSTRUMENTATION;
    TRACEGRIND_ADD_MARKER("end");
    close(fd);
    return 0;
}
