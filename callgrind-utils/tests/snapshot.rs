//! Golden snapshot tests over the `testdata/*.c` fixtures.
//!
//! Each case compiles its fixture and profiles it with the in-repo Callgrind
//! (`vg-in-place`, expected at the repo root), then snapshots the folded
//! stacks. The fixtures run with `--instr-atstart=no` (plus client requests)
//! and `--obj-skip`, so the graph is just their own functions and the folded
//! output is stable across platforms.
//!
//! These tests require a built `./vg-in-place` at the repo root.
use std::env::consts::ARCH;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::process::Command;

use callgrind_utils::model::CallGraph;
use rstest::rstest;

/// Repo root: this crate lives at `<repo>/callgrind-utils`.
fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("crate has a parent directory")
        .to_path_buf()
}

fn vg_in_place() -> PathBuf {
    let path = repo_root().join("vg-in-place");
    assert!(
        path.is_file(),
        "vg-in-place not found at {} - build Valgrind in place first",
        path.display()
    );
    path
}

/// Compile `testdata/<stem>.c` into this test binary's temp dir. `-O0` keeps the
/// default fixtures un-inlined and `-g` gives them debug names; `callgrind.h`
/// pulls in `valgrind.h` via `-I include`.
fn compile_fixture_with_flags(stem: &str, cflags: &[&str]) -> PathBuf {
    let repo = repo_root();
    let src = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("testdata")
        .join(format!("{stem}.c"));
    let bin = Path::new(env!("CARGO_TARGET_TMPDIR")).join(stem);

    let status = Command::new("cc")
        .arg("-g")
        .arg("-I")
        .arg(repo.join("callgrind"))
        .arg("-I")
        .arg(repo.join("include"))
        .arg("-o")
        .arg(&bin)
        .arg(&src)
        // Flags (including `-l` libs) go after the source so link-order
        // sensitive libraries resolve symbols the source object needs.
        .args(cflags)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn cc for {stem}: {e}"));
    assert!(
        status.success(),
        "cc failed for {} ({status})",
        src.display()
    );
    bin
}

fn compile_fixture(stem: &str) -> PathBuf {
    compile_fixture_with_flags(stem, &["-O0"])
}

fn runner_callgrind_args(out_file: &Path) -> Vec<String> {
    [
        "-q",
        "--trace-children=yes",
        "--cache-sim=yes",
        "--I1=32768,8,64",
        "--D1=32768,8,64",
        "--LL=8388608,16,64",
        "--instr-atstart=no",
        "--collect-systime=nsec",
        "--read-inline-info=yes",
        "--tool=callgrind",
        "--compress-strings=no",
        "--combine-dumps=yes",
        "--dump-line=no",
    ]
    .into_iter()
    .map(str::to_string)
    .chain([format!("--callgrind-out-file={}", out_file.display())])
    .collect()
}

fn run_callgrind_with_runner_args(bin: &Path, out_file: &Path) -> String {
    let log_file = out_file.with_extension("valgrind.log");
    let status = Command::new("setarch")
        .arg(ARCH)
        .arg("--addr-no-randomize")
        .arg(vg_in_place())
        .args(runner_callgrind_args(out_file))
        .arg(format!("--log-file={}", log_file.display()))
        .arg(bin)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn setarch/vg-in-place: {e}"));
    assert!(status.success());
    std::fs::read_to_string(out_file).unwrap_or_else(|e| panic!("read {}: {e}", out_file.display()))
}

/// Profile `bin` with the same Callgrind flags as the runner and return the
/// `.out` contents.
fn run_callgrind(bin: &Path) -> String {
    let out_file = bin.with_extension("callgrind.out");
    run_callgrind_with_runner_args(bin, &out_file)
}

#[rstest]
#[case("recursion")]
#[case("chain")]
#[case("diamond")]
#[case("mutual")]
#[case("fractal")]
fn fixture_canonical_json(#[case] stem: &str) {
    let bin = compile_fixture(stem);
    let raw = run_callgrind(&bin);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse {stem} callgrind output: {e:?}"))
        .redact();
    graph
        .to_flamegraph_file(format!("{stem}.partial.svg"))
        .unwrap();
    insta::assert_snapshot!(
        format!("{stem}_folded"),
        graph.to_folded_without_costs().join("\n")
    );
}

/// AArch64-specific unwinding reproducers, built at `-O2` (see each fixture's
/// header comment for the shadow-stack scenario it targets). Golden snapshots
/// are only ever generated on aarch64, so this stays out of the cross-arch
/// `fixture_canonical_json` cases above.
#[cfg(target_arch = "aarch64")]
#[rstest]
// #[case("arm64_recursive_return")]
// #[case("arm64_tail_call")]
// #[case("arm64_free_during_recursion")]
// #[case("arm64_multi_alloc_cycle")]
// #[case("arm64_libm_recursion")]
// #[case("arm64_ping_pong_recursion")]
// #[case("arm64_longjmp_unwind")]
// #[case("arm64_deep_tailcall_chain")]
// #[case("arm64_wrapped_alloc_chain")]
#[case("arm64_plt_phantom_recursion")]
#[case("arm64_free_tailcall_phantom")]
fn arm64_fixture_canonical_json(#[case] stem: &str) {
    // `-lm` is harmless for fixtures that don't need libm and required by
    // arm64_libm_recursion, which does.
    let bin = compile_fixture_with_flags(stem, &["-O2", "-lm"]);
    let raw = run_callgrind(&bin);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse {stem} callgrind output: {e:?}"))
        .redact();

    insta::assert_snapshot!(
        format!("{stem}_folded"),
        graph.to_folded_without_costs().join("\n")
    );
}

/// Profile `bin` with the same Callgrind flags as the runner and return the
/// raw, unredacted graph input. The production runner uses
/// `--instr-atstart=no`, so this intentionally does not capture a separate
/// full-program trace.
fn run_callgrind_full(bin: &Path) -> String {
    let out_file = bin.with_extension("full.callgrind.out");
    run_callgrind_with_runner_args(bin, &out_file)
}

#[rstest]
#[case("recursion")]
#[case("chain")]
#[case("diamond")]
#[case("mutual")]
#[case("fractal")]
fn fixture_full_trace(#[case] stem: &str) {
    let bin = compile_fixture(stem);
    let raw = run_callgrind_full(&bin);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse {stem} full callgrind output: {e:?}"));
    graph
        .to_flamegraph_file(format!("{stem}.full.svg"))
        .unwrap();

    insta::assert_snapshot!(
        format!("{stem}_full_folded"),
        graph.to_folded_without_costs().join("\n")
    );
}
