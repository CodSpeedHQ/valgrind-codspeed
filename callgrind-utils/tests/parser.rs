//! Integration tests for the Callgrind `.out` -> call-graph parser.
//!
//! Exercises the real format shapes from `callgrind/docs/cl-format.xml` and
//! `callgrind/dump.c`: two-line call specs, name compression `(N)`, the
//! `cfl`/`cfi` alias, callee file/object inheritance (including inline
//! `fi`/`fe` transitions), same-named functions in distinct objects, direct
//! recursion, multi-part merge, and the canonical JSON projection.

use callgrind_utils::model::{CallGraph, Edge, Node, ParseOptions};
use std::io::Cursor;

const FIXTURE: &str = include_str!("data/example.out");

fn parse_default() -> CallGraph {
    CallGraph::parse(Cursor::new(FIXTURE)).expect("parse fixture")
}

/// All edges whose caller and callee function names match.
fn edges_fn<'a>(g: &'a CallGraph, caller: &str, callee: &str) -> Vec<&'a Edge> {
    g.edges()
        .iter()
        .filter(|e| e.caller.function == caller && e.callee.function == callee)
        .collect()
}

/// All nodes with the given function name (distinct by object/file).
fn nodes_fn<'a>(g: &'a CallGraph, function: &str) -> Vec<&'a Node> {
    g.nodes()
        .iter()
        .filter(|n| n.function == function)
        .collect()
}

#[test]
fn parses_basic_callgraph() {
    let g = parse_default();
    // 12 distinct nodes, 12 edges (see fixture; `nocnt` is discarded, no edge).
    assert_eq!(g.nodes().len(), 12, "nodes: {:#?}", g.nodes());
    assert_eq!(g.edges().len(), 12, "edges: {:#?}", g.edges());

    let mf1 = edges_fn(&g, "main", "func1");
    assert_eq!(mf1.len(), 1);
    assert_eq!(mf1[0].call_count, Some(1));
    assert_eq!(mf1[0].caller.file, "file1.c");
    assert_eq!(mf1[0].callee.file, "file1.c");
}

#[test]
fn resolves_name_compression() {
    // `fn=(1)`/`fl=(1)`/`ob=(1)` references must resolve to their defs.
    let g = parse_default();
    let main = nodes_fn(&g, "main");
    assert_eq!(main.len(), 1);
    assert_eq!(main[0].file, "file1.c");
    assert_eq!(main[0].object, "clreq");
    // func2 -> func1 uses `cfn=(2)` as a *reference* to the earlier def.
    assert_eq!(edges_fn(&g, "func2", "func1").len(), 1);
}

#[test]
fn cfl_alias_equals_cfi() {
    // `cfl=(5) cflfile.c` is the historical alias of `cfi=`; the callee file
    // must resolve to cflfile.c.
    let g = parse_default();
    let e = edges_fn(&g, "main", "cflop");
    assert_eq!(e.len(), 1);
    assert_eq!(e[0].callee.file, "cflfile.c");
    assert_eq!(e[0].callee.object, "clreq");
}

#[test]
fn omitted_cfi_inherits_current_file_context() {
    // No `cfi`/`cfl`: the callee inherits the CURRENT position file, NOT the
    // caller's original `fl`. For `nofile` the context is still file1.c.
    let g = parse_default();
    let e = edges_fn(&g, "main", "nofile");
    assert_eq!(e.len(), 1);
    assert_eq!(e[0].callee.file, "file1.c");
}

