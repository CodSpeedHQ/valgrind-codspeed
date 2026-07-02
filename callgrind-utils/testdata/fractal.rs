// Rust twin of `testdata/fractal.c`: a pure-compute recursive fractal whose
// Callgrind client requests fire several frames deep.
//
// The CALLGRIND_* client requests are inline-asm sequences, so a pure-Rust
// binary can't issue them directly. Instead this fixture links the C
// `clgctl.c` shim (compiled into a static lib by the test harness) and calls
// `clg_start` / `clg_stop` through FFI, the same shim the Python fixture drives
// via ctypes.
//
// Every function is `#[no_mangle]` so the profile carries stable C-like symbol
// names (Callgrind's node redaction does not strip Rust mangling hashes).
// Integer arithmetic and a fixed-size arena (no `Vec`, no `f64`) keep the graph
// free of allocator / libm frames, so the parsed JSON is stable across
// platforms.
//
// Build (done by tests/rust_callgraph.rs):
//   rustc --edition 2021 -g -C opt-level=0 -L native=<dir> -l static=clgctl ...

#![allow(dead_code)]

const MAX_DEPTH: usize = 5;
const BRANCH_FACTOR: usize = 3;
const FIB_N: usize = 25;
const MAX_NODES: usize = 1024;

extern "C" {
    fn clg_start();
    fn clg_stop();
}

#[derive(Clone, Copy)]
struct FractalNode {
    value: i64,
    depth: i64,
    computed_hash: u64,
    children: [usize; BRANCH_FACTOR],
    num_children: usize,
}

impl FractalNode {
    const fn zero() -> Self {
        FractalNode {
            value: 0,
            depth: 0,
            computed_hash: 0,
            children: [0; BRANCH_FACTOR],
            num_children: 0,
        }
    }
}

// Bump-allocated node arena: avoids the allocator frames a heap tree would leak
// into the profile. A fresh arena is used for every tree build.
struct Pool {
    nodes: [FractalNode; MAX_NODES],
    used: usize,
}

impl Pool {
    fn new() -> Self {
        Pool {
            nodes: [FractalNode::zero(); MAX_NODES],
            used: 0,
        }
    }
}

#[no_mangle]
#[inline(never)]
fn pool_alloc(pool: &mut Pool) -> usize {
    let idx = pool.used;
    pool.used += 1;
    pool.nodes[idx] = FractalNode::zero();
    idx
}

// Deterministic child seed (integer stand-in for the original golden-ratio sine).
#[no_mangle]
#[inline(never)]
fn compute_child_value(parent_value: i64, child_index: usize, depth: usize) -> i64 {
    let base = (parent_value as u64).wrapping_mul(2654435761);
    let offset = ((child_index as u64) + 1).wrapping_mul((depth as u64) + 1);
    (((base ^ offset.wrapping_mul(40503)) % 100) + 1) as i64
}

#[no_mangle]
#[inline(never)]
fn compute_tree_hash(pool: &Pool, idx: usize) -> u64 {
    let node = pool.nodes[idx];
    let mut hash = (node.value as u64).wrapping_mul(31).wrapping_add(node.depth as u64);
    for i in 0..node.num_children {
        hash = hash
            .wrapping_mul(31)
            .wrapping_add(compute_tree_hash(pool, node.children[i]));
    }
    hash
}

#[no_mangle]
#[inline(never)]
fn build_fractal(pool: &mut Pool, depth: usize, seed: i64) -> usize {
    let idx = pool_alloc(pool);
    pool.nodes[idx].value = seed;
    pool.nodes[idx].depth = depth as i64;

    if depth < MAX_DEPTH {
        let mut children = [0usize; BRANCH_FACTOR];
        for i in 0..BRANCH_FACTOR {
            let child_seed = compute_child_value(seed, i, depth);
            children[i] = build_fractal(pool, depth + 1, child_seed);
        }
        pool.nodes[idx].children = children;
        pool.nodes[idx].num_children = BRANCH_FACTOR;
    }

    pool.nodes[idx].computed_hash = compute_tree_hash(pool, idx);
    idx
}

#[no_mangle]
#[inline(never)]
fn recursive_sum(pool: &Pool, idx: usize) -> i64 {
    let node = pool.nodes[idx];
    let mut children_sum = 0i64;
    for i in 0..node.num_children {
        children_sum += recursive_sum(pool, node.children[i]);
    }
    node.value + children_sum
}

#[no_mangle]
#[inline(never)]
fn max_path_sum(pool: &Pool, idx: usize) -> i64 {
    let node = pool.nodes[idx];
    if node.num_children == 0 {
        return node.value;
    }

    let mut max_child_path = 0i64;
    for i in 0..node.num_children {
        let child_path = max_path_sum(pool, node.children[i]);
        if child_path > max_child_path {
            max_child_path = child_path;
        }
    }
    node.value + max_child_path
}

#[no_mangle]
#[inline(never)]
fn count_nodes(pool: &Pool, idx: usize) -> i64 {
    let node = pool.nodes[idx];
    let mut count = 1i64;
    for i in 0..node.num_children {
        count += count_nodes(pool, node.children[i]);
    }
    count
}

