use std::collections::HashMap;

use crate::{
    error::ParseError,
    model::{CallGraph, Edge, Node, ParseOptions},
};

mod normalize;

/// Header/auxiliary keys that carry no call-graph topology and are dropped
/// outright. `part`/`thread` are handled separately (context boundaries),
/// not here. `cfni` is an inline-function annotation, not a callee spec.
const SKIP_KEYS: &[&str] = &[
    "version",
    "creator",
    "pid",
    "cmd",
    "desc",
    "positions",
    "events",
    "event",
    "summary",
    "totals",
    "rec",
    "jfi",
    "jfn",
    "frfn",
    "cfni",
    "jump",
    "jcnd",
];

impl CallGraph {
    /// Parse a Callgrind `.out` stream into a call graph.
    ///
    /// The format is line-oriented (see `callgrind/docs/cl-format.xml`). We
    /// track three independent name-compression ID spaces (functions, files,
    /// objects), the current caller context, and a pending callee record.
    /// An edge is emitted only when a `calls=` line closes a record that has a
    /// pending `cfn=`; a bare `cfn=` is callee context that gets discarded.
    pub fn parse(reader: impl std::io::BufRead) -> Result<Self, ParseError> {
        Self::parse_with(reader, &ParseOptions::default())
    }

    /// Parse with explicit [`ParseOptions`] (e.g. to disable path normalization).
    pub fn parse_with(
        reader: impl std::io::BufRead,
        opts: &ParseOptions,
    ) -> Result<Self, ParseError> {
        // Three SEPARATE name-compression ID spaces.
        let mut fn_ids: HashMap<u32, String> = HashMap::new();
        let mut file_ids: HashMap<u32, String> = HashMap::new();
        let mut obj_ids: HashMap<u32, String> = HashMap::new();

        // Current caller context.
        let mut cur_obj: Option<String> = None;
        let mut cur_fl: Option<String> = None; // the function's own file (`fl=`)
        let mut cur_pos_file: Option<String> = None; // current position file (`fl`/`fi`/`fe`)
        let mut cur_fn: Option<String> = None;

        // Pending callee record, built from `cob`/`cfi`/`cfl`/`cfn`.
        let mut pend_cob: Option<String> = None;
        let mut pend_cfi: Option<String> = None;
        let mut pend_cfn: Option<String> = None;

        let mut nodes: Vec<Node> = Vec::new();
        let mut edges: Vec<Edge> = Vec::new();

        // Self cost (first event column) accumulated per function-node identity.
        let mut self_costs: HashMap<Node, u64> = HashMap::new();

        // Cost-line layout, learned from the `positions:`/`events:` headers.
        // A cost line has exactly `num_positions + num_events` tokens; the first
        // event value lives at token index `num_positions`.
        let mut num_positions: usize = 1;
        let mut num_events: usize = 1;

        // Index of the edge whose inclusive cost the NEXT cost line supplies.
        // Set right after a `calls=` line, consumed by that call's cost line.
        let mut expect_call_cost: Option<usize> = None;

        for line in reader.lines() {
            let line = line?; // io error -> ParseError::Io (#[from])
            let trimmed = line.trim_start();

            // Blank lines and comments carry nothing.
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }

            let key = line_key(trimmed);

            // Cost-line layout headers. `positions: line` / `positions: instr line`
            // fixes the leading position-column count; `events: Ir Cy ...` fixes the
            // event-column count. The flamegraph weight is the FIRST event column.
            if key == "positions" {
                num_positions = header_token_count(trimmed, key).max(1);
                continue;
            }
            if key == "events" {
                num_events = header_token_count(trimmed, key).max(1);
                continue;
            }

            // `part:`/`thread:` separators bound a record: clear the pending
            // callee, but keep the ID maps and caller context (IDs persist
            // across parts; parts/threads are always merged into one graph).
            if key == "part" || key == "thread" {
                pend_cob = None;
                pend_cfi = None;
                pend_cfn = None;
                expect_call_cost = None;
                continue;
            }

            // Header/auxiliary lines carry no topology. Body-level skips
            // (`jump`/`jcnd`/`jfi`/`jfn`/`cfni`/`frfn`) must ALSO close any open
            // call record, so a bare `cfn=` cannot survive across them and
            // poison a later `calls=`. Clearing when nothing is pending is a
            // harmless no-op for true header lines.
            if SKIP_KEYS.contains(&key) {
                pend_cob = None;
                pend_cfi = None;
                pend_cfn = None;
                expect_call_cost = None;
                continue;
            }

            // Position specs and `calls` are `key=value`; a colon-separated
            // (`ob:`) or bare token is a header/cost/unknown line, never a spec.
            let assign = trimmed.as_bytes().get(key.len()) == Some(&b'=');

            // A `calls=` line closes a call record and emits the edge.
            if key == "calls" && assign {
                if let Some(cfn) = pend_cfn.take() {
                    let rhs = &trimmed[key.len() + 1..];
                    let call_count = parse_call_count(rhs);

                    // Caller file is the function's own `fl` (cur_fl), NEVER the
                    // current position file: an inline `fi=`/`fe=` transition
                    // moves the callee context but not the caller's identity.
                    let caller = make_node(
                        cur_fn.as_deref(),
                        cur_fl.as_deref(),
                        cur_obj.as_deref(),
                        opts,
                    );
                    // Callee inherits the current position file (which may be an
                    // inline `fi`/`fe` file) and the caller object unless the
                    // record overrode them with `cfi`/`cfl`/`cob`.
                    let callee_file = pend_cfi.as_deref().or(cur_pos_file.as_deref());
                    let callee_obj = pend_cob.as_deref().or(cur_obj.as_deref());
                    let callee = make_node(Some(cfn.as_str()), callee_file, callee_obj, opts);

                    nodes.push(caller.clone());
                    nodes.push(callee.clone());
                    edges.push(Edge {
                        caller,
                        callee,
                        call_count,
                        inclusive_cost: None,
                    });
                    // The next cost line carries this call's inclusive cost.
                    expect_call_cost = Some(edges.len() - 1);
                }
                // Whether or not an edge was emitted, the record is closed.
                pend_cob = None;
                pend_cfi = None;
                continue;
            }

            // Lines lacking an `=` after the key — colon headers (`ob:`), bare
            // tokens, and cost/address lines — are never specs or calls, so
            // they only close any open call record (a bare `cfn=` thus cannot
            // poison a later `calls=`).
            if !assign {
                let cost = parse_cost_value(trimmed, num_positions, num_events);
                match (cost, expect_call_cost.take()) {
                    // The cost line immediately following a `calls=`: inclusive
                    // cost of that call's callee subtree.
                    (Some(c), Some(edge_idx)) => {
                        edges[edge_idx].inclusive_cost = Some(c);
                    }
                    // A body cost line of the current function: self cost.
                    (Some(c), None) => {
                        if let Some(f) = cur_fn.as_deref() {
                            let node =
                                make_node(Some(f), cur_fl.as_deref(), cur_obj.as_deref(), opts);
                            *self_costs.entry(node).or_insert(0) += c;
                        }
                    }
                    // Not a cost line (colon header / bare token).
                    (None, _) => {}
                }
                pend_cob = None;
                pend_cfi = None;
                pend_cfn = None;
                continue;
            }

            // Recognized position specs dispatch below; an unknown `key=value`
            // falls to the `_` arm, which also closes the record. A spec line
            // means the call's cost line (if any) has passed.
            expect_call_cost = None;
            match key {
                "ob" => {
                    let x = parse_pos_name(rhs_of(trimmed, key), &mut obj_ids)?;
                    cur_obj = Some(x);
                    pend_cob = None;
                    pend_cfi = None;
                    pend_cfn = None;
                }
                "fl" => {
                    let x = parse_pos_name(rhs_of(trimmed, key), &mut file_ids)?;
                    cur_fl = Some(x.clone());
                    cur_pos_file = Some(x);
                    pend_cob = None;
                    pend_cfi = None;
                    pend_cfn = None;
                }
                "fi" | "fe" => {
                    // Inline-file transition: moves the position file only, not
                    // the function's own `fl`.
                    let x = parse_pos_name(rhs_of(trimmed, key), &mut file_ids)?;
                    cur_pos_file = Some(x);
                    pend_cob = None;
                    pend_cfi = None;
                    pend_cfn = None;
                }
                "fn" => {
                    let x = parse_pos_name(rhs_of(trimmed, key), &mut fn_ids)?;
                    cur_fn = Some(x);
                    pend_cob = None;
                    pend_cfi = None;
                    pend_cfn = None;
                }
                "cob" => {
                    let x = parse_pos_name(rhs_of(trimmed, key), &mut obj_ids)?;
                    pend_cob = Some(x);
                }
                "cfi" | "cfl" => {
                    // `cfl` is the historical alias of `cfi`; identical meaning.
                    let x = parse_pos_name(rhs_of(trimmed, key), &mut file_ids)?;
                    pend_cfi = Some(x);
                }
                "cfn" => {
                    // Do NOT clear pend_cob/pend_cfi: they legitimately precede
                    // cfn within the same call record.
                    let x = parse_pos_name(rhs_of(trimmed, key), &mut fn_ids)?;
                    pend_cfn = Some(x);
                }
                _ => {
                    // Cost/subposition lines and anything unrecognized close any
                    // dangling callee context.
                    pend_cob = None;
                    pend_cfi = None;
                    pend_cfn = None;
                }
            }
        }

