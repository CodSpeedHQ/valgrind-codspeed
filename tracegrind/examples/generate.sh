#!/usr/bin/env bash
#
# Generate tracegrind example output files.
#
# Run from the valgrind-codspeed repository root:
#   bash tracegrind/examples/generate.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VG="$ROOT/vg-in-place"
ANALYZER="$ROOT/tracegrind/scripts/tracegrind-analyzer"
TESTS="$ROOT/tracegrind/tests"
OUT="$ROOT/tracegrind/examples"

if [ ! -x "$VG" ]; then
    echo "Error: vg-in-place not found at $VG" >&2
    echo "Build valgrind first (./configure && make)" >&2
    exit 1
fi

generate() {
    local name="$1"
    local binary="$2"
    shift 2
    local vgopts=("$@")

    local trace="$OUT/${name}.tgtrace"
    local txt="$OUT/${name}.txt"

    echo "Generating $name ..."
    "$VG" --tool=tracegrind \
        --tracegrind-out-file="$trace" \
        "${vgopts[@]}" \
        "$TESTS/$binary" > /dev/null 2>&1

    "$ANALYZER" "$trace" > "$txt" 2>&1

    echo "  -> $(wc -c < "$trace") bytes, $(wc -l < "$txt") lines"
}

# Remove previous outputs
rm -f "$OUT"/*.tgtrace "$OUT"/*.txt

generate test_basic              test_basic.bin
generate test_marker             test_marker.bin
generate test_toggle_collect     test_toggle_collect.bin
generate test_foo_bar_baz        test_foo_bar_baz.bin        --instr-atstart=no
generate test_inline             test_inline.bin             --instr-atstart=no
generate test_enter_inlined      test_enter_inlined.bin      --instr-atstart=no --read-inline-info=yes
generate test_nested_inlined     test_nested_inlined.bin     --instr-atstart=no --read-inline-info=yes
generate test_recursion          test_recursion.bin          --instr-atstart=no
generate test_tailcall           test_tailcall.bin           --instr-atstart=no
generate test_longjmp            test_longjmp.bin            --instr-atstart=no
generate test_signal             test_signal.bin             --instr-atstart=no
generate test_exception          test_exception.bin          --instr-atstart=no
generate test_thread_create      test_thread_create.bin      --instr-atstart=no
generate test_thread_interleave  test_thread_interleave.bin  --instr-atstart=no
generate test_syscall            test_syscall.bin            --instr-atstart=no --collect-systime=nsec
generate test_instr_toggle       test_instr_toggle.bin       --instr-atstart=no

echo ""
echo "Done. Generated $(ls "$OUT"/*.tgtrace 2>/dev/null | wc -l) trace files."
