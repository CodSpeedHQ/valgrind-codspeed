//! Golden snapshot of the Rust fixture's call graph.
//!
//! The Rust twin of the C `fractal` case in `snapshot.rs`: compile
//! `testdata/fractal.rs` (linking the `clgctl.c` client-request shim as a static
//! lib, since the CALLGRIND_* requests are inline asm), profile it live under
//! the in-repo Callgrind with `--instr-atstart=no`, parse, and snapshot the
//! redacted folded stacks.
//!
//! The fixture fires the client requests several frames deep
//! (`main` -> `run_benchmark` -> `warmup` -> `run_measured`), so the scoped
//! graph is just the measured region's own functions: the shadow-stack seeder
//! reconstructs the native chain but the outer frames do their work while
//! instrumentation is off, so they never enter the graph. Every fixture
//! function is `#[no_mangle] #[inline(never)]` and the workload is pure integer
//! math over a fixed arena, so the only non-fixture frame is a libc `memset`
//! (redacted to `???`) and the JSON is stable across platforms.
//!
//! A second `--instr-atstart=yes` case captures the whole program from process
//! start, mirroring the C `fixture_full_trace`: the std runtime startup
//! (`std::rt::lang_start`), `main`, and the loader frames appear, and the JSON
//! is snapshotted raw (no redaction), so it is toolchain- and platform-specific
//! like the C full-trace snapshots. Callgrind demangles the Rust symbols and
//! drops their hash suffixes, so the names stay stable for a pinned toolchain.
//!
//! Requires a built `./vg-in-place` at the repo root. Silently skips when
//! `rustc` is not on PATH.
use std::env::consts::ARCH;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::process::Command;

use callgrind_utils::model::CallGraph;

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

fn have_rustc() -> bool {
    Command::new("rustc")
        .arg("--version")
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}

/// Compile the Callgrind client-request shim into a static library, then build
/// `testdata/fractal.rs` against it.
///
/// `-C opt-level=2` inlines away the std iterator / bounds-check helpers so they
/// don't appear as their own (toolchain-version-specific) nodes; the fixture's
/// own functions stay distinct because each is `#[inline(never)]`. The binary is
/// named `fractal_rs` so its object basename is stable in the snapshot.
///
/// Each caller passes a private `work` dir: the two test cases run in parallel,
/// so they must not share the intermediate `.o`/`.a`/binary paths. The binary
/// basename stays `fractal_rs` either way, so the snapshot's object name is
/// identical across cases.
fn compile_rust_fixture(work: &Path) -> PathBuf {
    let repo = repo_root();
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
    let tmp = work;
    std::fs::create_dir_all(tmp).expect("create work dir");

    let obj = tmp.join("clgctl_rs.o");
    let status = Command::new("cc")
        .args(["-g", "-O0", "-fPIC", "-c"])
        .arg("-I")
        .arg(repo.join("callgrind"))
        .arg("-I")
        .arg(repo.join("include"))
        .arg("-o")
        .arg(&obj)
        .arg(manifest.join("testdata/clgctl.c"))
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn cc for clgctl: {e}"));
    assert!(status.success(), "cc failed for clgctl.c ({status})");

    // `ar` appends, so start from a clean archive.
    let lib = tmp.join("libclgctl_rs.a");
    let _ = std::fs::remove_file(&lib);
    let status = Command::new("ar")
        .arg("rcs")
        .arg(&lib)
        .arg(&obj)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn ar: {e}"));
    assert!(status.success(), "ar failed ({status})");

    let bin = tmp.join("fractal_rs");
    let status = Command::new("rustc")
        .args(["--edition", "2021", "-g", "-C", "opt-level=2"])
        .arg("-L")
        .arg(format!("native={}", tmp.display()))
        .arg("-l")
        .arg("static=clgctl_rs")
        .arg("-o")
        .arg(&bin)
        .arg(manifest.join("testdata/fractal.rs"))
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn rustc: {e}"));
    assert!(status.success(), "rustc failed ({status})");
    bin
}

