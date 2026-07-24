#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = []
# ///
"""Generate a `codspeed.yml` that benchmarks Valgrind via `codspeed exec`.

Usage:
  ./generate_config.py --valgrind /usr/local/bin/valgrind --output codspeed.yml

Then run the benchmarks with:
  codspeed run --config codspeed.yml --mode walltime
"""

import argparse
import json
import subprocess
import sys

# Commands to profile.
COMMANDS = [
    "testdata/take_strings-aarch64 varbinview_non_null",
    "echo Hello, World!",
    "python3 testdata/test.py",
    "stress-ng --cpu 1 --cpu-ops 10",
    "stress-ng --cpu 4 --cpu-ops 10",
    # Repeated localtime()/__tz_convert() calls: the guest CAS loop
    # (outline-atomics LDAXR/STXR helper) that triggered the ARM64
    # fallback-LL/SC livelock fix. Built by the "Build bench fixtures" CI
    # step from testdata/llsc_tzconvert_bench.c (not checked in as a binary).
    "testdata/llsc_tzconvert_bench 5000",
]

# Callgrind configurations: (extra args, config name, requires_codspeed). The
# config name is the last segment of the benchmark id, e.g.
# `test_valgrind[<version>, <cmd>, no-inline]`. `requires_codspeed` marks configs
# that rely on CodSpeed-only options (e.g. `--cycle-estimation`); they are skipped
# for upstream Valgrind builds, which would otherwise abort with "Unknown option".
CONFIGS = [
    (["--read-inline-info=no"], "no-inline", False),
    (["--read-inline-info=yes"], "inline", False),
    (
        [
            "--trace-children=yes",
            "--cache-sim=yes",
            "--I1=32768,8,64",
            "--D1=32768,8,64",
            "--LL=8388608,16,64",
            "--collect-systime=nsec",
            "--compress-strings=no",
            "--combine-dumps=yes",
            "--dump-line=no",
            "--read-inline-info=yes",
        ],
        "full-with-inline",
        False,
    ),
    (
        [
            "--trace-children=yes",
            "--cache-sim=yes",
            "--I1=32768,8,64",
            "--D1=32768,8,64",
            "--LL=8388608,16,64",
            "--collect-systime=nsec",
            "--compress-strings=no",
            "--combine-dumps=yes",
            "--dump-line=no",
        ],
        "full-no-inline",
        False,
    ),
    (
        [
            "--trace-children=yes",
            "--cache-sim=yes",
            "--I1=32768,8,64",
            "--D1=32768,8,64",
            "--LL=8388608,16,64",
            "--collect-systime=nsec",
            "--compress-strings=no",
            "--combine-dumps=yes",
            "--dump-line=no",
            "--read-inline-info=yes",
            "--cycle-estimation=yes"
        ],
        "full-with-inline-with-cycle-estimation",
        True,
    ),
    (["--cycle-estimation=yes"], "cycle-estimation", True),
]

# Label produced by `valgrind_version` for CodSpeed's custom build.
CODSPEED_VERSION = "valgrind.codspeed"

# Core options added only to upstream builds.
#
# Upstream lacks our Cortex-A72 fallback-LL/SC autodetection, so a guest CAS loop
# livelocks under callgrind on the macro runner: the STXR never sees a live
# exclusive monitor and the guest retries forever (3.26.0 hung 5/5 runs, 3.25.1
# 1/5). This sim-hint, which upstream has shipped since 2017 for ThunderX, forces
# the same CAS-based code path our build picks automatically. It also keeps the
# measurements comparable, since retry iterations are counted as real guest
# instructions.
UPSTREAM_ONLY_ARGS = ["--sim-hints=fallback-llsc"]

# Default walltime sampling settings applied to every benchmark.
#
# Valgrind runs are slow (a single execution can take seconds), so with the
# runner's default `max-time` of 3s the slowest benchmarks only complete a
# single round. A one-sample estimate is dominated by run-to-run noise, which
# is what makes these benchmarks unstable.
#
# We therefore ask the harness for a minimum number of measured rounds so
# CodSpeed always has several samples to pick the representative time from,
# plus one warmup round to discard cold-start effects (process spawn, page
# faults, disk cache).
#
# The runner rejects `min-rounds` combined with `max-time` at config
# validation, so no `max-time` is set. That also removes the 3s default:
# per the harness docs, `max_time` defaults to unlimited when `min_rounds`
# (or `min_time`/`max_rounds`) is set, so every benchmark completes its
# rounds no matter how slow a single execution is.
#
# These are exposed as CLI flags so the CI workflow (and local runs) can tune
# the stability/duration trade-off without editing this script.
DEFAULT_WARMUP_TIME = "1s"
DEFAULT_MIN_ROUNDS = 5


