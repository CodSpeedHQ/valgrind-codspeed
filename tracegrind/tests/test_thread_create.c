#include "tracegrind.h"
#include <pthread.h>

static void* thread_fn(void* arg)
{
   (void)arg;
   return NULL;
}

int main(void)
{
   pthread_t t;
   TRACEGRIND_ADD_MARKER("start");
   TRACEGRIND_START_INSTRUMENTATION;
   pthread_create(&t, NULL, thread_fn, NULL);
   pthread_join(t, NULL);
   TRACEGRIND_STOP_INSTRUMENTATION;
   TRACEGRIND_ADD_MARKER("end");
   return 0;
}
