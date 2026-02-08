# Tracegrind example output files

This directory contains pre-generated tracegrind output files for use as
reference material when implementing a trace parser.

Each test produces two files:

- **`<name>.tgtrace`** — binary trace file (msgpack + lz4 compressed)
- **`<name>.txt`** — full human-readable dump from `tracegrind-analyzer`

## Files

| Name | Description | Extra options |
|------|-------------|---------------|
| `test_basic` | Full program trace (loader + libc + main) | — |
| `test_marker` | `VALGRIND_TRACEGRIND_MARKER` client request | — |
| `test_toggle_collect` | `--toggle-collect` style collection | — |
| `test_foo_bar_baz` | Simple call chain: `foo -> bar -> baz` | `--instr-atstart=no` |
| `test_inline` | Inlined function calls | `--instr-atstart=no` |
| `test_enter_inlined` | `ENTER_INLINED_FN` / `EXIT_INLINED_FN` events | `--instr-atstart=no --read-inline-info=yes` |
| `test_nested_inlined` | Nested inlined function calls | `--instr-atstart=no --read-inline-info=yes` |
| `test_recursion` | Recursive function calls | `--instr-atstart=no` |
| `test_tailcall` | Tail-call optimized functions | `--instr-atstart=no` |
| `test_longjmp` | `setjmp` / `longjmp` unwinding | `--instr-atstart=no` |
| `test_signal` | Signal handler invocation | `--instr-atstart=no` |
| `test_exception` | C++ exception throw/catch | `--instr-atstart=no` |
| `test_thread_create` | `THREAD_CREATE` events | `--instr-atstart=no` |
| `test_thread_interleave` | Multi-thread interleaved callstacks | `--instr-atstart=no` |
| `test_syscall` | System call timing (`sysCount`, `sysTime` counters) | `--instr-atstart=no --collect-systime=nsec` |
| `test_instr_toggle` | Instrumentation toggle on/off mid-run | `--instr-atstart=no` |

## Regenerating

From the repository root (after building valgrind):

```bash
bash tracegrind/examples/generate.sh
```

## Format

The `.tgtrace` files use the tracegrind msgpack format (lz4-compressed msgpack).
See `tracegrind/docs/tracegrind-msgpack-format.md` for the format specification.

Use `tracegrind/scripts/tracegrind-analyzer` to inspect any trace file:

```bash
# Full dump
./tracegrind/scripts/tracegrind-analyzer tracegrind/examples/test_foo_bar_baz.tgtrace

# Schema only
./tracegrind/scripts/tracegrind-analyzer tracegrind/examples/test_foo_bar_baz.tgtrace --schema

# Statistics
./tracegrind/scripts/tracegrind-analyzer tracegrind/examples/test_foo_bar_baz.tgtrace --stats

# Filter by event type
./tracegrind/scripts/tracegrind-analyzer tracegrind/examples/test_foo_bar_baz.tgtrace --event ENTER_FN
```
