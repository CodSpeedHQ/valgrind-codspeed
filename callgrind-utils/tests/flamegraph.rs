//! Tests for the collapsed-stack / flamegraph projection.

use callgrind_utils::error::FlamegraphError;
use callgrind_utils::model::CallGraph;
use std::io::Cursor;

fn parse(out: &str) -> CallGraph {
    CallGraph::parse(Cursor::new(out)).expect("parse")
}

fn folded_sorted(g: &CallGraph) -> Vec<String> {
    let mut lines = g.to_folded();
    lines.sort();
    lines
}

const LINEAR: &str = "\
part: 1
pid: 1
positions: line
events: Ir
fn=main
10 5
cfn=work
calls=1 90
11 90

fn=work
20 40
cfn=leaf
calls=1 50
21 50

fn=leaf
30 50
";

#[test]
fn folds_linear_chain_with_self_costs() {
    let g = parse(LINEAR);
    assert_eq!(
        folded_sorted(&g),
        vec![
            "main 5".to_string(),
            "main;work 40".to_string(),
            "main;work;leaf 50".to_string(),
        ]
    );
}

#[test]
fn renders_svg() {
    let g = parse(LINEAR);
    let svg = g.to_flamegraph().expect("svg");
    assert!(svg.contains("<svg"), "expected an SVG document");
    assert!(svg.contains("main"), "expected frame labels in the SVG");
}

const SHARED: &str = "\
part: 1
pid: 1
positions: line
events: Ir
fn=root
10 0
cfn=a
calls=1 20
11 20
cfn=b
calls=1 10
12 10

fn=a
20 0
cfn=shared
calls=1 20
21 20

fn=b
30 0
cfn=shared
calls=1 10
31 10

fn=shared
40 30
";

#[test]
fn distributes_shared_callee_by_inclusive_cost() {
    let g = parse(SHARED);
    assert_eq!(
        folded_sorted(&g),
        vec![
            "root;a;shared 20".to_string(),
            "root;b;shared 10".to_string(),
        ]
    );
}

const RECURSION: &str = "\
part: 1
pid: 1
positions: line
events: Ir
fn=rec
10 5
cfn=rec
calls=1 3
11 3
";

#[test]
fn recursion_does_not_loop() {
    let g = parse(RECURSION);
    let lines = folded_sorted(&g);
    assert!(lines.iter().all(|l| l.matches("rec").count() <= 2));
    assert!(!lines.is_empty());
}

const SEEDED: &str = "\
part: 1
pid: 1
positions: line
events: Ir
fn=entry
10 5
cfn=hot
calls=1 0
11 0

fn=hot
20 100
";

#[test]
fn heavy_frame_behind_zero_cost_edge_survives() {
    let g = parse(SEEDED);
    let folded = folded_sorted(&g);
    assert!(
        folded.iter().any(|l| l == "hot 100"),
        "hot's self cost must survive a zero-cost incoming edge; got {folded:?}"
    );
    let total: u64 = folded
        .iter()
        .map(|l| l.rsplit(' ').next().unwrap().parse::<u64>().unwrap())
        .sum();
    assert_eq!(total, 105, "entry(5) + hot(100)");
}

const SPARSE: &str = "\
part: 1
pid: 1
positions: instr line
events: Ir Dr Dw
fn=main
0x1000 10 7
+4 11 3 0
cfn=leaf
calls=1 0 0
0x2000 12 20

fn=leaf
0x3000 20 20 0 0
";

#[test]
fn parses_sparse_instr_line_cost_lines() {
    let g = parse(SPARSE);
    assert_eq!(
        folded_sorted(&g),
        vec!["main 10".to_string(), "main;leaf 20".to_string()],
        "self=7+3 for main, inclusive/self=20 for leaf"
    );
}

#[test]
fn no_cost_data_is_an_error() {
    let out = "\
part: 1
pid: 1
positions: line
events: Ir
fn=main
0 0
cfn=child
calls=1 0
0 0

fn=child
0 0
";
    let g = parse(out);
    assert!(matches!(g.to_flamegraph(), Err(FlamegraphError::NoCost)));
}
