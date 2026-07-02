//! Minimal cross-arch reproduction of the arm64 OFF->ON seeding-underflow
//! cascade (analysis: `.agents/docs/arm64-python-seeding-underflow-analysis.md`).
//!
//! `testdata/objskip_seed_underflow.c` starts instrumentation two obj-skipped
//! frames below an asm trampoline that maintains the FP chain but carries no
//! CFI — the exact shape of CPython's `-X perf` JIT trampolines in the
//! `python_fractal_*` tests. Correct behavior on every arch: the `skip_*`
//! frames fold away and `workload` parents under `trampoline_call`.
//!
//! On aarch64 the CFI-only unwinder seeds a depth-1 context stack, bbcc.c's
//! underflow heuristic misreads the fn-stack base sentinel as a signal
//! marker, and `handleUnderflow` mints named nodes for `skip=1` functions —
//! these assertions fail there until that is fixed. On x86_64 the FP-fallback
//! unwinder seeds main/libc below the trampoline, the depth never reaches 1,
//! and the assertions pass.
//!
//! Structural assertions only (no insta golden): the contract is
//! platform-independent, and the folded text stays free of libc/arch noise.
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

/// Compile the skipped companion `.so` and the main binary (which links it
/// directly, with an rpath back to the work dir).
fn compile(work: &Path) -> (PathBuf, PathBuf) {
    let repo = repo_root();
    let testdata = Path::new(env!("CARGO_MANIFEST_DIR")).join("testdata");
    std::fs::create_dir_all(work).expect("create work dir");

    let lib = work.join("libobjskip_seed_underflow.so");
    let status = Command::new("cc")
        // -z now: resolve PLTs eagerly so lazy-binding _dl_runtime_resolve
        // chains don't show up as first-call noise in the folded graph.
        .args(["-g", "-O0", "-shared", "-fPIC", "-Wl,-z,now", "-o"])
        .arg(&lib)
        .arg(testdata.join("objskip_seed_underflow_lib.c"))
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn cc for skip lib: {e}"));
    assert!(status.success(), "cc failed for skip lib ({status})");

    let bin = work.join("objskip_seed_underflow");
    let status = Command::new("cc")
        // -rdynamic: the skipped .so resolves workload/clg_*_marker back in
        // the executable at load time.
        .args(["-g", "-O0", "-rdynamic", "-Wl,-z,now"])
        .arg("-I")
        .arg(repo.join("callgrind"))
        .arg("-I")
        .arg(repo.join("include"))
        .arg("-o")
        .arg(&bin)
        .arg(testdata.join("objskip_seed_underflow.c"))
        .arg(&lib)
        .arg(format!("-Wl,-rpath,{}", work.display()))
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn cc for fixture: {e}"));
    assert!(status.success(), "cc failed for fixture ({status})");
    (bin, lib)
}

/// Profile with the exact runner flag set used by the other fixture tests.
/// The lib path is argv[1]: the fixture realpaths it into
/// `CALLGRIND_ADD_OBJ_SKIP` before instrumentation starts.
fn run_callgrind(bin: &Path, lib: &Path, out_file: &Path) -> String {
    let log_file = out_file.with_extension("valgrind.log");
    let args = [
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
    ];
    let status = Command::new("setarch")
        .arg(ARCH)
        .arg("--addr-no-randomize")
        .arg(vg_in_place())
        .args(args)
        .arg(format!("--callgrind-out-file={}", out_file.display()))
        .arg(format!("--log-file={}", log_file.display()))
        .arg(bin)
        .arg(lib)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn setarch/vg-in-place: {e}"));
    assert!(status.success(), "vg-in-place exited with {status}");
    std::fs::read_to_string(out_file).unwrap_or_else(|e| panic!("read {}: {e}", out_file.display()))
}

#[test]
fn objskip_seed_underflow_folds_skipped_frames() {
    let work = Path::new(env!("CARGO_TARGET_TMPDIR")).join("objskip_seed_underflow");
    let (bin, lib) = compile(&work);
    let out_file = work.join("objskip_seed_underflow.callgrind.out");
    let raw = run_callgrind(&bin, &lib, &out_file);
    let graph = CallGraph::parse(Cursor::new(raw.as_str()))
        .unwrap_or_else(|e| panic!("parse callgrind output: {e:?}"));
    let folded = graph.to_folded_without_costs();
    let dump = folded.join("\n");

    // 1. Obj-skipped frames must never appear as named nodes. On broken
    //    arm64 the underflow cascade mints skip_begin_hop1/_ctypes-style
    //    inverted nodes for skip=1 functions.
    let leaked: Vec<&String> = folded.iter().filter(|l| l.contains("skip_")).collect();
    assert!(
        leaked.is_empty(),
        "obj-skipped frames leaked into the folded graph \
         (OFF->ON seeding-underflow cascade):\n{}\n\nfull folded output:\n{dump}",
        leaked
            .iter()
            .map(|s| s.as_str())
            .collect::<Vec<_>>()
            .join("\n")
    );

    // 2. The measured region must parent under the trampoline (nearest
    //    non-skipped frame), like py::run_measured;py::complex_fractal_benchmark.
    assert!(
        folded
            .iter()
            .any(|l| l.starts_with("trampoline_call;workload")),
        "workload is not parented under trampoline_call:\n{dump}"
    );

    // 3. Roots: only the seeded innermost frame (clg_begin_marker, tiny
    //    post-request residue) and the trampoline may be roots.
    for line in &folded {
        let root = line
            .split(';')
            .next()
            .unwrap()
            .trim_end_matches(" <cost>");
        assert!(
            root == "trampoline_call" || root == "clg_begin_marker",
            "unexpected root {root:?} in folded graph:\n{dump}"
        );
    }
}
