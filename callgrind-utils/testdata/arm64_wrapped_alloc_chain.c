// AArch64 reproducer for the "aliased emulated frames" bug: a chain of
// THREE plain-`b` tail calls (mirroring Rust's real
// `__rust_alloc -> __rdl_alloc -> malloc@plt` / `__rust_dealloc ->
// __rdl_dealloc -> free@plt` shims) into a real external allocator
// function. Each tail-called wrapper's call-stack entry inherits its
// ret_addr from the frame that emulated it (bbcc.c's push_call_stack), so
// all three stacked entries end up sharing the exact same ret_addr. When
// the real allocator function finally does its own `ret`, only the
// topmost of these aliased entries gets popped unless the return-matching
// loop keeps consuming deeper equal-SP frames that independently match
// the same target (bbcc.c's extend_popcount_through_aliases) -- otherwise
// the two stale wrapper entries misattribute the NEXT jump as a fresh
// call into whatever code runs after the allocation, instead of a plain
// continuation of the real caller.
//
// A single-hop wrapper (see arm64_tail_call.c/arm64_free_during_recursion.c)
// is not enough to exercise this: with only one emulated frame between the
// real caller and the real external function, "descend until a match is
// found" (the pre-existing loop) walks past the lone zero/aliased entry
// and lands correctly on the real caller's own (non-aliased) entry by
// coincidence. Three hops stacks enough aliased frames that under-counting
// by even one leaves a stale entry behind.
#include <callgrind.h>
#include <stdlib.h>
#include <string.h>

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

// Three-hop tail-call chains into the real allocator, matching the real
// Rust shim depth (`__rust_alloc -> __rdl_alloc -> malloc@plt`).
__attribute__((noinline)) static void *alloc_hop3(size_t n) {
    return malloc(n);
}
__attribute__((noinline)) static void *alloc_hop2(size_t n) {
    return alloc_hop3(n);
}
__attribute__((noinline)) static void *alloc_hop1(size_t n) {
    return alloc_hop2(n);
}

__attribute__((noinline)) static void dealloc_hop3(void *ptr) {
    free(ptr);
}
__attribute__((noinline)) static void dealloc_hop2(void *ptr) {
    dealloc_hop3(ptr);
}
__attribute__((noinline)) static void dealloc_hop1(void *ptr) {
    dealloc_hop2(ptr);
}

__attribute__((noinline)) static void collect_leaf(const Node *node, int *buf, int *count) {
    if (!node) return;
    if (!node->left && !node->right) {
        buf[(*count)++] = node->value;
        return;
    }
    collect_leaf(node->left, buf, count);
    collect_leaf(node->right, buf, count);
}

__attribute__((noinline)) static int compute_stat(const Node *root) {
    // Real call (`bl`) into the 3-hop alloc chain -- the frame that
    // eventually emulates the tail-called wrappers.
    int *buf = alloc_hop1(sizeof(int) * MAX_NODES);
    int count = 0;
    collect_leaf(root, buf, &count);

    int sum = 0;
    for (int i = 0; i < count; i++) sum += buf[i];

    // Real call (`bl`) into the 3-hop dealloc chain.
    dealloc_hop1(buf);

    // Post-free work in this same frame -- exactly the code that gets
    // stolen and re-parented under the allocator if the aliased frames
    // above aren't all correctly popped.
    return sum % 1000;
}

__attribute__((noinline)) static Node *build_tree(int depth, int seed) {
    Node *node = pool_alloc(seed);
    if (depth < MAX_DEPTH) {
        node->left = build_tree(depth + 1, child_value(seed, 1, depth));
        node->right = build_tree(depth + 1, child_value(seed, 2, depth));
    }
    return node;
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    Node *root = build_tree(0, 1);
    return compute_stat(root);
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