        // Nothing to flush at EOF: a bare trailing `cfn=` is discarded.
        Ok(CallGraph::from_parts(nodes, edges, self_costs))
    }
}

/// Count the whitespace-separated tokens in a `positions:`/`events:` header
/// value (everything after the `key:` prefix). `positions: instr line` -> 2.
fn header_token_count(trimmed: &str, key: &str) -> usize {
    trimmed[key.len()..]
        .trim_start_matches([':', '='])
        .split_whitespace()
        .count()
}

/// First event value of a cost line, or `None` if `trimmed` is not one.
///
/// A cost line is `num_positions` position tokens followed by 1..=`num_events`
/// event counts; Callgrind omits trailing zero counts, so the value list is
/// variable-length. The first event column (`Ir`, token index `num_positions`)
/// is returned. Requiring the leading tokens to be position-like (line/instr,
/// possibly `+N`/`-N`/`*`/`0x..`) plus a decimal first value rejects colon
/// headers and bare tokens that also lack an `=`.
fn parse_cost_value(trimmed: &str, num_positions: usize, num_events: usize) -> Option<u64> {
    let tokens: Vec<&str> = trimmed.split_whitespace().collect();
    let has_valid_token_count =
        tokens.len() > num_positions && tokens.len() <= num_positions + num_events;
    if !has_valid_token_count {
        return None;
    }
    if !tokens[..num_positions].iter().all(|t| is_position_token(t)) {
        return None;
    }
    tokens[num_positions].parse::<u64>().ok()
}