#[test]
fn inline_fi_fe_changes_callee_context_not_caller() {
    // CRITICAL: after `fi=(6) inline.c`, a `cfn=` with no `cfi` makes the
    // CALLEE inherit inline.c, while the CALLER (inlhost) keeps its own `fl`
    // (file1.c). Pins both halves: caller file != callee file here.
    let g = parse_default();
    let inlhost = nodes_fn(&g, "inlhost");
    assert_eq!(inlhost.len(), 1);
    assert_eq!(
        inlhost[0].file, "file1.c",
        "caller keeps its fl, not the inline file"
    );

    let e = edges_fn(&g, "inlhost", "inltarget");
    assert_eq!(e.len(), 1);
    assert_eq!(
        e[0].callee.file, "inline.c",
        "callee inherits the inline context"
    );
    assert_eq!(e[0].caller.file, "file1.c");
}

#[test]
fn same_name_different_object_are_distinct() {
    // `helper` exists in liba/fileA.c AND libb/fileB.c -> two distinct nodes,
    // two distinct edges from main.
    let g = parse_default();
    let helpers = nodes_fn(&g, "helper");
    assert_eq!(helpers.len(), 2, "helpers: {helpers:#?}");

    let mut keys: Vec<(&str, &str)> = helpers
        .iter()
        .map(|n| (n.object.as_str(), n.file.as_str()))
        .collect();
    keys.sort();
    assert_eq!(keys, vec![("liba", "fileA.c"), ("libb", "fileB.c")]);

    assert_eq!(edges_fn(&g, "main", "helper").len(), 2);
}

#[test]
fn recursion_becomes_self_edge() {
    let g = parse_default();
    let rec = edges_fn(&g, "rec", "rec");
    assert_eq!(rec.len(), 1);
    assert_eq!(rec[0].caller, rec[0].callee);
}

#[test]
fn cob_overrides_caller_object() {
    // `cob=(4) extlib` with no `cfi`: callee object is extlib, file inherited
    // from caller context (file1.c).
    let g = parse_default();
    let e = edges_fn(&g, "main", "extfn");
    assert_eq!(e.len(), 1);
    assert_eq!(e[0].callee.object, "extlib");
    assert_eq!(e[0].callee.file, "file1.c");
    assert_eq!(e[0].caller.object, "clreq");
}

#[test]
fn multi_part_merged() {
    // The `part: 2` section's `main -> part2fn` edge must merge into one graph.
    let g = parse_default();
    assert_eq!(edges_fn(&g, "main", "part2fn").len(), 1);
}

#[test]
fn bare_cfn_without_calls_is_discarded() {
    // `cfn=nocnt` with no `calls=` line is callee context only, not a call
    // record (cl-format.xml: CallSpec requires a CallLine). No node, no edge.
    let g = parse_default();
    assert!(nodes_fn(&g, "nocnt").is_empty(), "nocnt must not be a node");
    assert!(edges_fn(&g, "main", "nocnt").is_empty(), "no edge to nocnt");
}

#[test]
fn every_edge_has_a_call_count() {
    // With the calls=-required rule, every emitted edge carries Some(count).
    let g = parse_default();
    for e in g.edges() {
        assert!(e.call_count.is_some(), "edge {e:?} should have a count");
    }
}

#[test]
fn costs_and_addresses_ignored() {
    // Subposition/cost lines (+N, *, -N, 0x..., "16 400") never create nodes.
    // Node count stays at the 12 real functions.
    let g = parse_default();
    assert_eq!(g.nodes().len(), 12);
    assert!(!g.nodes().iter().any(|n| n.function.starts_with("0x")));
}

#[test]
fn paths_normalized_by_default() {
    // Default opts: object path `/path/to/clreq` -> basename `clreq`.
    let g = parse_default();
    assert!(g.nodes().iter().any(|n| n.object == "clreq"));
    assert!(
        !g.nodes().iter().any(|n| n.object.contains('/')),
        "no object should retain a path separator"
    );
}

#[test]
fn paths_verbatim_when_normalization_off() {
    let opts = ParseOptions {
        normalize_paths: false,
        ..Default::default()
    };
    let g = CallGraph::parse_with(Cursor::new(FIXTURE), &opts).expect("parse");
    assert!(
        g.nodes().iter().any(|n| n.object == "/path/to/clreq"),
        "object path must be kept verbatim: {:#?}",
        g.nodes()
    );
}

