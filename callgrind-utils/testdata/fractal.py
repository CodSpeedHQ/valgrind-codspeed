# Python twin of `testdata/fractal.rs`: a self-contained copy of the CodSpeed
# e2e Python benchmark (its `fractal.py` + `benchmark.py` merged), driven the
# way CodSpeed drives a benchmark.
#
# Instrumentation is off at startup (run with --instr-atstart=no) and turned on
# around the measured region via the `clgctl` shim, whose compiled path is
# passed as argv[1]. The client requests fire several frames deep
# (main -> run_benchmark -> warmup -> run_measured), mirroring the Rust twin, so
# the seeder must reconstruct the native chain at the OFF->ON transition.
#
# Before starting, we skip the Python runtime objects (libpython + the python
# executable) from Callgrind at runtime, exactly as pytest-codspeed's
# instrument-hooks does in _callgrind_skip_python_runtime: the interpreter's own
# C frames are folded into their callers so they don't obfuscate the graph.
# Matching is by exact realpath, since Callgrind keys obj-skip on the mapped
# object path.

import ctypes
import math
import os
import sys
import sysconfig
from typing import Dict, List

clgctl = ctypes.CDLL(sys.argv[1])

# Benchmark workload parameters, matching the e2e `test_benchmark.py` /
# `bench_fractal.rs` case: complex_fractal_benchmark(5, 3, 25).
TREE_DEPTH = 5
BRANCH_FACTOR = 3
FIB_N = 25


def skip_python_runtime():
    ldlibrary = sysconfig.get_config_var("LDLIBRARY")
    libdir = sysconfig.get_config_var("LIBDIR")
    libpython = next(
        (
            p
            for p in (
                os.path.join(libdir, ldlibrary) if ldlibrary and libdir else None,
                os.path.join(sys.prefix, "lib", ldlibrary) if ldlibrary else None,
            )
            if p and os.path.exists(p)
        ),
        None,
    )
    for path in (libpython, sys.executable):
        if path:
            clgctl.clg_add_obj_skip(os.path.realpath(path).encode())


class NodeMetadata:
    """Metadata for a fractal node."""

    def __init__(self, depth: int, branch_factor: int):
        self.depth = depth
        self.branch_factor = branch_factor
        self.computed_hash = 0


class FractalNode:
    """A node in a fractal computation tree."""

    def __init__(self, value: float, depth: int, branch_factor: int):
        self.value = value
        self.children: List[FractalNode] = []
        self.metadata = NodeMetadata(depth, branch_factor)

    @classmethod
    def build_fractal(
        cls, depth: int, max_depth: int, branch_factor: int, seed: float
    ) -> "FractalNode":
        """Recursively build a fractal tree with branching patterns."""
        node = cls(seed, depth, branch_factor)

        if depth < max_depth:
            for i in range(branch_factor):
                child_seed = cls._compute_child_value(seed, i, depth)
                child = cls.build_fractal(depth + 1, max_depth, branch_factor, child_seed)
                node.children.append(child)

        node.metadata.computed_hash = node.compute_tree_hash()
        return node

    @staticmethod
    def _compute_child_value(parent_value: float, child_index: int, depth: int) -> float:
        """Nested helper function to compute child values."""
        base = parent_value * 0.618033988749  # Golden ratio conjugate
        offset = (child_index + 1) * (depth + 1)
        return abs(math.sin(base + offset)) * 100.0

    def compute_tree_hash(self) -> int:
        """Recursively compute a hash of the entire tree structure."""
        hash_value = int(self.value * 1000)
        hash_value = (hash_value * 31 + self.metadata.depth) & 0xFFFFFFFFFFFFFFFF
        for child in self.children:
            child_hash = child.compute_tree_hash()
            hash_value = (hash_value * 31 + child_hash) & 0xFFFFFFFFFFFFFFFF
        return hash_value

    def recursive_sum(self) -> float:
        """Recursively compute the sum of all values in the tree."""
        children_sum = sum(child.recursive_sum() for child in self.children)
        return self.value + children_sum

    def max_path_sum(self) -> float:
        """Recursively find the maximum path sum from root to any leaf."""
        if not self.children:
            return self.value
        max_child_path = max(child.max_path_sum() for child in self.children)
        return self.value + max_child_path

    def count_nodes(self) -> int:
        """Recursively count all nodes in the tree."""
        return 1 + sum(child.count_nodes() for child in self.children)

    def collect_leaves(self, leaves: List[float]) -> None:
        """Recursively collect all leaf values."""
        if not self.children:
            leaves.append(self.value)
        else:
            for child in self.children:
                child.collect_leaves(leaves)


