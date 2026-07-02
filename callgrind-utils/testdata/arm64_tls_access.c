// AArch64 reproducer for a misattribution around AArch64 TLS descriptor
// resolvers. On a dynamically-linked/PIE binary, accessing a `__thread`
// variable compiles to a GOT-loaded {resolver_fn, arg} pair followed by
// `blr` into that resolver (NOT a normal PLT call) -- `_dl_tlsdesc_return`
// for a statically-known offset, `_dl_tlsdesc_undefweak`/`_dl_tlsdesc_dynamic`
// for other TLS models. This is the exact same class of "transparent
// trampoline" as `_dl_runtime_resolve` (the lazy PLT-binding resolver,
// which callgrind/fn.c already special-cases via `fn->pop_on_jump = True`),
// but callgrind never applied the same treatment to the tlsdesc family.
// Every TLS access in a recursive hot path triggers this, so the return
// from `_dl_tlsdesc_return` back into the accessing function gets
// misattributed as `_dl_tlsdesc_return` calling into whatever runs next --
// observed in production pulling almost the ENTIRE program's cost under
// `_dl_tlsdesc_return` for TLS-heavy workloads (e.g. CPython, which keeps
// per-thread interpreter state in a `__thread` variable).
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

// Defined in arm64_tls_access_lib.c's shared library: a `__thread`
// variable that lives in a SEPARATE .so can't be relaxed by the linker
// down to the cheap Local-Exec TP-relative model, forcing the real
// TLS-descriptor path (GOT-loaded {resolver, arg} pair plus `blr`).
extern int touch_tls(int delta);

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

    // Every recursion level touches the TLS variable (real `blr` into the
    // tlsdesc resolver), then does more work in this same frame afterward
    // -- exactly the "post-trampoline-return work" that gets stolen.
    int bumped = touch_tls(node->value);
    node->value += hash_tree(node) + (bumped % 7);
    return node;
}

__attribute__((noinline)) static int complex_benchmark(void) {
    used = 0;
    touch_tls(-touch_tls(0));
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
