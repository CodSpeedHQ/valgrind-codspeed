//! Snapshot of the Python fixture's external Callgrind graph JSON.
//!
//! Callgrind records the CPython interpreter's C frames, not the Python
//! functions: the interpreter loop is obj-skipped at runtime via the `clgctl`
//! shim's `CALLGRIND_ADD_OBJ_SKIP`, so what remains is the ctypes/libffi/libc
//! C-residual around the `clg_start`/`clg_stop` shim.
//!
//! Requires a built `./vg-in-place` at the repo root and `cc`. Silently skips
//! when `python3` is not on PATH (mirrors the `.vgtest` `prereq` guards).
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

fn have_python3() -> bool {
    Command::new("python3")
        .arg("--version")
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}

/// Compile the Callgrind client-request shim the Python fixture loads via
/// `ctypes`, as a shared library against the in-repo `callgrind.h`.
fn compile_clgctl() -> PathBuf {
    let repo = repo_root();
    let src = Path::new(env!("CARGO_MANIFEST_DIR")).join("testdata/clgctl.c");
    let lib = Path::new(env!("CARGO_TARGET_TMPDIR")).join("libclgctl.so");

    let status = Command::new("cc")
        .args(["-g", "-O0", "-shared", "-fPIC"])
        .arg("-I")
        .arg(repo.join("callgrind"))
        .arg("-I")
        .arg(repo.join("include"))
        .arg("-o")
        .arg(&lib)
        .arg(&src)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn cc for clgctl: {e}"));
    assert!(
        status.success(),
        "cc failed for {} ({status})",
        src.display()
    );
    lib
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

/// Profile `testdata/recursion.py` with the same Callgrind flags as the runner
/// and return the `.out` contents.
fn run_python(clgctl: &Path, _instr_atstart: bool) -> String {
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("testdata/recursion.py");
    let out_file = Path::new(env!("CARGO_TARGET_TMPDIR")).join("python.callgrind.out");
    let log_file = out_file.with_extension("valgrind.log");

    let status = Command::new("setarch")
        .arg(ARCH)
        .arg("--addr-no-randomize")
        .arg(vg_in_place())
        .args(runner_callgrind_args(&out_file))
        .arg(format!("--log-file={}", log_file.display()))
        .arg("python3")
        .arg(&script)
        .arg(clgctl)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn setarch/vg-in-place: {e}"));
    assert!(status.success(), "vg-in-place exited with {status}");
    std::fs::read_to_string(&out_file)
        .unwrap_or_else(|e| panic!("read {}: {e}", out_file.display()))
}

/// Render a flamegraph of the fixture profiled with `--instr-atstart=yes`, so
/// the whole-program call stack is captured from process start and the
/// interpreter's `fib` recursion (`_PyEval_EvalFrameDefault` and the
/// PyLong/frame helpers) is visible. Under `--instr-atstart=no` the measured
/// region begins inside already-obj-skipped libpython, so everything folds
/// into `(below main)` and the flamegraph is a single uninformative bar.
/// Rendered from the RAW graph (redaction collapses libc/ld into a non-root
/// `???` node). Writes `python.svg` at the crate root for manual inspection.
#[test]
#[ignore]
fn python_flamegraph() {
    if !have_python3() {
        eprintln!("skipping python_flamegraph: python3 not on PATH");
        return;
    }

    let clgctl = compile_clgctl();
    let raw = run_python(&clgctl, true);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse python callgrind output: {e:?}"));

    let out = Path::new(env!("CARGO_MANIFEST_DIR")).join("python.svg");
    graph.to_flamegraph_file(&out).expect("render flamegraph");
}
