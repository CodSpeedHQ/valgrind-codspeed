//! Golden snapshot of the Python fractal fixture's call graph, with real Python
//! frames recovered via CPython's `-X perf` trampolines.
//!
//! The Python twin of `tests/rust_callgraph.rs`: profile `testdata/fractal.py`
//! (a self-contained copy of the CodSpeed e2e Python benchmark) live under the
//! in-repo Callgrind with `--instr-atstart=no`, parse, symbolize, and snapshot
//! the redacted folded stacks.
//!
//! Callgrind is a native profiler, so on its own it only sees the CPython
//! interpreter's C frames, not the Python functions. To surface real Python
//! frames the fixture runs under `python3 -X perf`, whose per-code-object
//! trampolines Callgrind records as anonymous `0x...` addresses and CPython maps
//! to `py::<qualname>:<file>` in `/tmp/perf-<pid>.map`. `symbolize_perf_map`
//! resolves those addresses back to names. Because `setarch` execs into
//! Valgrind, the spawned pid equals CPython's `getpid()`, so the map is at a
//! deterministic path.
//!
//! The fixture obj-skips libpython (like pytest-codspeed's instrument-hooks), so
//! the interpreter's own C frames fold into the `py::` trampoline frames and the
//! graph is a clean Python call tree (`py::run_benchmark` -> ... ->
//! `py::complex_fractal_benchmark` -> ...), with only small libc/libm residuals.
//! The client requests fire several frames deep, so the seeder reconstructs the
//! native chain at the OFF->ON transition.
//!
//! Requires a built `./vg-in-place` at the repo root and `cc`. Silently skips
//! when `python3` is not on PATH (mirrors the `.vgtest` `prereq` guards).
use std::env::consts::ARCH;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::process::Command;

use callgrind_utils::model::CallGraph;
use callgrind_utils::perf_map::PerfMap;

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
fn compile_clgctl(work: &Path) -> PathBuf {
    let repo = repo_root();
    let src = Path::new(env!("CARGO_MANIFEST_DIR")).join("testdata/clgctl.c");
    std::fs::create_dir_all(work).expect("create work dir");
    let lib = work.join("libclgctl.so");

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

/// Profile `testdata/fractal.py` under `python3 -X perf` with the runner's
/// Callgrind flags. Returns the `.out` contents and the parsed
/// `/tmp/perf-<pid>.map`. `setarch` execs into Valgrind, so the spawned pid is
/// CPython's `getpid()` and thus the perf-map filename.
fn run_python(clgctl: &Path, out_file: &Path) -> (String, PerfMap) {
    let script = Path::new(env!("CARGO_MANIFEST_DIR")).join("testdata/fractal.py");
    let log_file = out_file.with_extension("valgrind.log");

    let mut child = Command::new("setarch")
        .arg(ARCH)
        .arg("--addr-no-randomize")
        .arg(vg_in_place())
        .args(runner_callgrind_args(out_file))
        .arg(format!("--log-file={}", log_file.display()))
        .arg("python3")
        .arg("-X")
        .arg("perf")
        .arg(&script)
        .arg(clgctl)
        .spawn()
        .unwrap_or_else(|e| panic!("failed to spawn setarch/vg-in-place: {e}"));
    let pid = child.id();
    let status = child.wait().expect("wait for vg-in-place");
    assert!(status.success(), "vg-in-place exited with {status}");

    let raw = std::fs::read_to_string(out_file)
        .unwrap_or_else(|e| panic!("read {}: {e}", out_file.display()));
    let perf_map_path = PathBuf::from(format!("/tmp/perf-{pid}.map"));
    let perf_map = PerfMap::from_file(&perf_map_path)
        .unwrap_or_else(|e| panic!("read {}: {e}", perf_map_path.display()));
    (raw, perf_map)
}

#[test]
fn python_fractal_canonical_json() {
    if !have_python3() {
        eprintln!("skipping python_fractal_canonical_json: python3 not on PATH");
        return;
    }

    let work = Path::new(env!("CARGO_TARGET_TMPDIR")).join("scoped");
    let clgctl = compile_clgctl(&work);
    let out_file = work.join("fractal_py.callgrind.out");
    let (raw, perf_map) = run_python(&clgctl, &out_file);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse python callgrind output: {e:?}"))
        .symbolize_perf_map(&perf_map)
        .redact();
    insta::assert_snapshot!(
        "fractal_py_folded",
        graph.to_folded_without_costs().join("\n")
    );
    graph.to_flamegraph_file("fractal_py.partial.svg").unwrap();
}

#[test]
fn python_fractal_full_trace() {
    if !have_python3() {
        eprintln!("skipping python_fractal_full_trace: python3 not on PATH");
        return;
    }

    let work = Path::new(env!("CARGO_TARGET_TMPDIR")).join("full");
    let clgctl = compile_clgctl(&work);
    let out_file = work.join("fractal_py.full.callgrind.out");
    let (raw, perf_map) = run_python(&clgctl, &out_file);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse python full callgrind output: {e:?}"))
        .symbolize_perf_map(&perf_map);

    insta::assert_snapshot!(
        "fractal_py_full_folded",
        graph.to_folded_without_costs().join("\n")
    );
    graph.to_flamegraph_file("fractal_py.full.svg").unwrap();
}
