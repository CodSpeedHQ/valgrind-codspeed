// Adapted directly from the real production benchmark that exhibits the
// "free calls analyze_fractal_tree" misattribution on aarch64
// (codspeed-integrations-e2e-tests/rust/{src/lib.rs,src/fractal.rs}). Unlike
// testdata/fractal.rs (which deliberately avoids Vec/f64 to keep the graph
// allocator-free and stable across platforms), this fixture intentionally
// uses real Vec<FractalNode>/Vec<f64> heap allocation, matching the shape
// that has been confirmed (via a real production trace) to trigger the
// bug: `analyze_fractal_tree` computes a median then an interquartile range
// back-to-back (each allocates a scratch Vec<f64>, sorts it, and drops it)
// before making a self-recursive call.
//
// `enable_regression` is hardcoded false, matching CODSPEED_REGRESSION=0 in
// CI -- it doesn't gate any of the allocations relevant to this bug.
//
// Build (mirrors testdata/fractal.rs's convention, done by tests/rust_callgraph.rs):
//   rustc --edition 2021 -g -C opt-level=3 -L native=<dir> -l static=clgctl ...

#![allow(dead_code)]

extern "C" {
    fn clg_start();
    fn clg_stop();
}

#[derive(Debug, Clone)]
struct FractalNode {
    value: f64,
    children: Vec<FractalNode>,
}

impl FractalNode {
    fn new(value: f64) -> Self {
        FractalNode {
            value,
            children: Vec::new(),
        }
    }

    fn build_fractal(depth: usize, max_depth: usize, branch_factor: usize, seed: f64) -> Self {
        let mut node = FractalNode::new(seed);
        if depth < max_depth {
            for i in 0..branch_factor {
                let child_seed = Self::compute_child_value(seed, i, depth);
                node.children
                    .push(Self::build_fractal(depth + 1, max_depth, branch_factor, child_seed));
            }
        }
        node
    }

    fn compute_child_value(parent_value: f64, child_index: usize, depth: usize) -> f64 {
        let base = parent_value * 0.618033988749;
        let offset = (child_index as f64 + 1.0) * (depth as f64 + 1.0);
        (base + offset).sin().abs() * 100.0
    }

    fn recursive_sum(&self) -> f64 {
        let children_sum: f64 = self.children.iter().map(|c| c.recursive_sum()).sum();
        self.value + children_sum
    }

    fn max_path_sum(&self) -> f64 {
        if self.children.is_empty() {
            return self.value;
        }
        let max_child_path = self
            .children
            .iter()
            .map(|c| c.max_path_sum())
            .fold(f64::NEG_INFINITY, f64::max);
        self.value + max_child_path
    }

    fn count_nodes(&self) -> usize {
        1 + self.children.iter().map(|c| c.count_nodes()).sum::<usize>()
    }

    fn collect_leaves(&self, leaves: &mut Vec<f64>) {
        if self.children.is_empty() {
            leaves.push(self.value);
        } else {
            for child in &self.children {
                child.collect_leaves(leaves);
            }
        }
    }
}

#[no_mangle]
#[inline(never)]
fn analyze_fractal_tree(tree: &FractalNode, analysis_depth: usize) -> TreeAnalysis {
    let total_sum = tree.recursive_sum();
    let node_count = tree.count_nodes();
    let max_path = tree.max_path_sum();

    let mut leaves = Vec::new();
    tree.collect_leaves(&mut leaves);
    let leaf_variance = compute_variance(&leaves);

    let leaf_stddev = leaf_variance.sqrt();
    let leaf_median = compute_median(&leaves);
    let leaf_iqr = compute_interquartile_range(&leaves);

    if analysis_depth > 0 {
        let nested_analysis = analyze_fractal_tree(tree, analysis_depth - 1);
        TreeAnalysis {
            total_sum: total_sum + nested_analysis.total_sum * 0.1,
            node_count,
            max_path: max_path.max(nested_analysis.max_path),
            leaf_variance: (leaf_variance + nested_analysis.leaf_variance) / 2.0,
            complexity_score: compute_complexity_score(
                node_count,
                leaf_variance,
                max_path,
                leaf_stddev,
                leaf_median,
                leaf_iqr,
            ),
        }
    } else {
        TreeAnalysis {
            total_sum,
            node_count,
            max_path,
            leaf_variance,
            complexity_score: compute_complexity_score(
                node_count,
                leaf_variance,
                max_path,
                leaf_stddev,
                leaf_median,
                leaf_iqr,
            ),
        }
    }
}

