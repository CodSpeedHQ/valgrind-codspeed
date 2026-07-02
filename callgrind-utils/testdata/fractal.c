// Build with `-g -O0` so the functions are real (no inlining) and carry debug
// names:
//   cc -g -O0 -I callgrind -I include ...

#include <callgrind.h>

#define MAX_DEPTH 5
#define BRANCH_FACTOR 3
#define FIB_N 25
#define MAX_NODES 1024

typedef struct FractalNode {
    long value;
    int depth;
    unsigned long computed_hash;
    struct FractalNode *children[BRANCH_FACTOR];
    int num_children;
} FractalNode;

// Bump-allocated node pool: avoids the allocator frames a heap tree would leak
// into the profile. Reset at the start of every tree build.
static FractalNode g_pool[MAX_NODES];
static int g_pool_used;

static FractalNode *pool_alloc(void) {
    FractalNode *node = &g_pool[g_pool_used++];
    node->value = 0;
    node->depth = 0;
    node->computed_hash = 0;
    node->num_children = 0;
    return node;
}

// Deterministic child seed (integer stand-in for the original golden-ratio sine).
static long compute_child_value(long parent_value, int child_index, int depth) {
    unsigned long base = (unsigned long)parent_value * 2654435761UL;
    unsigned long offset = (unsigned long)(child_index + 1) * (unsigned long)(depth + 1);
    return (long)(((base ^ (offset * 40503UL)) % 100UL) + 1UL);
}

static unsigned long compute_tree_hash(const FractalNode *node) {
    unsigned long hash = (unsigned long)node->value;
    hash = hash * 31 + (unsigned long)node->depth;

    for (int i = 0; i < node->num_children; i++) {
        hash = hash * 31 + compute_tree_hash(node->children[i]);
    }
    return hash;
}

static FractalNode *build_fractal(int depth, long seed) {
    FractalNode *node = pool_alloc();
    node->value = seed;
    node->depth = depth;

    if (depth < MAX_DEPTH) {
        node->num_children = BRANCH_FACTOR;
        for (int i = 0; i < BRANCH_FACTOR; i++) {
            long child_seed = compute_child_value(seed, i, depth);
            node->children[i] = build_fractal(depth + 1, child_seed);
        }
    }

    node->computed_hash = compute_tree_hash(node);
    return node;
}

static long recursive_sum(const FractalNode *node) {
    long children_sum = 0;
    for (int i = 0; i < node->num_children; i++) {
        children_sum += recursive_sum(node->children[i]);
    }
    return node->value + children_sum;
}

static long max_path_sum(const FractalNode *node) {
    if (node->num_children == 0) {
        return node->value;
    }

    long max_child_path = 0;
    for (int i = 0; i < node->num_children; i++) {
        long child_path = max_path_sum(node->children[i]);
        if (child_path > max_child_path) {
            max_child_path = child_path;
        }
    }
    return node->value + max_child_path;
}

static int count_nodes(const FractalNode *node) {
    int count = 1;
    for (int i = 0; i < node->num_children; i++) {
        count += count_nodes(node->children[i]);
    }
    return count;
}

// Collected leaves land in a shared buffer; the caller resets g_leaf_count.
static long g_leaves[MAX_NODES];
static int g_leaf_count;

static void collect_leaves(const FractalNode *node) {
    if (node->num_children == 0) {
        g_leaves[g_leaf_count++] = node->value;
        return;
    }
    for (int i = 0; i < node->num_children; i++) {
        collect_leaves(node->children[i]);
    }
}

static int fibonacci_memo(int n, int *memo) {
    if (n <= 1) {
        return n;
    }
    if (memo[n] != -1) {
        return memo[n];
    }

    int result = fibonacci_memo(n - 1, memo) + fibonacci_memo(n - 2, memo);
    memo[n] = result;
    return result;
}

static long compute_variance(const long *values, int count) {
    if (count == 0) {
        return 0;
    }

    long mean = 0;
    for (int i = 0; i < count; i++) {
        mean += values[i];
    }
    mean /= count;

    long variance = 0;
    for (int i = 0; i < count; i++) {
        long diff = values[i] - mean;
        variance += diff * diff;
    }
    return variance / count;
}

static long recursive_path_score(long value, int depth) {
    if (depth == 0 || value < 2) {
        return value;
    }
    long reduced = (value * 4) / 5;
    return 1 + recursive_path_score(reduced, depth - 1) / 2;
}

static long compute_complexity_score(int node_count, long variance, long max_path) {
    long base_score = (long)node_count * variance;
    long path_factor = recursive_path_score(max_path, 5);
    return base_score + path_factor;
}

typedef struct {
    long total_sum;
    int node_count;
    long max_path;
    long leaf_variance;
    long complexity_score;
} TreeAnalysis;

static TreeAnalysis analyze_fractal_tree(FractalNode *tree, int analysis_depth) {
    long total_sum = recursive_sum(tree);
    int node_count = count_nodes(tree);
    long max_path = max_path_sum(tree);

    g_leaf_count = 0;
    collect_leaves(tree);
    long leaf_variance = compute_variance(g_leaves, g_leaf_count);

    TreeAnalysis analysis;
    if (analysis_depth > 0) {
        TreeAnalysis nested = analyze_fractal_tree(tree, analysis_depth - 1);
        analysis.total_sum = total_sum + nested.total_sum / 10;
        analysis.node_count = node_count;
        analysis.max_path = max_path > nested.max_path ? max_path : nested.max_path;
        analysis.leaf_variance = (leaf_variance + nested.leaf_variance) / 2;
        analysis.complexity_score =
            compute_complexity_score(node_count, leaf_variance, max_path);
        return analysis;
    }

    analysis.total_sum = total_sum;
    analysis.node_count = node_count;
    analysis.max_path = max_path;
    analysis.leaf_variance = leaf_variance;
    analysis.complexity_score = compute_complexity_score(node_count, leaf_variance, max_path);
    return analysis;
}

static long complex_fractal_benchmark(void) {
    g_pool_used = 0;
    FractalNode *tree = build_fractal(0, 42);

    TreeAnalysis analysis = analyze_fractal_tree(tree, 2);

    int memo[FIB_N + 1];
    for (int i = 0; i <= FIB_N; i++) {
        memo[i] = -1;
    }
    long fib_result = fibonacci_memo(FIB_N, memo);

    long tree_hash = (long)compute_tree_hash(tree);
    long tree_metric = analysis.total_sum + (long)analysis.node_count * 10 + analysis.max_path +
                       analysis.leaf_variance + analysis.complexity_score;

    return (tree_metric + fib_result + tree_hash) % 1000000;
}

// Deepest frame: this is where instrumentation is turned on, with
// main -> run_benchmark -> warmup -> run_measured already live on the native
// stack but the shadow stack empty. The seeder reconstructs that chain.
static long run_measured(void) {
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_ZERO_STATS;

    long result = complex_fractal_benchmark();

    CALLGRIND_STOP_INSTRUMENTATION;
    return result;
}

// Two unmeasured warmup iterations (instrumentation still off) before the
// measured run, like a real benchmark harness.
static long warmup(void) {
    volatile long acc = 0;
    for (int i = 0; i < 2; i++) {
        acc += complex_fractal_benchmark();
    }
    (void)acc;
    return run_measured();
}

static long run_benchmark(void) {
    return warmup();
}

int main(void) {
    volatile long result = run_benchmark();
    (void)result;
    return 0;
}