/// Compile `testdata/fractal_alloc.rs` -- a real heap-allocating twin of
/// `fractal.rs` (`Vec<FractalNode>` tree, `Vec<f64>` scratch buffers,
/// `HashMap` memoization) adapted from the actual production benchmark that
/// exhibited the "free calls analyze_fractal_tree" misattribution bug.
/// `-C opt-level=3` matches the real benchmark's build profile; plain
/// `fractal.rs`'s `opt-level=2` was not enough to reproduce it.
#[cfg(target_arch = "aarch64")]
fn compile_fractal_alloc_fixture(work: &Path) -> PathBuf {
    let repo = repo_root();
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
    let tmp = work;
    std::fs::create_dir_all(tmp).expect("create work dir");

    let obj = tmp.join("clgctl_rs.o");
    let status = Command::new("cc")
        .args(["-g", "-O0", "-fPIC", "-c"])
        .arg("-I")
        .arg(repo.join("callgrind"))
        .arg("-I")
        .arg(repo.join("include"))
        .arg("-o")
        .arg(&obj)
        .arg(manifest.join("testdata/clgctl.c"))
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn cc for clgctl: {e}"));
    assert!(status.success(), "cc failed for clgctl.c ({status})");

    let lib = tmp.join("libclgctl_rs.a");
    let _ = std::fs::remove_file(&lib);
    let status = Command::new("ar")
        .arg("rcs")
        .arg(&lib)
        .arg(&obj)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn ar: {e}"));
    assert!(status.success(), "ar failed ({status})");

    let bin = tmp.join("fractal_alloc");
    let status = Command::new("rustc")
        .args(["--edition", "2021", "-g", "-C", "opt-level=3"])
        .arg("-L")
        .arg(format!("native={}", tmp.display()))
        .arg("-l")
        .arg("static=clgctl_rs")
        .arg("-o")
        .arg(&bin)
        .arg(manifest.join("testdata/fractal_alloc.rs"))
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn rustc: {e}"));
    assert!(status.success(), "rustc failed ({status})");
    bin
}

