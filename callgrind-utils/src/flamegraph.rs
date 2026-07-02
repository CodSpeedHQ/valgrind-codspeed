use inferno::flamegraph::{self, Options};

use super::{error::FlamegraphError, model::CallGraph};

const MIN_BUDGET: f64 = 1.0;
const MIN_BUDGET_FRACTION: f64 = 0.0005;

impl CallGraph {
    pub fn to_folded_without_costs(&self) -> Vec<String> {
        self.to_folded()
            .iter()
            .map(|line| {
                let mut parts = line.split_whitespace();
                let stack = parts.next().unwrap_or_default();
                format!("{stack} <cost>")
            })
            .collect()
    }

    pub fn to_folded(&self) -> Vec<String> {
        let nodes = self.nodes();
        let n = nodes.len();
        let names: Vec<&str> = nodes.iter().map(|node| node.function.as_str()).collect();
        let self_costs: Vec<u64> = (0..n).map(|i| self.self_cost(i)).collect();

        let mut out = vec![Vec::<(usize, u64)>::new(); n];
        let mut incoming_incl = vec![0u64; n];
        for edge in self.edges() {
            let Some(caller) = self.node_index(&edge.caller) else {
                continue;
            };
            let Some(callee) = self.node_index(&edge.callee) else {
                continue;
            };
            let inclusive_cost = edge.inclusive_cost.unwrap_or(0);
            out[caller].push((callee, inclusive_cost));
            incoming_incl[callee] += inclusive_cost;
        }

        let incl: Vec<u64> = (0..n)
            .map(|i| self_costs[i] + out[i].iter().map(|(_, cost)| *cost).sum::<u64>())
            .collect();

        let roots = roots(&incoming_incl, &incl);
        let total: f64 = roots.iter().map(|(_, budget)| *budget).sum();
        let min_budget = MIN_BUDGET.max(total * MIN_BUDGET_FRACTION);

        let mut lines = Vec::new();
        let mut stack = Vec::new();
        let mut on_path = vec![false; n];
        for (root, budget) in roots {
            fold_dfs(
                root,
                budget,
                min_budget,
                &mut stack,
                &mut on_path,
                &out,
                &incl,
                &self_costs,
                &names,
                &mut lines,
            );
        }
        lines
    }

    pub fn to_flamegraph(&self) -> Result<String, FlamegraphError> {
        let lines = self.to_folded();
        if lines.is_empty() {
            return Err(FlamegraphError::NoCost);
        }

        let mut opts = Options::default();
        opts.title = "Callgrind".to_string();
        opts.count_name = "instructions".to_string();

        let mut svg = Vec::new();
        flamegraph::from_lines(&mut opts, lines.iter().map(String::as_str), &mut svg)
            .map_err(|e| FlamegraphError::Inferno(e.to_string()))?;
        String::from_utf8(svg).map_err(|e| FlamegraphError::Inferno(e.to_string()))
    }

    pub fn to_flamegraph_file(
        &self,
        path: impl AsRef<std::path::Path>,
    ) -> Result<(), FlamegraphError> {
        let svg = self.to_flamegraph()?;
        std::fs::write(path, svg)?;
        Ok(())
    }
}

fn roots(incoming_incl: &[u64], incl: &[u64]) -> Vec<(usize, f64)> {
    let roots: Vec<(usize, f64)> = (0..incl.len())
        .filter_map(|i| {
            let uncovered = incl[i].saturating_sub(incoming_incl[i]);
            (uncovered > 0).then_some((i, uncovered as f64))
        })
        .collect();
    if !roots.is_empty() {
        return roots;
    }
    (0..incl.len())
        .filter(|&i| incl[i] > 0)
        .max_by_key(|&i| incl[i])
        .map(|i| (i, incl[i] as f64))
        .into_iter()
        .collect()
}

#[allow(clippy::too_many_arguments)]
fn fold_dfs(
    node: usize,
    budget: f64,
    min_budget: f64,
    stack: &mut Vec<usize>,
    on_path: &mut [bool],
    out: &[Vec<(usize, u64)>],
    incl: &[u64],
    self_costs: &[u64],
    names: &[&str],
    lines: &mut Vec<String>,
) {
    let should_prune = budget < min_budget || incl[node] == 0;
    if should_prune {
        return;
    }

    stack.push(node);
    on_path[node] = true;

    let frac = (budget / incl[node] as f64).min(1.0);
    let self_here = (self_costs[node] as f64 * frac).round() as u64;
    if self_here >= 1 {
        lines.push(fold_line(stack, names, self_here));
    }

    for &(child, edge_incl) in &out[node] {
        let child_budget = edge_incl as f64 * frac;
        if !on_path[child] {
            fold_dfs(
                child,
                child_budget,
                min_budget,
                stack,
                on_path,
                out,
                incl,
                self_costs,
                names,
                lines,
            );
            continue;
        }
        let recursive = child_budget.round() as u64;
        if recursive >= 1 {
            stack.push(child);
            lines.push(fold_line(stack, names, recursive));
            stack.pop();
        }
    }

    on_path[node] = false;
    stack.pop();
}

fn fold_line(stack: &[usize], names: &[&str], count: u64) -> String {
    let mut line = String::new();
    for (i, &idx) in stack.iter().enumerate() {
        if i > 0 {
            line.push(';');
        }
        line.push_str(names[idx]);
    }
    line.push(' ');
    line.push_str(&count.to_string());
    line
}
