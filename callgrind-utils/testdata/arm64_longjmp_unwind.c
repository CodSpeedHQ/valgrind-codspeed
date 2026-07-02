// AArch64 reproducer: a multi-level non-local jump (`longjmp`) that unwinds
// several real recursion frames at once via an indirect branch, not a
// `ret`. Exercises the NEW bbcc.c block that reclassifies an ordinary jump
// as a return when its target matches a recorded return address deeper in
// the call stack (as opposed to the immediate top-of-stack frame) --
// distinct from arm64_recursive_return.c, which only unwinds one frame at
// a time via ordinary `ret`s.
#include <callgrind.h>
#include <setjmp.h>

#define MAX_DEPTH 8
#define MAX_NODES 512
#define ABORT_DEPTH 5

typedef struct Node {
    int value;
    struct Node *left;
    struct Node *right;
} Node;

static Node pool[MAX_NODES];
static int used;
static int aborted;
static jmp_buf abort_point;

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

__attribute__((noinline)) static Node *build_tree(int depth, int seed) {
    Node *node = pool_alloc(seed);

    if (!aborted && depth == ABORT_DEPTH && seed % 7 == 0) {
        // Jump directly back to `complex_benchmark`'s frame, skipping every
        // intermediate `build_tree` recursion level's own `ret`. Guarded by
        // `aborted` so the post-landing rebuild can't re-trigger the jump.
        aborted = 1;
        longjmp(abort_point, seed);
    }

    if (depth < MAX_DEPTH) {
        node->left = build_tree(depth + 1, child_value(seed, 1, depth));
        node->right = build_tree(depth + 1, child_value(seed, 2, depth));
    }

    node->value += depth;
    return node;
}

__attribute__((noinline)) static int recursive_sum(const Node *node) {
    if (!node) return 0;
    return node->value + recursive_sum(node->left) + recursive_sum(node->right);
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    aborted = 0;
    int jumped_seed = setjmp(abort_point);

    Node *root;
    if (jumped_seed != 0) {
        // Landed here via longjmp from deep inside build_tree. Continue
        // real work in this frame after the multi-level unwind.
        root = build_tree(0, jumped_seed + 1);
    } else {
        root = build_tree(0, 1);
    }

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
