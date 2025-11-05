#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = [
#     "pytest>=8.4.2",
#     "pytest-codspeed>=4.2.0",
# ]
# ///

import argparse
import shlex
import subprocess

import pytest


class ValgrindRunner:
    """Run Valgrind with different configurations."""

    def __init__(
        self,
        cmd: str,
        valgrind_path: str = "valgrind",
    ):
        """Initialize valgrind runner.

        Args:
            cmd: Command to profile (can be a path or arbitrary shell command)
            valgrind_path: Path to valgrind executable
        """
        self.cmd = cmd
        self.valgrind_path = valgrind_path

        # Verify valgrind is available
        result = subprocess.run(
            [self.valgrind_path, "--version"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"Valgrind not found at: {self.valgrind_path}")
        self.valgrind_version = result.stdout.strip()

    def run_valgrind(self, *args: str) -> None:
        """Execute valgrind with given arguments.

        Args:
            *args: Valgrind arguments
        """

        cmd = [
            self.valgrind_path,
            "--tool=callgrind",
            "--log-file=/dev/null",
            *args,
            *shlex.split(self.cmd),
        ]

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"Valgrind execution failed with code {result.returncode}\n"
                f"Stdout:\n{result.stdout}\n"
                f"Stderr:\n{result.stderr}"
            )


@pytest.fixture
def runner(request):
    """Fixture to provide runner instance to tests."""
    return request.config._valgrind_runner


def pytest_generate_tests(metafunc):
    """Parametrize tests with valgrind configurations."""
    if "valgrind_args" in metafunc.fixturenames:
        runner = getattr(metafunc.config, "_valgrind_runner", None)
        if not runner:
            return

        # Define valgrind configurations
        configs = [
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

        # If the valgrind version is from CodSpeed, we don't want to display the exact version
        # to allow comparison against older versions. 
        if ".codspeed" in runner.valgrind_version:
            runner.valgrind_version = "valgrind.codspeed"

        # Create test IDs with format: valgrind-version, command, config-name
        test_ids = [
            f"{runner.valgrind_version}, {runner.cmd}, {config_name}"
            for _, config_name in configs
        ]

        # Parametrize with just the args
        metafunc.parametrize(
            "valgrind_args",
            [args for args, _ in configs],
            ids=test_ids,
        )


@pytest.mark.benchmark
def test_valgrind(runner, valgrind_args):
    if runner:
        runner.run_valgrind(*valgrind_args)


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark Valgrind with pytest-codspeed",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Run with a binary path
  uv run bench.py --cmd /path/to/binary

  # Run with an arbitrary command
  uv run bench.py --cmd 'echo "hello world"'

  # Run with custom valgrind installation
  uv run bench.py --cmd /usr/bin/ls --valgrind-path /usr/local/bin/valgrind
        """,
    )

    parser.add_argument(
        "--cmd",
        type=str,
        required=True,
        help="Command to profile (can be a path to a binary or any arbitrary command)",
    )
    parser.add_argument(
        "--valgrind-path",
        type=str,
        default="valgrind",
        help="Path to valgrind executable (default: valgrind)",
    )
    args = parser.parse_args()

    # Create runner instance
    runner = ValgrindRunner(
        cmd=args.cmd,
        valgrind_path=args.valgrind_path,
    )
    print(f"Valgrind version: {runner.valgrind_version}")
    print(f"Command: {args.cmd}")

    # Plugin to pass runner to tests
    class RunnerPlugin:
        def pytest_configure(self, config):
            config._valgrind_runner = runner

    exit_code = pytest.main(
        [__file__, "-v", "--codspeed", "--codspeed-warmup-time=0", "--codspeed-max-time=5"],
        plugins=[RunnerPlugin()],
    )
    if exit_code != 0 and exit_code != 5:
        print(f"Benchmark execution returned exit code: {exit_code}")


if __name__ == "__main__":
    main()
