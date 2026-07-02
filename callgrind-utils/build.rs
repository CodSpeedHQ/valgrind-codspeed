//! Ensures the in-repo Callgrind (`../vg-in-place`) is built before the tests
//! that shell out to it run.
//!
//! The build is incremental: `make` is timestamp-driven, so this is a few
//! seconds when the tree is already current and only does real work when the
//! Callgrind sources change. Build order matters: VEX -> coregrind -> callgrind.

use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let repo = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("crate has a parent directory")
        .to_path_buf();

    track_sources(&repo);

    // Callgrind names its tool binary after the target: callgrind-<arch>-linux.
    let arch = match env::consts::ARCH {
        "x86_64" => "amd64",
        "aarch64" => "arm64",
        other => panic!("unsupported arch for the Callgrind build: {other}"),
    };

    configure_if_needed(&repo);
    build(&repo);
    assert_artifacts(&repo, arch);
}

/// Rebuild when a hand-written Callgrind source changes. Only the top-level
/// `callgrind/*.c` / `*.h` are tracked: the `tests/` subdir accumulates
/// `callgrind.out.*` / `vgcore.*` on every run, which would otherwise
/// re-trigger this build on each test invocation.
fn track_sources(repo: &Path) {
    println!("cargo:rerun-if-changed=build.rs");
    println!(
        "cargo:rerun-if-changed={}",
        repo.join("configure").display()
    );

    let cg = repo.join("callgrind");
    let entries = std::fs::read_dir(&cg).unwrap_or_else(|e| panic!("read {}: {e}", cg.display()));
    for path in entries.flatten().map(|e| e.path()) {
        if matches!(path.extension().and_then(|e| e.to_str()), Some("c" | "h")) {
            println!("cargo:rerun-if-changed={}", path.display());
        }
    }
}

/// `configure` is checked in, so this only runs on a pristine tree. Callgrind
/// cycle estimation needs Capstone; `nix develop` exports `CAPSTONE_DIR`, which
/// `configure` picks up. Fail loudly if it is missing rather than emitting a
/// cryptic configure error.
fn configure_if_needed(repo: &Path) {
    if repo.join("Makefile").is_file() {
        return;
    }

    assert!(
        env::var_os("CAPSTONE_DIR").is_some(),
        "valgrind-codspeed is not configured and CAPSTONE_DIR is unset.\n\
         Build from inside `nix develop` (which exports CAPSTONE_DIR), or configure\n\
         manually: ./configure --enable-only64bit --with-capstone=PATH"
    );

    run(Command::new("./configure")
        .arg("--enable-only64bit")
        .current_dir(repo));
}

fn build(repo: &Path) {
    let jobs = format!(
        "-j{}",
        std::thread::available_parallelism()
            .map(|n| n.get())
            .unwrap_or(1)
    );

    run(Command::new("make")
        .arg("include/vgversion.h")
        .current_dir(repo));
    for dir in ["VEX", "coregrind", "callgrind"] {
        run(Command::new("make")
            .arg(&jobs)
            .arg("-C")
            .arg(dir)
            .current_dir(repo));
    }
}

/// The three artifacts `vg-in-place` execs: the launcher, the tool, and the
/// `.in_place` symlink the launcher resolves via `VALGRIND_LIB`.
fn assert_artifacts(repo: &Path, arch: &str) {
    let tool = format!("callgrind-{arch}-linux");
    for path in [
        repo.join("coregrind/valgrind"),
        repo.join("callgrind").join(&tool),
        repo.join(".in_place").join(&tool),
    ] {
        assert!(
            path.exists(),
            "expected build artifact missing after make: {}",
            path.display()
        );
    }
}

fn run(cmd: &mut Command) {
    let shown = format!("{cmd:?}");
    let status = cmd
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn {shown}: {e}"));
    assert!(status.success(), "command failed ({status}): {shown}");
}
