// AArch64 reproducer: TWO sequential malloc/free cycles inside the same
// recursive analysis frame (mirrors the real fractal benchmark's
// compute_median + compute_interquartile_range, which each allocate and
// drop their own scratch Vec back-to-back). Tests whether call-stack state
// left over from unwinding the FIRST alloc/free cycle corrupts matching for
// the SECOND cycle within the same still-open caller frame.
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
    int variance;
    int spread;
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

__attribute__((noinline)) static void collect_leaf(const Node *node, int *buf, int *count) {
    if (!node) return;
    if (!node->left && !node->right) {
        buf[(*count)++] = node->value;
        return;
    }
    collect_leaf(node->left, buf, count);
    collect_leaf(node->right, buf, count);
}

__attribute__((noinline)) static int compute_variance(const Node *root) {
    int *buf = malloc(sizeof(int) * MAX_NODES);
    int count = 0;
    collect_leaf(root, buf, &count);

    int local[MAX_NODES];
    for (int i = 0; i < count; i++) local[i] = buf[i];

    int mean = 0;
    for (int i = 0; i < count; i++) mean += buf[i];
    if (count > 0) mean /= count;

    free(buf);

    // Post-free work in this frame, following the first free() in this
    // function -- reads the pre-free local copy, not the freed buffer.
    int variance = 0;
    for (int i = 0; i < count; i++) {
        int diff = local[i] - mean;
        variance += diff * diff;
    }
    if (count > 0) variance /= count;
    return variance;
}

__attribute__((noinline)) static int compute_spread(const Node *root) {
    int *buf = malloc(sizeof(int) * MAX_NODES);
    int count = 0;
    collect_leaf(root, buf, &count);

    int lo = count > 0 ? buf[0] : 0;
    int hi = count > 0 ? buf[0] : 0;
    for (int i = 1; i < count; i++) {
        if (buf[i] < lo) lo = buf[i];
        if (buf[i] > hi) hi = buf[i];
    }

    free(buf);

    // Post-free work in this frame, following the second free() in a row.
    return (hi - lo) * count;
}

__attribute__((noinline)) static Analysis analyze_tree(const Node *root, int depth) {
    int total_sum = recursive_sum(root);
    int variance = compute_variance(root);
    int spread = compute_spread(root);

    if (depth > 0) {
        Analysis nested = analyze_tree(root, depth - 1);
        Analysis result;
        result.total_sum = total_sum + nested.total_sum / 10;
        result.variance = (variance + nested.variance) / 2;
        result.spread = spread + nested.spread;
        return result;
    }

    Analysis result = { total_sum, variance, spread };
    return result;
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    Node *root = build_tree(0, 1);
    Analysis analysis = analyze_tree(root, ANALYSIS_DEPTH);
    return analysis.total_sum + analysis.variance + analysis.spread;
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