#[no_mangle]
#[inline(never)]
fn collect_leaves(pool: &Pool, idx: usize, leaves: &mut [i64], count: &mut usize) {
    let node = pool.nodes[idx];
    if node.num_children == 0 {
        leaves[*count] = node.value;
        *count += 1;
        return;
    }
    for i in 0..node.num_children {
        collect_leaves(pool, node.children[i], leaves, count);
    }
}

#[no_mangle]
#[inline(never)]
fn fibonacci_memo(n: i64, memo: &mut [i64]) -> i64 {
    if n <= 1 {
        return n;
    }
    if memo[n as usize] != -1 {
        return memo[n as usize];
    }

    let result = fibonacci_memo(n - 1, memo) + fibonacci_memo(n - 2, memo);
    memo[n as usize] = result;
    result
}

#[no_mangle]
#[inline(never)]
fn compute_variance(values: &[i64]) -> i64 {
    if values.is_empty() {
        return 0;
    }

    let mut mean = 0i64;
    for &v in values {
        mean += v;
    }
    mean /= values.len() as i64;

    let mut variance = 0i64;
    for &v in values {
        let diff = v - mean;
        variance += diff * diff;
    }
    variance / values.len() as i64
}

#[no_mangle]
#[inline(never)]
fn recursive_path_score(value: i64, depth: usize) -> i64 {
    if depth == 0 || value < 2 {
        return value;
    }
    let reduced = (value * 4) / 5;
    1 + recursive_path_score(reduced, depth - 1) / 2
}

#[no_mangle]
#[inline(never)]
fn compute_complexity_score(node_count: i64, variance: i64, max_path: i64) -> i64 {
    let base_score = node_count * variance;
    let path_factor = recursive_path_score(max_path, 5);
    base_score + path_factor
}

#[derive(Clone, Copy)]
struct TreeAnalysis {
    total_sum: i64,
    node_count: i64,
    max_path: i64,
    leaf_variance: i64,
    complexity_score: i64,
}

#[no_mangle]
#[inline(never)]
fn analyze_fractal_tree(pool: &Pool, root: usize, analysis_depth: usize) -> TreeAnalysis {
    let total_sum = recursive_sum(pool, root);
    let node_count = count_nodes(pool, root);
    let max_path = max_path_sum(pool, root);

    let mut leaves = [0i64; MAX_NODES];
    let mut leaf_count = 0usize;
    collect_leaves(pool, root, &mut leaves, &mut leaf_count);
    let leaf_variance = compute_variance(&leaves[..leaf_count]);

    if analysis_depth > 0 {
        let nested = analyze_fractal_tree(pool, root, analysis_depth - 1);
        return TreeAnalysis {
            total_sum: total_sum + nested.total_sum / 10,
            node_count,
            max_path: max_path.max(nested.max_path),
            leaf_variance: (leaf_variance + nested.leaf_variance) / 2,
            complexity_score: compute_complexity_score(node_count, leaf_variance, max_path),
        };
    }

    TreeAnalysis {
        total_sum,
        node_count,
        max_path,
        leaf_variance,
        complexity_score: compute_complexity_score(node_count, leaf_variance, max_path),
    }
}

#[no_mangle]
#[inline(never)]
fn complex_fractal_benchmark() -> i64 {
    let mut pool = Pool::new();
    let root = build_fractal(&mut pool, 0, 42);

    let analysis = analyze_fractal_tree(&pool, root, 2);

    let mut memo = [-1i64; FIB_N + 1];
    let fib_result = fibonacci_memo(FIB_N as i64, &mut memo);

    let tree_hash = compute_tree_hash(&pool, root) as i64;
    let tree_metric = analysis.total_sum
        + analysis.node_count * 10
        + analysis.max_path
        + analysis.leaf_variance
        + analysis.complexity_score;

    (tree_metric.wrapping_add(fib_result).wrapping_add(tree_hash)).rem_euclid(1_000_000)
}

// Deepest frame: instrumentation is turned on here, with
// main -> run_benchmark -> warmup -> run_measured already live on the native
// stack but the shadow stack empty. The seeder reconstructs that chain.
#[no_mangle]
#[inline(never)]
fn run_measured() -> i64 {
    unsafe {
        clg_start();
    }

    let result = complex_fractal_benchmark();

    unsafe {
        clg_stop();
    }
    result
}

// Two unmeasured warmup iterations (instrumentation still off) before the
// measured run, like a real benchmark harness.
#[no_mangle]
#[inline(never)]
fn warmup() -> i64 {
    let mut acc = 0i64;
    for _ in 0..2 {
        acc = acc.wrapping_add(complex_fractal_benchmark());
    }
    std::hint::black_box(acc);
    run_measured()
}

#[no_mangle]
#[inline(never)]
fn run_benchmark() -> i64 {
    warmup()
}

fn main() {
    let result = run_benchmark();
    std::hint::black_box(result);
}
