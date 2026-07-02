// AArch64 reproducer: a long flat-SP mutual-tail-call chain (`ping` <-> `pong`,
// alternating plain `b` sibling calls) nested INSIDE ordinary `bl`-based tree
// recursion. Each tree node triggers a bounded ping/pong chain before doing
// post-call sibling work in its own frame. Stresses `popcount_on_return`
// needing to pop many same-SP frames at once, nested under multiple levels
// of strictly-lower-SP real recursion frames -- the combination the simpler
// arm64_tail_call.c (flat-only) and arm64_recursive_return.c (bl-only)
// fixtures don't exercise together.
#include <callgrind.h>

#define MAX_DEPTH 5
#define MAX_NODES 256
#define PING_PONG_ROUNDS 10

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

__attribute__((noinline)) static int pong(int v, int n);

__attribute__((noinline)) static int ping(int v, int n) {
    if (n <= 0) return v;
    return pong(v * 2 + 1, n - 1);
}

__attribute__((noinline)) static int pong(int v, int n) {
    if (n <= 0) return v;
    return ping(v * 3 + 2, n - 1);
}

__attribute__((noinline)) static Node *walk(int depth, int seed) {
    Node *node = pool_alloc(seed);

    if (depth < MAX_DEPTH) {
        node->left = walk(depth + 1, child_value(seed, 1, depth));
        node->right = walk(depth + 1, child_value(seed, 2, depth));
    }

    // Bounded flat-SP tail-call chain, then post-call sibling work in this
    // same (real, `bl`-reached) frame once the chain's final `ret` fires.
    int chained = ping(seed, PING_PONG_ROUNDS);
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