class TreeAnalysis:
    """Results of fractal tree analysis."""

    def __init__(
        self,
        total_sum: float,
        node_count: int,
        max_path: float,
        leaf_variance: float,
        complexity_score: float,
    ):
        self.total_sum = total_sum
        self.node_count = node_count
        self.max_path = max_path
        self.leaf_variance = leaf_variance
        self.complexity_score = complexity_score


def fibonacci_memo(n: int, memo: Dict[int, int]) -> int:
    """Compute Fibonacci with memoization (recursive with nested dict operations)."""
    if n <= 1:
        return n
    if n in memo:
        return memo[n]
    result = fibonacci_memo(n - 1, memo) + fibonacci_memo(n - 2, memo)
    memo[n] = result
    return result


def compute_variance(values: List[float]) -> float:
    """Nested helper to compute variance."""
    if not values:
        return 0.0
    mean = sum(values) / len(values)
    variance = sum((v - mean) ** 2 for v in values) / len(values)
    return variance


def recursive_path_score(value: float, depth: int) -> float:
    """Recursive helper for path scoring."""
    if depth == 0 or value < 1.0:
        return value
    reduced = value * 0.8
    return 1.0 + recursive_path_score(reduced, depth - 1) * 0.5


def compute_complexity_score(node_count: int, variance: float, max_path: float) -> float:
    """Nested helper to compute complexity score (with recursive internal call)."""
    base_score = math.log(node_count) * math.sqrt(variance)
    path_factor = recursive_path_score(max_path, 5)
    return base_score * path_factor


def analyze_fractal_tree(tree: FractalNode, analysis_depth: int) -> TreeAnalysis:
    """Nested function that analyzes the fractal tree with multiple passes."""
    total_sum = tree.recursive_sum()
    node_count = tree.count_nodes()
    max_path = tree.max_path_sum()

    leaves: List[float] = []
    tree.collect_leaves(leaves)
    leaf_variance = compute_variance(leaves)

    if analysis_depth > 0:
        nested_analysis = analyze_fractal_tree(tree, analysis_depth - 1)
        return TreeAnalysis(
            total_sum=total_sum + nested_analysis.total_sum * 0.1,
            node_count=node_count,
            max_path=max(max_path, nested_analysis.max_path),
            leaf_variance=(leaf_variance + nested_analysis.leaf_variance) / 2.0,
            complexity_score=compute_complexity_score(node_count, leaf_variance, max_path),
        )
    return TreeAnalysis(
        total_sum=total_sum,
        node_count=node_count,
        max_path=max_path,
        leaf_variance=leaf_variance,
        complexity_score=compute_complexity_score(node_count, leaf_variance, max_path),
    )


def complex_fractal_benchmark(tree_depth: int, branch_factor: int, fib_n: int) -> float:
    """Main benchmark: complex fractal tree computation."""
    tree = FractalNode.build_fractal(0, tree_depth, branch_factor, 42.0)
    analysis = analyze_fractal_tree(tree, 2)

    memo: Dict[int, int] = {}
    fib_result = float(fibonacci_memo(fib_n, memo))

    tree_hash = float(tree.compute_tree_hash())
    tree_metric = (
        analysis.total_sum
        + (analysis.node_count * 10.0)
        + analysis.max_path
        + analysis.leaf_variance
    )
    return (tree_metric + fib_result + tree_hash) % 1_000_000.0


# Deepest frame: instrumentation is turned on here, with
# main -> run_benchmark -> warmup -> run_measured already live on the native
# stack but the shadow stack empty. The seeder reconstructs that chain.
def run_measured() -> float:
    clgctl.clg_start()
    result = complex_fractal_benchmark(TREE_DEPTH, BRANCH_FACTOR, FIB_N)
    clgctl.clg_stop()
    return result


# Two unmeasured warmup iterations (instrumentation still off) before the
# measured run, like a real benchmark harness.
def warmup() -> float:
    acc = 0.0
    for _ in range(2):
        acc += complex_fractal_benchmark(TREE_DEPTH, BRANCH_FACTOR, FIB_N)
    return run_measured()


def run_benchmark() -> float:
    return warmup()


def main() -> None:
    skip_python_runtime()
    result = run_benchmark()
    assert 0 <= result < 1_000_000.0, result


if __name__ == "__main__":
    main()
