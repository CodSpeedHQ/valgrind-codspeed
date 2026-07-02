// AArch64-focused reproducer for Callgrind shadow-stack unwinding on ordinary
// compiler-generated recursive returns.  Mirrors fractal.rs's shape: a
// multi-frame wrapper chain (main -> run_benchmark -> warmup -> run_measured)
// so CALLGRIND_START_INSTRUMENTATION fires several native frames deep and the
// shadow stack must be seeded, then a benchmark function that builds a
// recursive tree and does post-order/sibling work afterwards.
#include <callgrind.h>

#define MAX_DEPTH 6
#define MAX_NODES 256

typedef struct Node {
    int value;
    struct Node *left;
    struct Node *right;
} Node;

static Node pool[MAX_NODES];
static int used;

__attribute__((noinline)) static Node *pool_alloc(int value) {
    Node *node = &pool[used++];
    node->value = value;
    node->left = 0;
    node->right = 0;
    return node;
}

__attribute__((noinline)) static int child_value(int parent, int side, int depth) {
    return parent * 3 + side + depth;
}

__attribute__((noinline)) static int hash_tree(const Node *node) {
    if (!node) return 0;
    return node->value + hash_tree(node->left) * 5 + hash_tree(node->right) * 7;
}

__attribute__((noinline)) static Node *build_tree(int depth, int seed) {
    Node *node = pool_alloc(seed);

    if (depth < MAX_DEPTH) {
        node->left = build_tree(depth + 1, child_value(seed, 1, depth));
        node->right = build_tree(depth + 1, child_value(seed, 2, depth));
    }

    node->value += hash_tree(node);
    return node;
}

__attribute__((noinline)) static int sibling_after_tree(const Node *root) {
    return root->value % 97;
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    Node *root = build_tree(0, 1);
    int total = hash_tree(root);
    total += sibling_after_tree(root);
    return total;
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
