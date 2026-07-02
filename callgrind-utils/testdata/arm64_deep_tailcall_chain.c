// AArch64 reproducer: a longer (6-stage) flat-SP tail-call chain than
// arm64_tail_call.c's 2-stage one, reached from inside real `bl`-based tree
// recursion rather than a flat wrapper chain. Scales up the number of
// same-SP frames `popcount_on_return` must pop in one go when the final
// `ret` fires, and nests that under strictly-lower recursion frames above
// it (arm64_tail_call.c has no recursion above its chain).
#include <callgrind.h>

#define MAX_DEPTH 5
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

__attribute__((noinline)) static int stage_f(int n) { return n * 2 + 1; }
__attribute__((noinline)) static int stage_e(int n) { return stage_f(n + 1); }
__attribute__((noinline)) static int stage_d(int n) { return stage_e(n + 1); }
__attribute__((noinline)) static int stage_c(int n) { return stage_d(n + 1); }
__attribute__((noinline)) static int stage_b(int n) { return stage_c(n + 1); }
__attribute__((noinline)) static int stage_a(int n) { return stage_b(n + 1); }

__attribute__((noinline)) static Node *walk(int depth, int seed) {
    Node *node = pool_alloc(seed);

    if (depth < MAX_DEPTH) {
        node->left = walk(depth + 1, child_value(seed, 1, depth));
        node->right = walk(depth + 1, child_value(seed, 2, depth));
    }

    // Real call (`bl`) into the chain, then 5 plain-`b` sibling calls, then
    // one real `ret`, then post-call sibling work in this same frame.
    int chained = stage_a(seed);
    node->value = seed + (chained % 97);
    return node;
}

__attribute__((noinline)) static int recursive_sum(const Node *node) {
    if (!node) return 0;
    return node->value + recursive_sum(node->left) + recursive_sum(node->right);
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    Node *root = walk(0, 1);
    return recursive_sum(root) % 1000000;
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
