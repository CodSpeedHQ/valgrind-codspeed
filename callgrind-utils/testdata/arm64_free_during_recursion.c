// AArch64 reproducer for a real production bug: after `free()` returns from
// deallocating a scratch heap buffer used mid-recursion, Callgrind's return
// matching misattributes the post-free work in the SAME caller frame as a
// fresh call FROM `free()` INTO the caller, instead of a return. This was
// observed live on a CodSpeed aarch64 runner (`free` showing up as a parent
// of `analyze_fractal_tree`, stealing ~13% of the benchmark's total time)
// even after the "fix: ARM unwinding" commit — the fix does not cover this
// case. Mirrors fractal.rs's `analyze_fractal_tree`: a self-recursive
// analysis function that, at every recursion level, walks the tree
// (real `bl` recursion => strictly-lower-SP frames), then mallocs a scratch
// buffer, does work, frees it, and keeps computing in the same frame
// afterward (real `bl`/`ret` to libc malloc/free, not a toy stand-in).
#include <callgrind.h>
#include <stdlib.h>

#define MAX_DEPTH 6
#define MAX_NODES 256
#define ANALYSIS_DEPTH 3

typedef struct Node {
    int value;
    struct Node *left;
    struct Node *right;
} Node;

typedef struct Analysis {
    int total_sum;
    int node_count;
    int variance;
} Analysis;

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

__attribute__((noinline)) static Node *build_tree(int depth, int seed) {
    Node *node = pool_alloc(seed);
    if (depth < MAX_DEPTH) {
        node->left = build_tree(depth + 1, child_value(seed, 1, depth));
        node->right = build_tree(depth + 1, child_value(seed, 2, depth));
    }
    return node;
}

__attribute__((noinline)) static int recursive_sum(const Node *node) {
    if (!node) return 0;
    return node->value + recursive_sum(node->left) + recursive_sum(node->right);
}

__attribute__((noinline)) static int count_nodes(const Node *node) {
    if (!node) return 0;
    return 1 + count_nodes(node->left) + count_nodes(node->right);
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

// Mirrors Rust's `__rust_dealloc -> __rdl_dealloc -> free` thin-wrapper
// chain, which the compiler likely tail-calls all the way down to the PLT
// stub for `free`, rather than calling `free` directly.
__attribute__((noinline)) static void dealloc_wrapper2(void *ptr) {
    free(ptr);
}

__attribute__((noinline)) static void dealloc_wrapper1(void *ptr) {
    dealloc_wrapper2(ptr);
}

__attribute__((noinline)) static int compute_variance(const Node *root) {
    int *buf = malloc(sizeof(int) * MAX_NODES);
    int count = 0;
    collect_leaf(root, buf, &count);

    int local[MAX_NODES];
    for (int i = 0; i < count; i++) {
        local[i] = buf[i];
    }

    dealloc_wrapper1(buf);

    // Post-free work in the caller's own frame -- this is exactly the cost
    // that gets stolen and re-parented under `free` in the buggy case.
    int mean = 0;
    for (int i = 0; i < count; i++) {
        mean += local[i];
    }
    if (count > 0) mean /= count;

    int variance = 0;
    for (int i = 0; i < count; i++) {
        int diff = local[i] - mean;
        variance += diff * diff;
    }
    if (count > 0) variance /= count;
    return variance;
}

__attribute__((noinline)) static Analysis analyze_tree(const Node *root, int depth) {
    int total_sum = recursive_sum(root);
    int node_count = count_nodes(root);
    int variance = compute_variance(root);

    if (depth > 0) {
        Analysis nested = analyze_tree(root, depth - 1);
        Analysis result;
        result.total_sum = total_sum + nested.total_sum / 10;
        result.node_count = node_count;
        result.variance = (variance + nested.variance) / 2;
        return result;
    }

    Analysis result = { total_sum, node_count, variance };
    return result;
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    Node *root = build_tree(0, 1);
    Analysis analysis = analyze_tree(root, ANALYSIS_DEPTH);
    return analysis.total_sum + analysis.node_count + analysis.variance;
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