/// Whether `tok` is a Callgrind position/subposition token: `*` (repeat), an
/// absolute decimal or `0x` address, or a `+N`/`-N` relative offset.
fn is_position_token(tok: &str) -> bool {
    if tok == "*" {
        return true;
    }
    if let Some(hex) = tok.strip_prefix("0x").or_else(|| tok.strip_prefix("0X")) {
        return !hex.is_empty() && hex.bytes().all(|b| b.is_ascii_hexdigit());
    }
    let digits = tok.strip_prefix(['+', '-']).unwrap_or(tok);
    !digits.is_empty() && digits.bytes().all(|b| b.is_ascii_digit())
}

/// The leading token of `line`: everything up to the first `=`, `:`, or
/// whitespace. For `fn=(1) main` this is `"fn"`; for `0x401000 4`, `"0x401000"`.
fn line_key(line: &str) -> &str {
    let end = line
        .find(|c: char| c == '=' || c == ':' || c.is_whitespace())
        .unwrap_or(line.len());
    &line[..end]
}

/// The value after `key=` in a position-spec line. Callers only invoke this for
/// keys known to be followed by `=`, so the separator byte is skipped directly.
fn rhs_of<'a>(trimmed: &'a str, key: &str) -> &'a str {
    &trimmed[key.len() + 1..]
}

/// Resolve a name-compression RHS against its ID map.
///
/// `(N) name` defines ID `N` -> `name` and returns the name; `(N)` references a
/// previously defined ID; a bare `name` (compression off) is returned verbatim
/// and never touches the map.
fn parse_pos_name(rhs: &str, map: &mut HashMap<u32, String>) -> Result<String, ParseError> {
    let rhs = rhs.trim_start();
    let Some(after_paren) = rhs.strip_prefix('(') else {
        // Compression off: literal name.
        return Ok(rhs.trim().to_owned());
    };

    // The entire substring before `)` is the numeric ID; everything after it
    // (split on the FIRST `)`, so names may themselves contain `)`) is the
    // optional name. An unterminated `(N` treats the remainder as the ID.
    let (num, rest) = after_paren.split_once(')').unwrap_or((after_paren, ""));
    let id: u32 = num.trim().parse()?; // non-numeric/empty id -> ParseError::BadId
    let name = rest.trim();

    if name.is_empty() {
        // Reference: resolve the prior definition (empty if unknown; the
        // normalizer maps empties to opts.unknown for files/objects).
        Ok(map.get(&id).cloned().unwrap_or_default())
    } else {
        map.insert(id, name.to_owned());
        Ok(name.to_owned())
    }
}

/// First token after `calls=`, parsed as a decimal or `0x`-hex count.
fn parse_call_count(rhs: &str) -> Option<u64> {
    let tok = rhs.split_whitespace().next()?;
    match tok.strip_prefix("0x").or_else(|| tok.strip_prefix("0X")) {
        Some(hex) => u64::from_str_radix(hex, 16).ok(),
        None => tok.parse::<u64>().ok(),
    }
}

/// Build a node. The function name keeps its raw text; file and object are
/// normalized (basename + unknown handling per `opts`). Absent/empty file and
/// object default to `opts.unknown` BEFORE normalizing so that disabling
/// `normalize_paths` cannot leave a blank node key.
fn make_node(
    function: Option<&str>,
    file: Option<&str>,
    object: Option<&str>,
    opts: &ParseOptions,
) -> Node {
    let or_unknown = |v: Option<&str>| {
        normalize::normalize_path(
            v.filter(|s| !s.is_empty()).unwrap_or(opts.unknown.as_str()),
            opts,
        )
    };
    let function = match function {
        Some(f) if !f.is_empty() => f.to_owned(),
        _ => opts.unknown.clone(),
    };
    Node {
        function,
        file: or_unknown(file),
        object: or_unknown(object),
    }
}
