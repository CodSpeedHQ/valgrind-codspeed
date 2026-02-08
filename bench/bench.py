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

        # Check which tools are available
        self.available_tools = self._detect_available_tools()

    def _detect_available_tools(self) -> set:
        """Detect which valgrind tools are available."""
        tools = set()
        for tool in ["callgrind", "tracegrind"]:
            result = subprocess.run(
                [self.valgrind_path, f"--tool={tool}", "--help"],
                capture_output=True,
                text=True,
            )
            if result.returncode == 0:
                tools.add(tool)
        return tools

    def run_valgrind(self, tool: str, *args: str) -> None:
        """Execute valgrind with given tool and arguments.

        Args:
            tool: Valgrind tool to use (callgrind, tracegrind)
            *args: Valgrind arguments
        """

        cmd = [
            self.valgrind_path,
            f"--tool={tool}",
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


CACHE_SIM_OPTIONS = [
    "--cache-sim=yes",
    "--I1=32768,8,64",
    "--D1=32768,8,64",
    "--LL=8388608,16,64",
]

def pytest_generate_tests(metafunc):
    """Parametrize tests with valgrind configurations."""
    if "tool_and_args" in metafunc.fixturenames:
        runner = getattr(metafunc.config, "_valgrind_runner", None)
        if not runner:
            return

        # Define configurations for each tool
        # Format: (tool, args, config_name)
        all_configs = [
            # Callgrind configurations
            ("callgrind", ["--read-inline-info=no"], "cg/no-inline"),
            ("callgrind", ["--read-inline-info=yes"], "cg/inline"),
            (
                "callgrind",
                [
                    *CACHE_SIM_OPTIONS,
                    "--trace-children=yes",
                    "--collect-systime=nsec",
                    "--compress-strings=no",
                    "--combine-dumps=yes",
                    "--dump-line=no",
                    "--read-inline-info=yes",
                ],
                "cg/full-inline",
            ),
            (
                "callgrind",
                [
                    *CACHE_SIM_OPTIONS,
                    "--trace-children=yes",
                    "--collect-systime=nsec",
                    "--compress-strings=no",
                    "--combine-dumps=yes",
                    "--dump-line=no",
                    "--read-inline-info=no",
                ],
                "cg/full-no-inline",
            ),
            # Tracegrind configurations (only available in codspeed fork)
            ("tracegrind", ["--read-inline-info=no"], "tg/no-inline"),
            ("tracegrind", ["--read-inline-info=yes"], "tg/inline"),
            (
                "tracegrind",
                [
                    *CACHE_SIM_OPTIONS,
                    "--trace-children=yes",
                    "--collect-systime=nsec",
                    "--read-inline-info=no",
                ],
                "tg/full-no-inline",
            ),
            (
                "tracegrind",
                [
                    *CACHE_SIM_OPTIONS,
                    "--trace-children=yes",
                    "--collect-systime=nsec",
                    "--read-inline-info=yes",
                ],
                "tg/full-inline",
            ),
        ]

        # Filter configs to only include available tools
        configs = [
            (tool, args, name)
            for tool, args, name in all_configs
            if tool in runner.available_tools
        ]

        if not configs:
            return

        # If the valgrind version is from CodSpeed, we don't want to display the exact version
        # to allow comparison against older versions.
        if ".codspeed" in runner.valgrind_version:
            runner.valgrind_version = "codspeed"
        # Clean valgrind version names
        else:
            runner.valgrind_version.removeprefix("valgrind-")

        # Create test IDs with format: valgrind-version, command, config-name
        test_ids = [
            f"{runner.valgrind_version}/{config_name}, {runner.cmd}"
            for _, _, config_name in configs
        ]

        # Parametrize with (tool, args) tuples
        metafunc.parametrize(
            "tool_and_args",
            [(tool, args) for tool, args, _ in configs],
            ids=test_ids,
        )


@pytest.mark.benchmark
def test_valgrind(runner, tool_and_args):
    if runner:
        tool, args = tool_and_args
        runner.run_valgrind(tool, *args)


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark Valgrind tools (callgrind, tracegrind) with pytest-codspeed",
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
    print(f"Available tools: {', '.join(sorted(runner.available_tools))}")
    print(f"Command: {args.cmd}")

    # Plugin to pass runner to tests
    class RunnerPlugin:
        def pytest_configure(self, config):
            config._valgrind_runner = runner

    exit_code = pytest.main(
        [__file__, "-v", "--codspeed", "--codspeed-warmup-time=0", "--codspeed-max-time=30"],
        plugins=[RunnerPlugin()],
    )
    if exit_code != 0 and exit_code != 5:
        print(f"Benchmark execution returned exit code: {exit_code}")


if __name__ == "__main__":
    main()
