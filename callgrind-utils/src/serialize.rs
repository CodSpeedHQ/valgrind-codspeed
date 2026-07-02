use serde::Serialize;

use super::{
    error::ToJsonError,
    model::{CallGraph, Node},
};

/// Canonical JSON view of the whole graph: nodes inline, edges by index.
#[derive(Serialize)]
struct GraphJson<'a> {
    nodes: &'a [Node],
    edges: Vec<EdgeJson>,
}

/// JSON view of a single edge: caller/callee as node indices.
///
/// `call_count` is omitted from the output when `None`.
#[derive(Serialize)]
struct EdgeJson {
    caller: usize,
    callee: usize,
    #[serde(skip_serializing_if = "Option::is_none")]
    call_count: Option<u64>,
}

impl CallGraph {
    /// Serialize the graph to a canonical pretty-printed JSON string.
    pub fn to_json(&self) -> Result<String, serde_json::Error> {
        let edges: Vec<EdgeJson> = self
            .edges()
            .iter()
            .map(|e| EdgeJson {
                caller: self.node_index(&e.caller).expect("caller node present"),
                callee: self.node_index(&e.callee).expect("callee node present"),
                call_count: e.call_count,
            })
            .collect();
        let graph = GraphJson {
            nodes: self.nodes(),
            edges,
        };
        serde_json::to_string_pretty(&graph)
    }

    /// Serialize the graph to a JSON file at `path`.
    pub fn to_json_file(&self, path: impl AsRef<std::path::Path>) -> Result<(), ToJsonError> {
        let s = self.to_json()?;
        std::fs::write(path, s)?;
        Ok(())
    }
}
