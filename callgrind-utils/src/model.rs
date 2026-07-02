use std::collections::HashMap;

use serde::Serialize;

/// A call-graph node: a single function identity.
///
/// Node identity is the full `(object, file, function)` tuple, so two
/// statics that share a name but live in different objects/files are
/// distinct nodes (no false merge).
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize)]
pub struct Node {
    pub function: String,
    pub file: String,
    pub object: String,
}

/// A directed call edge: `caller` calls `callee`, optionally annotated
/// with an observed `call_count` and the callee subtree's `inclusive_cost`
/// (first event column, e.g. `Ir`) as invoked through this edge.
///
/// `Edge` deliberately does NOT derive `Serialize`: the canonical JSON
/// view references nodes by index, not by value. See `serialize::EdgeJson`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Edge {
    pub caller: Node,
    pub callee: Node,
    pub call_count: Option<u64>,
    pub inclusive_cost: Option<u64>,
}

/// Tunables for `.out` parsing.
#[derive(Debug, Clone)]
pub struct ParseOptions {
    /// When true, file/object paths are reduced to their basename and
    /// Callgrind-style unknowns (`???`) collapse to `unknown`.
    pub normalize_paths: bool,
    /// Sentinel substituted for absent/unknown object or file names.
    pub unknown: String,
}

impl Default for ParseOptions {
    fn default() -> Self {
        Self {
            normalize_paths: true,
            unknown: "???".to_string(),
        }
    }
}

/// The parsed call graph: sorted, deduplicated nodes and edges.
///
/// Fields are `pub(crate)` so the sibling `parser` and `serialize`
/// modules can materialize/consume them without exposing them publicly.
pub struct CallGraph {
    pub(crate) nodes: Vec<Node>,
    pub(crate) edges: Vec<Edge>,
    /// Self cost (first event column, e.g. `Ir`) per node, aligned index-for-index
    /// with `nodes`. Zero for nodes that carried no self-cost lines.
    pub(crate) self_costs: Vec<u64>,
}

impl CallGraph {
    /// Borrow the sorted node list.
    pub fn nodes(&self) -> &[Node] {
        &self.nodes
    }

    /// Borrow the sorted, deduplicated edge list.
    pub fn edges(&self) -> &[Edge] {
        &self.edges
    }

    /// Self cost of the node at `index` (first event column). Zero if absent.
    pub fn self_cost(&self, index: usize) -> u64 {
        self.self_costs.get(index).copied().unwrap_or(0)
    }

    /// Construct a `CallGraph` from raw parsed material.
    ///
    /// Nodes are sorted by `(object, file, function)` and de-duplicated.
    /// Edges are sorted by `(caller_idx, callee_idx)` using the sorted
    /// node order, then de-duplicated by `(caller, callee)`, aggregating
    /// `call_count` and `inclusive_cost` across duplicates (sum when both
    /// are `Some`; keep the first value when any duplicate is `None`).
    ///
    /// `self_costs` maps each node identity to its accumulated self cost; it
    /// is projected onto the sorted node order (missing entries become 0).
    pub(crate) fn from_parts(
        mut nodes: Vec<Node>,
        mut edges: Vec<Edge>,
        self_costs: HashMap<Node, u64>,
    ) -> Self {
        nodes.sort_by(|a, b| {
            a.object
                .cmp(&b.object)
                .then_with(|| a.file.cmp(&b.file))
                .then_with(|| a.function.cmp(&b.function))
        });
        nodes.dedup();

        // Index lookup for stable node ordering of edges.
        let mut index: HashMap<&Node, usize> = HashMap::with_capacity(nodes.len());
        for (i, n) in nodes.iter().enumerate() {
            index.insert(n, i);
        }

        let edge_rank = |e: &Edge| {
            (
                index.get(&e.caller).copied().unwrap_or(usize::MAX),
                index.get(&e.callee).copied().unwrap_or(usize::MAX),
            )
        };
        edges.sort_by_key(edge_rank);

        // Dedup adjacent (now grouped) edges, aggregating call_count.
        let mut deduped: Vec<Edge> = Vec::with_capacity(edges.len());
        for e in edges {
            // Dedup adjacent (now grouped) duplicate edges, summing counts;
            // any None keeps the first value as-is.
            if let Some(last) = deduped.last_mut()
                && last.caller == e.caller
                && last.callee == e.callee
            {
                if let (Some(a), Some(b)) = (last.call_count, e.call_count) {
                    last.call_count = Some(a + b);
                }
                if let (Some(a), Some(b)) = (last.inclusive_cost, e.inclusive_cost) {
                    last.inclusive_cost = Some(a + b);
                }
                continue;
            }
            deduped.push(e);
        }

        let node_self_costs: Vec<u64> = nodes
            .iter()
            .map(|n| self_costs.get(n).copied().unwrap_or(0))
            .collect();

        Self {
            nodes,
            edges: deduped,
            self_costs: node_self_costs,
        }
    }

    /// Index of `n` within the sorted node list, or `None` if absent.
    ///
    /// Uses binary search over the `(object, file, function)` ordering
    /// established by `from_parts`.
    pub(crate) fn node_index(&self, n: &Node) -> Option<usize> {
        self.nodes
            .binary_search_by(|x| {
                x.object
                    .cmp(&n.object)
                    .then_with(|| x.file.cmp(&n.file))
                    .then_with(|| x.function.cmp(&n.function))
            })
            .ok()
    }
}
