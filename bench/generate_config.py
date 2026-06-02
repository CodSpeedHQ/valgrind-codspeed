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
]

# Callgrind configurations: (extra args, config name). The config name is the
# last segment of the benchmark id, e.g. `test_valgrind[<version>, <cmd>, no-inline]`.
CONFIGS = [
    (["--read-inline-info=no"], "no-inline"),
    (["--read-inline-info=yes"], "inline"),
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
    ),
]


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
        return "valgrind.codspeed"
    return version


def build_config(valgrind_paths: list) -> dict:
    """Build the codspeed.yml document for all valgrind builds and commands."""
    benchmarks = []
    for valgrind_path in valgrind_paths:
        version = valgrind_version(valgrind_path)
        for cmd in COMMANDS:
            for args, config_name in CONFIGS:
                name = f"test_valgrind[{version}, {cmd}, {config_name}]"
                exec_cmd = " ".join(
                    [valgrind_path, "--tool=callgrind", "--log-file=/dev/null", *args, cmd]
                )
                benchmarks.append({"name": name, "exec": exec_cmd})

    return {
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
    args = parser.parse_args()

    config = build_config(args.valgrinds)

    with open(args.output, "w") as f:
        json.dump(config, f, indent=2)
        f.write("\n")

    print(
        f"Wrote {args.output} with {len(config['benchmarks'])} benchmarks "
        f"({len(args.valgrinds)} valgrind builds x {len(COMMANDS)} commands x {len(CONFIGS)} configs)",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
