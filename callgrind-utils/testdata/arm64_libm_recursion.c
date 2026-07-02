// AArch64 reproducer: a real libm PLT call (`sin`) at every level of a
// recursive tree build, followed by sibling work in the same frame after
// the call returns. Mirrors fractal.rs's `build_fractal`, which calls `sin`
// at every recursion level to perturb child seeds -- exercises the same
// return-into-caller-frame path as malloc/free, but through a different
// external library boundary (libm instead of libc's allocator).
#include <callgrind.h>
#include <math.h>

#define MAX_DEPTH 6
#define MAX_NODES 256

typedef struct Node {
    double value;
    struct Node *left;
    struct Node *right;
} Node;

static Node pool[MAX_NODES];
static int used;

__attribute__((noinline)) static Node *pool_alloc(double value) {
    Node *node = &pool[used++];
    node->value = value;
    node->left = 0;
    node->right = 0;
    return node;
}

__attribute__((noinline)) static double perturb(double seed, int side, int depth) {
    return sin(seed * (side + 1) + depth);
}

__attribute__((noinline)) static double hash_tree(const Node *node) {
    if (!node) return 0.0;
    return node->value + hash_tree(node->left) * 1.5 + hash_tree(node->right) * 2.5;
}

__attribute__((noinline)) static Node *build_tree(int depth, double seed) {
    Node *node = pool_alloc(seed);

    if (depth < MAX_DEPTH) {
        double left_seed = perturb(seed, 0, depth);
        node->left = build_tree(depth + 1, left_seed);

        double right_seed = perturb(seed, 1, depth);
        node->right = build_tree(depth + 1, right_seed);
    }

    // Post-call sibling work in this same frame, after both recursive
    // descents (each preceded by a real `bl sin@plt`) have returned.
    node->value += hash_tree(node) * 0.01;
    return node;
}

__attribute__((noinline)) static double recursive_sum(const Node *node) {
    if (!node) return 0.0;
    return node->value + recursive_sum(node->left) + recursive_sum(node->right);
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    Node *root = build_tree(0, 0.37);
    double total = recursive_sum(root) + hash_tree(root);
    return (int)(total * 1000.0) % 1000000;
}

__attribute__((noinline)) static int run_measured(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    int result = complex_benchmark();

    CALLGRIND_STOP_INSTRUMENTATION;
    return result;
}

__attribute__((noinline)) static int warmup(void) {
    volatile int acc = 0;
    for (int i = 0; i < 2; i++) {
        acc += complex_benchmark();
    }
    (void)acc;
    return run_measured();
}

__attribute__((noinline)) static int run_benchmark(void) {
    return warmup();
}

int main(void) {
    volatile int result = run_benchmark();
    (void)result;
    return 0;
}