fn compute_variance(values: &[f64]) -> f64 {
    if values.is_empty() {
        return 0.0;
    }
    let mean = values.iter().sum::<f64>() / values.len() as f64;
    values.iter().map(|v| (v - mean) * (v - mean)).sum::<f64>() / values.len() as f64
}

fn compute_median(values: &[f64]) -> f64 {
    if values.is_empty() {
        return 0.0;
    }
    let mut sorted = values.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let mid = sorted.len() / 2;
    if sorted.len() % 2 == 0 {
        (sorted[mid - 1] + sorted[mid]) / 2.0
    } else {
        sorted[mid]
    }
}

fn compute_interquartile_range(values: &[f64]) -> f64 {
    if values.len() < 4 {
        return 0.0;
    }
    let mut sorted = values.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let q1_idx = sorted.len() / 4;
    let q3_idx = (sorted.len() * 3) / 4;
    sorted[q3_idx] - sorted[q1_idx]
}

fn compute_complexity_score(
    node_count: usize,
    variance: f64,
    max_path: f64,
    stddev: f64,
    median: f64,
    iqr: f64,
) -> f64 {
    let base_score = (node_count as f64).ln() * variance.sqrt();
    let path_factor = recursive_path_score(max_path, 7);
    let distribution_factor = (stddev + median + iqr) / 3.0;
    let trig_factor = (distribution_factor.sin().abs() + distribution_factor.cos().abs()) / 2.0;
    base_score * path_factor * (1.0 + trig_factor)
}

fn recursive_path_score(value: f64, depth: usize) -> f64 {
    if depth == 0 || value < 1.0 {
        return value;
    }
    let reduced = value * 0.8;
    1.0 + recursive_path_score(reduced, depth - 1) * 0.5
}

#[derive(Debug)]
struct TreeAnalysis {
    total_sum: f64,
    node_count: usize,
    max_path: f64,
    leaf_variance: f64,
    complexity_score: f64,
}

fn fibonacci_memo(n: u32, memo: &mut std::collections::HashMap<u32, u64>) -> u64 {
    if n <= 1 {
        return n as u64;
    }
    if let Some(&result) = memo.get(&n) {
        return result;
    }
    let result = fibonacci_memo(n - 1, memo) + fibonacci_memo(n - 2, memo);
    memo.insert(n, result);
    result
}

#[no_mangle]
#[inline(never)]
fn complex_fractal_benchmark(tree_depth: usize, branch_factor: usize, fib_n: u32) -> f64 {
    let tree = FractalNode::build_fractal(0, tree_depth, branch_factor, 42.0);
    let analysis = analyze_fractal_tree(&tree, 4);

    let mut memo = std::collections::HashMap::new();
    let fib_result = fibonacci_memo(fib_n, &mut memo) as f64;
    let fib_result2 = fibonacci_memo(fib_n + 2, &mut memo) as f64;
    let fib_result3 = fibonacci_memo(fib_n + 3, &mut memo) as f64;

    let tree_hash_stub = tree.recursive_sum();
    let tree_metric = analysis.total_sum
        + (analysis.node_count as f64 * 10.0)
        + analysis.max_path
        + analysis.leaf_variance
        + analysis.complexity_score;

    let combined = tree_metric + fib_result + fib_result2 + fib_result3 + tree_hash_stub;
    let transformed = combined.sqrt() * combined.ln_1p();
    let trig_result = (combined / 1000.0).sin().powi(2) + (combined / 1000.0).cos().powi(2);

    (transformed + combined + trig_result) % 1_000_000.0
}

#[no_mangle]
#[inline(never)]
fn run_measured() -> f64 {
    unsafe {
        clg_start();
    }
    let result = complex_fractal_benchmark(5, 3, 25);
    unsafe {
        clg_stop();
    }
    result
}

#[no_mangle]
#[inline(never)]
fn warmup() -> f64 {
    let mut acc = 0.0f64;
    for _ in 0..2 {
        acc += complex_fractal_benchmark(5, 3, 25);
    }
    std::hint::black_box(acc);
    run_measured()
}

#[no_mangle]
#[inline(never)]
fn run_benchmark() -> f64 {
    warmup()
}

fn main() {
    let result = run_benchmark();
    std::hint::black_box(result);
}