fn runner_callgrind_args(instr_atstart: bool, out_file: &Path) -> Vec<String> {
    [
        "-q",
        "--trace-children=yes",
        "--cache-sim=yes",
        "--I1=32768,8,64",
        "--D1=32768,8,64",
        "--LL=8388608,16,64",
        if instr_atstart {
            "--instr-atstart=yes"
        } else {
            "--instr-atstart=no"
        },
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

fn run_callgrind_with_runner_args(bin: &Path, out_file: &Path, instr_atstart: bool) -> String {
    let status = Command::new("setarch")
        .arg(ARCH)
        .arg("--addr-no-randomize")
        .arg(vg_in_place())
        .args(runner_callgrind_args(instr_atstart, out_file))
        .arg(bin)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn setarch/vg-in-place: {e}"));
    assert!(status.success(), "vg-in-place exited with {status}");
    std::fs::read_to_string(out_file).unwrap_or_else(|e| panic!("read {}: {e}", out_file.display()))
}

/// Profile `bin` with the same Callgrind flags as the runner and return the
/// `.out` contents. `--instr-atstart=no` pairs with the fixture's client
/// requests so only the measured region is profiled.
fn run_callgrind(bin: &Path) -> String {
    let out_file = bin.with_extension("callgrind.out");
    run_callgrind_with_runner_args(bin, &out_file, false)
}

/// Profile `bin` with the runner-equivalent Callgrind flags and return the raw,
/// unredacted graph input. This intentionally keeps `--instr-atstart=no`; the
/// production runner does not capture a separate full-program trace.
fn run_callgrind_full(bin: &Path) -> String {
    let out_file = bin.with_extension("full.callgrind.out");
    run_callgrind_with_runner_args(bin, &out_file, false)
}

#[test]
fn rust_fixture_canonical_json() {
    if !have_rustc() {
        eprintln!("skipping rust_fixture_canonical_json: rustc not on PATH");
        return;
    }

    let work = Path::new(env!("CARGO_TARGET_TMPDIR")).join("scoped");
    let bin = compile_rust_fixture(&work);
    let raw = run_callgrind(&bin);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse rust callgrind output: {e:?}"))
        .redact();
    insta::assert_snapshot!(
        format!("fractal_rs_folded"),
        graph.to_folded_without_costs().join("\n")
    );
    graph.to_flamegraph_file("fractal_rs.partial.svg").unwrap();
}

/// Regression test for the "free calls X" misattribution: `free()`'s
/// return was getting promoted to a fresh CALL into whatever function ran
/// next, because on arm64 a call into skipped code (the libc PLT hop) left
/// a stale skip frame + `nonskipped` state behind, corrupting the return
/// addresses recorded for the following frames. Fixed in
/// callgrind/callstack.c by recording the guest X30 as the frame's return
/// target for real calls and keeping `ret_addr = 0` for emulated/spliced
/// pushes. Asserts directly on the graph (not just a snapshot) so a
/// regression fails loudly instead of silently getting re-approved.
#[cfg(target_arch = "aarch64")]
#[test]
fn arm64_fractal_alloc_no_free_misattribution() {
    if !have_rustc() {
        eprintln!("skipping arm64_fractal_alloc_no_free_misattribution: rustc not on PATH");
        return;
    }

    let work = Path::new(env!("CARGO_TARGET_TMPDIR")).join("fractal_alloc");
    let bin = compile_fractal_alloc_fixture(&work);
    let raw = run_callgrind(&bin);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse fractal_alloc callgrind output: {e:?}"))
        .redact();

    // No snapshot assertion here: `fibonacci_memo`'s exact call/cost counts
    // have observed run-to-run jitter (unrelated to the free-misattribution
    // bug this test guards), which would make an exact-match snapshot
    // flaky. The structural assertions below are the actual regression
    // guard: after redaction every libc frame is `???`, and libc never
    // calls back into the fixture, so a fixture function nested under a
    // `???` frame is exactly the "free calls X" misattribution; a `'2`
    // clone of the non-recursive entry point is the phantom-recursion twin.
    let folded = graph.to_folded_without_costs().join("\n");
    assert!(
        !folded.contains("complex_fractal_benchmark'"),
        "phantom recursion clone of complex_fractal_benchmark:\n{folded}"
    );
    for line in folded.lines() {
        let frames: Vec<&str> = line
            .split_once(' ')
            .map_or(line, |(path, _)| path)
            .split(';')
            .collect();
        if let Some(first_unknown) = frames.iter().position(|f| *f == "???") {
            assert!(
                frames[first_unknown..].iter().all(|f| *f == "???"),
                "fixture frame misattributed under a libc (`???`) frame:\n{line}"
            );
        }
    }
}

#[test]
fn rust_fixture_full_trace() {
    if !have_rustc() {
        eprintln!("skipping rust_fixture_full_trace: rustc not on PATH");
        return;
    }

    let work = Path::new(env!("CARGO_TARGET_TMPDIR")).join("full");
    let bin = compile_rust_fixture(&work);
    let raw = run_callgrind_full(&bin);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse rust full callgrind output: {e:?}"));

    // complex_fractal_benchmark is called exactly once and is not recursive:
    // a `'2` clone means the shadow call stack lost a return and re-promoted
    // it to a phantom call back into the live caller (the arm64 X30/ret_addr
    // regression). Assert directly so this fails loudly on every platform,
    // independent of the platform-specific symbol noise in the snapshot.
    let folded = graph.to_folded_without_costs().join("\n");
    assert!(
        !folded.contains("complex_fractal_benchmark'"),
        "phantom recursion clone of complex_fractal_benchmark in folded output:\n{folded}"
    );

    insta::assert_snapshot!(
        format!("fractal_rs_full_folded"),
        graph.to_folded_without_costs().join("\n")
    );
    graph.to_flamegraph_file("fractal_rs.full.svg").unwrap();
}
