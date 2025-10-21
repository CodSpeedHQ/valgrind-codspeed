#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../callgrind.h"

// Forward declarations
void lib_function1(void);
void lib_function2(void);
void lib_function3(void);

// Simple helper functions to do some work
int compute_sum(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}

// Functions that call library functions
void lib_function1(void) {
    // Call strlen (from libc)
    const char *str = "test string";
    int len = strlen(str);
    printf("String length: %d\n", len);
}

void lib_function2(void) {
    // Call malloc/free (from libc)
    int *ptr = (int *)malloc(sizeof(int) * 10);
    if (ptr) {
        for (int i = 0; i < 10; i++) {
            ptr[i] = i;
        }
        printf("Allocated and initialized 10 ints\n");
        free(ptr);
    }
}

void lib_function3(void) {
    // Call printf directly
    printf("Direct printf call\n");
}

int main(void) {
    printf("Starting plt_sec_test\n");

    // Request callgrind to start collecting data
    CALLGRIND_START_INSTRUMENTATION;

    // Call functions that will use PLT/PLT.SEC
    lib_function1();
    lib_function2();
    lib_function3();

    // Do some work
    int result = compute_sum(100);
    printf("Sum result: %d\n", result);

    // Request callgrind to dump stats
    CALLGRIND_DUMP_STATS;

    printf("plt_sec_test completed\n");
    return 0;
}
