//! Symbolization of anonymous JIT frames via a `perf-<pid>.map` file.
//!
//! Callgrind emits anonymous JIT code (CPython's `-X perf` trampolines, V8, ...)
//! as the literal absolute address `0x...`, leaving symbolization to the
//! backend. CPython writes one trampoline per code object plus a
//! `/tmp/perf-<pid>.map` line `<start-hex> <size-hex> py::<qualname>:<file>`, so
//! an address that falls in a trampoline's range resolves to its Python name.

use std::collections::HashMap;
use std::io::BufRead;
use std::path::Path;

use super::model::{CallGraph, Node};

/// A parsed `perf-<pid>.map`: half-open `[start, end)` address ranges, each
/// mapped to a symbol, sorted by `start` for binary search.
pub struct PerfMap {
    entries: Vec<(u64, u64, String)>,
}

impl PerfMap {
    pub fn from_file(path: impl AsRef<Path>) -> std::io::Result<Self> {
        let file = std::fs::File::open(path)?;
        Ok(Self::from_reader(std::io::BufReader::new(file)))
    }

    pub fn from_reader(reader: impl BufRead) -> Self {
        let mut entries: Vec<(u64, u64, String)> = reader
            .lines()
            .map_while(Result::ok)
            .filter_map(|line| parse_entry(&line))
            .collect();
        entries.sort_by_key(|(start, _, _)| *start);
        Self { entries }
    }

    /// Resolve an address to its symbol, or `None` if it falls in no range.
    pub fn resolve(&self, addr: u64) -> Option<&str> {
        let index = self.entries.partition_point(|(start, _, _)| *start <= addr);
        let (start, end, name) = self.entries.get(index.checked_sub(1)?)?;
        (*start..*end).contains(&addr).then_some(name.as_str())
    }
}

/// A perf-map line is `<start-hex> <size-hex> <symbol>`; anything else (blank
/// lines, comments) is skipped.
fn parse_entry(line: &str) -> Option<(u64, u64, String)> {
    let mut parts = line.splitn(3, ' ');
    let start = u64::from_str_radix(parts.next()?, 16).ok()?;
    let size = u64::from_str_radix(parts.next()?, 16).ok()?;
    let symbol = parts.next()?.trim();
    (!symbol.is_empty()).then(|| (start, start.wrapping_add(size), symbol.to_string()))
}

impl CallGraph {
    /// Rename anonymous JIT nodes (`0x...`) to their `perf-<pid>.map` symbol.
    ///
    /// The perf symbol embeds the source path (`py::fib:/abs/path/fractal.py`);
    /// the path is split into the node's `file` (basename only, so snapshots
    /// stay portable) and the `py::`-prefixed name stays as the function.
    pub fn symbolize_perf_map(self, map: &PerfMap) -> CallGraph {
        let CallGraph {
            mut nodes,
            mut edges,
            self_costs,
        } = self;

        // Self costs are re-keyed onto the symbolized identities, summing where
        // distinct addresses collapse to the same resolved name.
        let mut self_cost_map: HashMap<Node, u64> = HashMap::new();
        for (node, &cost) in nodes.iter().zip(self_costs.iter()) {
            let mut symbolized = node.clone();
            symbolize_node(&mut symbolized, map);
            *self_cost_map.entry(symbolized).or_insert(0) += cost;
        }

        for node in &mut nodes {
            symbolize_node(node, map);
        }
        for edge in &mut edges {
            symbolize_node(&mut edge.caller, map);
            symbolize_node(&mut edge.callee, map);
        }

        CallGraph::from_parts(nodes, edges, self_cost_map)
    }
}

/// Rename an anonymous JIT node (`0x...`) to its `perf-<pid>.map` symbol.
///
/// The perf symbol embeds the source path (`py::fib:/abs/path/fractal.py`);
/// the path is split into the node's `file` (basename only, so snapshots stay
/// portable) and the `py::`-prefixed name stays as the function. Nodes whose
/// function is not a resolvable address are left untouched.
fn symbolize_node(node: &mut Node, map: &PerfMap) {
    // Callgrind appends a `'N` recursion marker (e.g. `0x1234'2`); strip it to
    // resolve the address, then re-attach it so the marker survives on the
    // resolved name like on native frames.
    let (base, cycle) = split_cycle_suffix(&node.function);
    let Some(addr) = parse_hex_address(base) else {
        return;
    };
    let Some(symbol) = map.resolve(addr) else {
        return;
    };
    let (name, file) = split_symbol_file(symbol);
    node.function = format!("{name}{cycle}");
    if let Some(file) = file {
        node.file = file;
    }
}

fn parse_hex_address(name: &str) -> Option<u64> {
    let hex = name.strip_prefix("0x")?;
    u64::from_str_radix(hex, 16).ok()
}

/// Split a trailing Callgrind recursion marker `'<digits>` off a node name,
/// returning (base, marker-including-quote). No marker yields an empty suffix.
fn split_cycle_suffix(name: &str) -> (&str, &str) {
    let Some((base, digits)) = name.rsplit_once('\'') else {
        return (name, "");
    };
    if !digits.is_empty() && digits.bytes().all(|b| b.is_ascii_digit()) {
        return (base, &name[base.len()..]);
    }
    (name, "")
}

/// Split `py::<qualname>:<path>` into (`py::<qualname>`, basename of `<path>`).
/// The trailing `:` separates the file, so split on the last one; a symbol
/// without it (rare) keeps its name and gets no file.
fn split_symbol_file(symbol: &str) -> (String, Option<String>) {
    let Some((name, path)) = symbol.rsplit_once(':') else {
        return (symbol.to_string(), None);
    };
    let base = path
        .rsplit('/')
        .next()
        .filter(|p| !p.is_empty())
        .unwrap_or(path);
    (name.to_string(), Some(base.to_string()))
}