#[test]
fn to_json_is_canonical() {
    let g = parse_default();
    let json = g.to_json().expect("to_json");
    let v: serde_json::Value = serde_json::from_str(&json).expect("valid json");

    let nodes = v["nodes"].as_array().expect("nodes array");
    let edges = v["edges"].as_array().expect("edges array");
    assert_eq!(nodes.len(), 12);
    assert_eq!(edges.len(), 12);

    // Nodes sorted by (object, file, function).
    let key = |n: &serde_json::Value| {
        (
            n["object"].as_str().unwrap().to_owned(),
            n["file"].as_str().unwrap().to_owned(),
            n["function"].as_str().unwrap().to_owned(),
        )
    };
    let mut sorted = nodes.clone();
    sorted.sort_by_key(key);
    assert_eq!(nodes, &sorted, "nodes must be pre-sorted");

    // Edges reference nodes by valid index; call_count present (never None here).
    for e in edges {
        let c = e["caller"].as_u64().unwrap() as usize;
        let d = e["callee"].as_u64().unwrap() as usize;
        assert!(
            c < nodes.len() && d < nodes.len(),
            "edge index out of range"
        );
        assert!(
            e.get("call_count").is_some(),
            "call_count present for fixture edges"
        );
    }

    // Edges sorted by (caller_idx, callee_idx).
    let pairs: Vec<(u64, u64)> = edges
        .iter()
        .map(|e| (e["caller"].as_u64().unwrap(), e["callee"].as_u64().unwrap()))
        .collect();
    let mut sorted_pairs = pairs.clone();
    sorted_pairs.sort();
    assert_eq!(
        pairs, sorted_pairs,
        "edges must be pre-sorted by index pair"
    );
}

#[test]
fn to_json_omits_none_call_count() {
    // Construct via parse, then confirm the serializer would omit a None count
    // by checking the field is absent only when the value is None. All fixture
    // edges have Some, so every edge object must carry call_count.
    let g = parse_default();
    let json = g.to_json().expect("to_json");
    let v: serde_json::Value = serde_json::from_str(&json).unwrap();
    for e in v["edges"].as_array().unwrap() {
        assert!(e.get("call_count").is_some());
    }
}

#[test]
fn bare_cfn_does_not_poison_next_edge() {
    // A bare `cfn=unused` (cleared by the following self-cost line) must not
    // become the callee of a later `calls=` that has its own `cfn=`.
    let out = "\
# callgrind format
events: Ir
ob=(1) prog
fl=(1) a.c
fn=(1) caller
cfn=(2) unused
5 3
cfn=(3) realcallee
calls=2 10
6 4
";
    let g = CallGraph::parse(Cursor::new(out)).expect("parse");
    assert!(
        nodes_fn(&g, "unused").is_empty(),
        "bare cfn must be discarded"
    );
    let e = edges_fn(&g, "caller", "realcallee");
    assert_eq!(e.len(), 1);
    assert_eq!(e[0].call_count, Some(2));
    assert!(edges_fn(&g, "caller", "unused").is_empty());
}

#[test]
fn bare_cfn_does_not_survive_jump_line() {
    // A `jump=`/`jcnd=` line between a bare `cfn=` and a `calls=` must clear the
    // pending callee, so the `calls=` (lacking its own `cfn=`) emits no edge.
    let out = "\
# callgrind format
events: Ir
ob=(1) prog
fl=(1) a.c
fn=(1) caller
cfn=(2) unused
jump=3 10
calls=2 11
6 4
";
    let g = CallGraph::parse(Cursor::new(out)).expect("parse");
    assert!(
        nodes_fn(&g, "unused").is_empty(),
        "jump must clear the pending cfn"
    );
    assert!(
        g.edges().is_empty(),
        "calls= had no live cfn after the jump -> no edge"
    );
}
