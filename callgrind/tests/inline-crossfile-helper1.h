#ifndef HELPER1_H
#define HELPER1_H

// Inline function from helper1.h
static inline int compute_sum(int x) {
    int sum = 0;
    for (int i = 0; i < x; i++) {
        sum += i;
    }
    return sum;
}

#endif