def valgrind_version(valgrind_path: str) -> str:
    """Return the normalized version label used in benchmark ids.

    CodSpeed builds are collapsed to a single `valgrind.codspeed` label so they
    can be compared against each other across iterations; upstream builds keep
    their reported version string (e.g. `valgrind-3.26.0`).
    """
    result = subprocess.run(
        [valgrind_path, "--version"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Valgrind not found or failed at: {valgrind_path}")

    version = result.stdout.strip()
    if "codspeed" in version:
        return CODSPEED_VERSION
    return version


def build_config(
    valgrind_paths: list,
    warmup_time: str = DEFAULT_WARMUP_TIME,
    min_rounds: int = DEFAULT_MIN_ROUNDS,
) -> dict:
    """Build the codspeed.yml document for all valgrind builds and commands."""
    benchmarks = []
    for valgrind_path in valgrind_paths:
        version = valgrind_version(valgrind_path)
        is_codspeed = version == CODSPEED_VERSION
        for cmd in COMMANDS:
            for args, config_name, requires_codspeed in CONFIGS:
                should_skip = requires_codspeed and not is_codspeed
                if should_skip:
                    continue
                name = f"test_valgrind[{version}, {cmd}, {config_name}]"
                extra_args = [] if is_codspeed else UPSTREAM_ONLY_ARGS
                exec_cmd = " ".join(
                    [
                        valgrind_path,
                        "--tool=callgrind",
                        "--log-file=/dev/null",
                        *extra_args,
                        *args,
                        cmd,
                    ]
                )
                benchmarks.append({"name": name, "exec": exec_cmd})

    # Root-level walltime options apply to every benchmark so all runs share the
    # same sampling policy. `min-rounds` guarantees several samples for a stable
    # estimate; `warmup-time` discards cold-start effects; `max-time` caps the
    # total per-benchmark wall time so the workflow stays bounded.
    #
    # The runner flattens WalltimeOptions into `options` (serde(flatten)), so
    # the keys must sit directly under `options`; a nested `walltime:` block is
    # silently ignored.
    return {
        "options": {
            "warmup-time": warmup_time,
            "min-rounds": min_rounds,
        },
        "benchmarks": benchmarks,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Generate a codspeed.yml that benchmarks Valgrind via codspeed exec",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--valgrind",
        dest="valgrinds",
        action="append",
        required=True,
        metavar="PATH",
        help="Path to a valgrind executable (repeat for each build to benchmark). "
        "The version label is derived from `<path> --version`.",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="codspeed.yml",
        help="Path to write the generated config (default: codspeed.yml)",
    )
    parser.add_argument(
        "--warmup-time",
        type=str,
        default=DEFAULT_WARMUP_TIME,
        help="Walltime warmup duration applied to every benchmark, discarded "
        f"before measurement (default: {DEFAULT_WARMUP_TIME}). Set to '0s' to disable.",
    )
    parser.add_argument(
        "--min-rounds",
        type=int,
        default=DEFAULT_MIN_ROUNDS,
        help="Minimum number of measured rounds per benchmark; more rounds give "
        f"a more stable estimate (default: {DEFAULT_MIN_ROUNDS}).",
    )
    args = parser.parse_args()

    config = build_config(
        args.valgrinds,
        warmup_time=args.warmup_time,
        min_rounds=args.min_rounds,
    )

    with open(args.output, "w") as f:
        json.dump(config, f, indent=2)
        f.write("\n")

    print(
        f"Wrote {args.output} with {len(config['benchmarks'])} benchmarks "
        f"({len(args.valgrinds)} valgrind builds x {len(COMMANDS)} commands x {len(CONFIGS)} configs); "
        f"walltime options: warmup-time={args.warmup_time}, min-rounds={args.min_rounds}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
