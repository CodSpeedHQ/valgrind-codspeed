//! AArch64 TLS-descriptor resolver transparency (`_dl_tlsdesc_return`).
//!
//! `testdata/arm64_tls_access.c` + `testdata/arm64_tls_access_lib.c`: a
//! `__thread` variable defined in a shared library forces the TLSDESC access
//! model, so every access `blr`s into the dynamic linker's resolver, which
//! `ret`s straight back into the middle of the accessing function.
//! `callgrind/fn.c` marks `_dl_tlsdesc_*` as skipped (the same transparent
//! trampoline class as PLT stubs and `_dl_runtime_resolve`), so the resolver
//! must never surface as a named node.
//!
//! The second test is the production shape (CPython under pytest-codspeed,
//! which obj-skips the interpreter binary): a TLS access made *from an
//! obj-skipped object* used to push the resolver frame with `ret_addr == 0`
//! via the skipped->nonskipped splice; the resolver's mid-function return
//! could never match, the RET-w/o-CALL promotion re-entered the skipped
//! object with `nonskipped` pointing at the resolver, and skipped cost plus
//! call edges piled up under `_dl_tlsdesc_return` (observed pulling nearly
//! whole Python flamegraphs under that node, plus inverted
//! `hash_tree -> build_tree` edges in this fixture).
//!
//! Structural assertions only (no insta golden), so glibc/toolchain noise
//! stays out of the contract. aarch64-only: other arches compile this TLS
//! access to `__tls_get_addr` calls and never exercise the TLSDESC path.
#![cfg(target_arch = "aarch64")]
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

/// Compile the TLS-owning `.so` and the main binary (which links it directly,
/// with an rpath back to the work dir). `-O2` matches the other `arm64_*`
/// fixtures and keeps the TLSDESC sequence a real GOT-loaded `blr`.
fn compile(work: &Path) -> (PathBuf, PathBuf) {
    let repo = repo_root();
    let testdata = Path::new(env!("CARGO_MANIFEST_DIR")).join("testdata");
    std::fs::create_dir_all(work).expect("create work dir");

    let lib = work.join("libarm64_tls_access.so");
    let status = Command::new("cc")
        .args(["-g", "-O2", "-shared", "-fPIC", "-o"])
        .arg(&lib)
        .arg(testdata.join("arm64_tls_access_lib.c"))
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn cc for TLS lib: {e}"));
    assert!(status.success(), "cc failed for TLS lib ({status})");

    let bin = work.join("arm64_tls_access");
    let status = Command::new("cc")
        .args(["-g", "-O2"])
        .arg("-I")
        .arg(repo.join("callgrind"))
        .arg("-I")
        .arg(repo.join("include"))
        .arg("-o")
        .arg(&bin)
        .arg(testdata.join("arm64_tls_access.c"))
        .arg(&lib)
        .arg(format!("-Wl,-rpath,{}", work.display()))
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn cc for fixture: {e}"));
    assert!(status.success(), "cc failed for fixture ({status})");
    (bin, lib)
}

/// Profile with the exact runner flag set used by the other fixture tests,
/// plus any `extra_args` (`--obj-skip=...` for the production shape).
fn run_callgrind(bin: &Path, out_file: &Path, extra_args: &[String]) -> String {
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
        .args(extra_args)
        .arg(format!("--callgrind-out-file={}", out_file.display()))
        .arg(format!("--log-file={}", log_file.display()))
        .arg(bin)
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn setarch/vg-in-place: {e}"));
    assert!(status.success(), "vg-in-place exited with {status}");
    std::fs::read_to_string(out_file).unwrap_or_else(|e| panic!("read {}: {e}", out_file.display()))
}

fn folded(raw: &str) -> Vec<String> {
    CallGraph::parse(Cursor::new(raw))
        .unwrap_or_else(|e| panic!("parse callgrind output: {e:?}"))
        .to_folded_without_costs()
}

/// The resolver must never appear as a named node, and no stack may place a
/// fixture function under it (the "post-trampoline work stolen" signature).
fn assert_no_tlsdesc_node(folded: &[String], dump: &str) {
    let leaked: Vec<&String> = folded.iter().filter(|l| l.contains("_dl_tlsdesc_")).collect();
    assert!(
        leaked.is_empty(),
        "TLSDESC resolver leaked into the folded graph:\n{}\n\nfull folded output:\n{dump}",
        leaked
            .iter()
            .map(|s| s.as_str())
            .collect::<Vec<_>>()
            .join("\n")
    );
}

#[test]
fn tlsdesc_resolver_is_transparent() {
    let work = Path::new(env!("CARGO_TARGET_TMPDIR")).join("plain");
    let (bin, _lib) = compile(&work);
    let out_file = work.join("arm64_tls_access.callgrind.out");
    let raw = run_callgrind(&bin, &out_file, &[]);
    let folded = folded(&raw);
    let dump = folded.join("\n");

    assert_no_tlsdesc_node(&folded, &dump);

    // touch_tls itself is not skipped here: it must show up under its real
    // callers, with the resolver's cost folded into it...
    assert!(
        folded.iter().any(|l| l.contains(";touch_tls")),
        "touch_tls missing from the folded graph:\n{dump}"
    );
    // ...and must stay a leaf: a `touch_tls;<fixture fn>` stack means the
    // resolver's unmatched return re-parented the caller's work.
    let stolen: Vec<&String> = folded
        .iter()
        .filter(|l| l.contains("touch_tls;"))
        .collect();
    assert!(
        stolen.is_empty(),
        "work stolen under touch_tls (unmatched TLSDESC return):\n{}\n\nfull folded output:\n{dump}",
        stolen
            .iter()
            .map(|s| s.as_str())
            .collect::<Vec<_>>()
            .join("\n")
    );
}

#[test]
fn tlsdesc_from_objskipped_code_does_not_steal_cost() {
    let work = Path::new(env!("CARGO_TARGET_TMPDIR")).join("objskip");
    let (bin, lib) = compile(&work);
    let out_file = work.join("arm64_tls_access.objskip.callgrind.out");
    let raw = run_callgrind(&bin, &out_file, &[format!("--obj-skip={}", lib.display())]);
    let folded = folded(&raw);
    let dump = folded.join("\n");

    assert_no_tlsdesc_node(&folded, &dump);

    // The TLS-accessing function lives in the skipped object: it must fold
    // away entirely.
    assert!(
        !dump.contains("touch_tls"),
        "obj-skipped touch_tls leaked into the folded graph:\n{dump}"
    );

    // The stuck-resolver cascade manufactured return-direction edges
    // (hash_tree "calling" build_tree). The real call direction is
    // build_tree -> hash_tree only.
    let inverted: Vec<&String> = folded
        .iter()
        .filter(|l| l.contains("hash_tree;build_tree"))
        .collect();
    assert!(
        inverted.is_empty(),
        "inverted hash_tree -> build_tree edges (stuck TLSDESC frame cascade):\n{}\n\nfull folded output:\n{dump}",
        inverted
            .iter()
            .map(|s| s.as_str())
            .collect::<Vec<_>>()
            .join("\n")
    );

    // Instrumentation starts inside run_measured: it is the only legal root.
    for line in &folded {
        let root = line
            .split(';')
            .next()
            .unwrap()
            .trim_end_matches(" <cost>");
        assert!(
            root == "run_measured",
            "unexpected root {root:?} in folded graph:\n{dump}"
        );
    }
}
