#ifndef HELPER2_H
#define HELPER2_H

// Inline function from helper2.h
static inline int compute_product(int x) {
    int prod = 1;
    for (int i = 1; i <= x; i++) {
        prod *= i;
    }
    return prod;
}

#endif
