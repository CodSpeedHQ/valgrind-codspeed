use std::collections::HashMap;

use super::model::{CallGraph, Node};

const UNKNOWN: &str = "???";

impl CallGraph {
    /// Redact host-specific node identity and rebuild the canonical graph.
    ///
    /// Self costs are re-keyed onto the redacted node identities, summing where
    /// distinct nodes collapse to the same identity (e.g. libc functions).
    pub fn redact(self) -> CallGraph {
        let CallGraph {
            nodes,
            edges,
            self_costs,
        } = self;
        let mut nodes = nodes;
        let mut edges = edges;

        let mut self_cost_map: HashMap<Node, u64> = HashMap::new();
        for (node, &cost) in nodes.iter().zip(self_costs.iter()) {
            let mut redacted = node.clone();
            redact_node(&mut redacted);
            *self_cost_map.entry(redacted).or_insert(0) += cost;
        }

        for node in &mut nodes {
            redact_node(node);
        }

        for edge in &mut edges {
            redact_node(&mut edge.caller);
            redact_node(&mut edge.callee);
        }

        CallGraph::from_parts(nodes, edges, self_cost_map)
    }
}

fn redact_node(node: &mut Node) {
    node.object = redact_object(&node.object);

    if is_runtime_object(&node.object) {
        node.function = UNKNOWN.to_string();
        node.file = UNKNOWN.to_string();
        return;
    }

    node.function = redact_function(&node.function);
}

fn redact_function(function: &str) -> String {
    let function = strip_symbol_version(function);
    if is_hex_address(function) {
        return "<unsymbolicated>".to_string();
    }
    function.to_string()
}

fn strip_symbol_version(function: &str) -> &str {
    for marker in ["@@", "@"] {
        let Some(index) = function.find(marker) else {
            continue;
        };
        let version = &function[index + marker.len()..];
        if is_symbol_version(version) {
            return &function[..index];
        }
    }
    function
}

fn is_symbol_version(version: &str) -> bool {
    let Some(first) = version.chars().next() else {
        return false;
    };
    (first.is_ascii_alphanumeric() || first == '_')
        && version
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '.')
}

fn is_hex_address(function: &str) -> bool {
    let Some(hex) = function.strip_prefix("0x") else {
        return false;
    };
    !hex.is_empty() && hex.chars().all(|c| c.is_ascii_hexdigit())
}

fn redact_object(object: &str) -> String {
    if is_loader_soname(object) {
        return "ld-linux".to_string();
    }
    if let Some(module) = cpython_extension_module(object) {
        return format!("{module}.cpython.so");
    }
    if is_libffi_soname(object) {
        return "libffi.so".to_string();
    }
    object.to_string()
}

fn is_runtime_object(object: &str) -> bool {
    object == "ld-linux" || is_libc_soname(object)
}

fn is_libc_soname(object: &str) -> bool {
    let Some(version) = object.strip_prefix("libc.so.") else {
        return false;
    };
    !version.is_empty() && version.chars().all(|c| c.is_ascii_digit())
}

fn cpython_extension_module(object: &str) -> Option<&str> {
    let (module, suffix) = object.split_once(".cpython-")?;
    let abi = suffix.strip_suffix(".so")?;
    if module.is_empty() || abi.is_empty() {
        return None;
    }
    Some(module)
}

fn is_libffi_soname(object: &str) -> bool {
    let Some(version) = object.strip_prefix("libffi.so.") else {
        return false;
    };
    !version.is_empty()
        && version.chars().all(|c| c.is_ascii_digit() || c == '.')
        && version.chars().any(|c| c.is_ascii_digit())
}
fn is_loader_soname(object: &str) -> bool {
    let Some(rest) = object.strip_prefix("ld-") else {
        return false;
    };
    let Some(index) = rest.find(".so.") else {
        return false;
    };

    let loader_name = &rest[..index];
    let soname_version = &rest[index + ".so.".len()..];
    !loader_name.is_empty()
        && loader_name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
        && !soname_version.is_empty()
        && soname_version.chars().all(|c| c.is_ascii_digit())
}
