#include "tracegrind.h"
#include <pthread.h>

__attribute__((noinline)) static void depth_a2(void) { }

__attribute__((noinline)) static void depth_a1(void) {
    depth_a2();
}

__attribute__((noinline)) static void *work_a(void *arg) {
    (void)arg;
    depth_a1();
    return NULL;
}

__attribute__((noinline)) static void depth_b1(void) { }

__attribute__((noinline)) static void *work_b(void *arg) {
    (void)arg;
    depth_b1();
    return NULL;
}

__attribute__((noinline)) static void depth_c2(void) { }

__attribute__((noinline)) static void depth_c1(void) {
    depth_c2();
}

__attribute__((noinline)) static void *work_c(void *arg) {
    (void)arg;
    depth_c1();
    return NULL;
}

int main(void) {
    pthread_t t1, t2, t3;

    TRACEGRIND_ADD_MARKER("start");
    TRACEGRIND_START_INSTRUMENTATION;

    pthread_create(&t1, NULL, work_a, NULL);
    pthread_create(&t2, NULL, work_b, NULL);
    pthread_create(&t3, NULL, work_c, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);

    TRACEGRIND_STOP_INSTRUMENTATION;
    TRACEGRIND_ADD_MARKER("end");

    return 0;
}
