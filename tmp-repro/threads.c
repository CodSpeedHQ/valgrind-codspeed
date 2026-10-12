// Starts 64 threads that all stay alive until every one has started.
// Optional argument: the thread stack guard size in bytes.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N 64

static pthread_barrier_t barrier;

static void *work(void *arg) {
  pthread_barrier_wait(&barrier);
  return arg;
}

int main(int argc, char **argv) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  if (argc > 1)
    pthread_attr_setguardsize(&attr, strtoul(argv[1], NULL, 0));

  pthread_t threads[N];
  pthread_barrier_init(&barrier, NULL, N + 1);
  for (int i = 0; i < N; i++)
    pthread_create(&threads[i], &attr, work, NULL);
  pthread_barrier_wait(&barrier);
  for (int i = 0; i < N; i++)
    pthread_join(threads[i], NULL);

  printf("%d threads: ok\n", N);
  return 0;
}
