#include <stdio.h>
#include <unistd.h>
#include "inline-crossfile-helper1.h"
#include "inline-crossfile-helper2.h"

// Non-inlinable output function to prevent any stdio inlining issues
__attribute__((noinline))
static void my_output(int sum, int prod) {
    char buf[100];
    int len = snprintf(buf, sizeof(buf), "sum=%d, product=%d\n", sum, prod);
    write(STDOUT_FILENO, buf, len);
}

int main(int argc, char **argv) {
    int n = 5 + (argc - 1);  // n = 5 when argc = 1

    // Call first inline function from helper1.h
    int sum = compute_sum(n);

    // Call second inline function from helper2.h
    int prod = compute_product(n);

    my_output(sum, prod);

    return 0;
}
