#include <stdio.h>
#include "inline-crossfile-helper1.h"
#include "inline-crossfile-helper2.h"

int main(int argc, char **argv) {
    int n = 5 + (argc - 1);  // n = 5 when argc = 1

    // Call first inline function from helper1.h
    int sum = compute_sum(n);

    // Call second inline function from helper2.h
    int prod = compute_product(n);

    printf("sum=%d, product=%d\n", sum, prod);

    return 0;
}
