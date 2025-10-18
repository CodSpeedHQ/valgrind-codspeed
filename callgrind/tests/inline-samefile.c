#include <stdio.h>

// Function 1: Iterative fibonacci - WILL be inlined
static inline int fibonacci_iterative(int n) {
    if (n <= 1)
        return n;

    int prev = 0;
    int curr = 1;

    for (int i = 2; i <= n; i++) {
        int next = prev + curr;
        prev = curr;
        curr = next;
    }

    return curr;
}

// Function 2: Recursive fibonacci - will NOT be inlined
int fibonacci_recursive(int n) {
    if (n <= 1)
        return n;
    return fibonacci_recursive(n - 1) + fibonacci_recursive(n - 2);
}

int main(int argc, char **argv) {
    // Use argc to prevent constant folding but still allow inlining
    int n = 10 + (argc - 1);  // n = 10 when argc = 1

    // Call the iterative fibonacci (should be INLINED)
    int fib_iter = fibonacci_iterative(n);

    // Call the recursive fibonacci (should NOT be inlined)
    int fib_rec = fibonacci_recursive(10);

    printf("Iterative fib(%d): %d, Recursive fib(10): %d\n", n, fib_iter, fib_rec);

    return 0;
}
