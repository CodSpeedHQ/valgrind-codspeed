// AArch64 reproducer probing the interaction between `--obj-skip` splicing
// (callgrind/bbcc.c's "call from skipped to nonskipped" handling, using
// CLG_(current_state).nonskipped) and the emulated-call machinery (tail
// calls promoted to jk_Call, ret_addr inheritance, alias-popping) fixed
// earlier this session. `skipped_entry`/`skipped_relay` live in a
// companion shared library that gets passed to `--obj-skip`; the relay
// hop into `skipped_relay` and the final hop back into `visible_target`
// (in THIS, non-skipped, executable) are both plain tail calls, so the
// return-matching machinery must correctly splice the skipped frames out
// while still popping the right number of call-stack entries when
// `visible_target` eventually returns for real.
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

extern int skipped_entry(int seed);

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

// Real call target for the skipped library's final tail-call hop. Also
// exported so the linker can't inline/elide the cross-object boundary.
__attribute__((noinline)) int visible_target(int seed) {
    return seed * 2 + 1;
}

__attribute__((noinline)) static Node *build_tree(int depth, int seed) {
    Node *node = pool_alloc(seed);

    if (depth < MAX_DEPTH) {
        node->left = build_tree(depth + 1, child_value(seed, 1, depth));
        node->right = build_tree(depth + 1, child_value(seed, 2, depth));
    }

    // Real call (`bl`) into the skipped library's entry point, which
    // tail-calls within the skipped object, then tail-calls back out into
    // visible_target (non-skipped) -- then post-call work in this same
    // frame, exactly the pattern that gets stolen if splicing mishandles
    // the emulated hops.
    int relayed = skipped_entry(seed);
    node->value += hash_tree(node) + (relayed % 7);
    return node;
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    Node *root = build_tree(0, 1);
    return hash_tree(root) % 1000000;
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
