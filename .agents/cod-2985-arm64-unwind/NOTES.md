# COD-2985 — AArch64 callgrind missed-return: raw working notes

Companion to the polished report at `.agents/docs/arm64-callgrind-missed-return.md`.
Fix lives on branch `cod-2985-arm64-unwind-minpops` (commits 523312559 + ff17d038f).
These are the raw investigation notes + reproducers, preserved verbatim.

## TL;DR root cause

`callgrind/callstack.c::CLG_(unwind_call_stack)` decremented `minpops` for **SP-lower**
pops, not only **SP-equal** pops. On AArch64 `bl`/`blr` don't move SP, so a callee's own
entry frame is recorded at SP == the return target and sits beneath the SP-lower frames
of its sub-calls (e.g. libc malloc via PLT). On return, the SP-lower sub-call frames ate
the `minpops` budget and the callee's SP-equal entry frame was left stuck → callee context
stays active (inverted edges like `malloc -> driftsort_main`) + never-decremented fn depth
→ fabricated `'2` recursion clones. x86 immune (entry frames are SP-lower there).

Fix: SP-lower frames always pop and never consume `minpops`; only SP-equal pops are
budget-bounded.

## Key trace evidence (reproC, unfixed) — reproC.trace

`rec_parent -> leaf_alloc -> malloc(PLT) -> ... -> ret`, malloc returns to 0x4000870:

```
+ 17 0x400086c => 0x4900560 ... RA 0x4000870          # malloc entry frame, RA is CORRECT (not 0)
...
Return 04900664 -> 04000870, SP 1fff000020            # malloc returns into leaf
- 17 0x48fef74 => 0x4900720 ... SP 0x1ffeffffe0       # only an internal _int_malloc frame popped
04000870/T  418:malloc ...                            # BUG: leaf code attributed to fn 'malloc'
Call 04000884 -> 04000690  (memset@plt)               # => recorded as malloc -> memset
```

After fix (reproC.fix.trace) the same address is attributed to `leaf` (cxt 454), no
`RET w/o CALL`, malloc frame popped correctly.

Important correction: the malloc entry frame's `ret_addr` is the CORRECT return address
(`0x4000870`), NOT 0. The earlier static-analysis hypothesis (emulated frame gets
ret_addr=0) was WRONG — disproved by the trace. The real defect is the minpops/SP-lower
accounting, found only by tracing the actual reproducer.

## Minimal reproducers (in this dir)

- `repro.c`   — variant A: leaf calls libc malloc, called from recursive parent → inverts.
- `reproB.c`  — control: leaf does pure arithmetic (no libc) → CLEAN. Pins trigger to
                "function reached via emulated/PLT call returning on AArch64".
- `reproC.c`  — tightest repro (warmup + recursive rec_parent + allocating leaf). Used for
                tracing. (Final test in repo: callgrind/tests/recursion_inversion.c.)

Build + run any repro:
```
gcc -O1 -g -fno-omit-frame-pointer -o repro repro.c
./vg-in-place -q --tool=callgrind --cache-sim=yes --compress-strings=no \
  --combine-dumps=yes --dump-line=no --callgrind-out-file=out repro
# inverted edge check:
awk -F= '/^fn=/{c=$2} /^cfn=/{if(c~/leaf/||c~/malloc/)print c" -> "$2}' out | sort -u
```

Focused verbose trace (warm the allocator first, scope verbosity to one fn):
```
./vg-in-place -q --tool=callgrind --cache-sim=no --ct-verbose3=rec --callgrind-out-file=o reproC \
  2>&1 | head -2500 > reproC.trace
```
(`--ct-verbose=N` global is too slow — the first cold malloc explodes; scope with
`--ct-verboseN=<fn>` and warm malloc before the traced region.)

## Real bench A/B (same binary, fix stashed vs applied)

Bench binary: codspeed-integrations-e2e-tests/rust/target/codspeed/analysis/graph-benchmark/bench_fractal
```
CODSPEED_RUNNER_MODE=instrumentation CODSPEED_PROFILE_FOLDER=$OUT ARCH=aarch64 CODSPEED_ENV=runner \
setarch aarch64 --addr-no-randomize ./vg-in-place -q --cache-sim=yes \
  --I1=32768,8,64 --D1=32768,8,64 --LL=8388608,16,64 --instr-atstart=no --collect-systime=nsec \
  --read-inline-info=yes --tool=callgrind --compress-strings=no --combine-dumps=yes --dump-line=no \
  --callgrind-out-file=$OUT/%p.out $BIN rust_fractal_computation
```

| | unfixed | fixed |
|---|---|---|
| inverted allocator→workload edges | 6 (malloc→driftsort_main, malloc'2→driftsort_main'2, free'2→analyze_fractal_tree'2, free'2→recursive_path_score, free→reserve_rehash'2, malloc→reserve_rehash'2) | 0 |
| driftsort_main parent | malloc (wrong) | analyze_fractal_tree (correct) |
| removed clones | — | malloc'2, __rust_alloc'2, __rdl_alloc'2 |
| determinism | — | identical across 2 runs |

Pre-existing residual clones present in BOTH builds (out of scope, issue-#1 class):
strncmp'2, malloc_consolidate'2, _int_malloc'2, _int_free'2, sin'2, sincos'2, etc.

## Regression suite

`perl tests/vg_regtest callgrind`:
- unfixed: 4 post failures (bug497723, find_debuginfo, inline-crossfile, inline-samefile)
- fixed:   2 post failures (bug497723, find_debuginfo) — both pre-existing arch/env:
           bug497723's memcheck test binary isn't built; find_debuginfo expects
           ld-linux-x86-64.so.2. inline-crossfile + inline-samefile FAIL→PASS (fix).
- new test recursion_inversion: FAIL on baseline (leaf_alloc'2, leaf_alloc→rec_parent'2),
  PASS on fix.

## Build/iterate loop

```
make -C callgrind callgrind-arm64-linux      # incremental rebuild of the tool
./vg-in-place ...                            # run local in-place build
# regen build files after editing a tests/Makefile.am:
automake callgrind/tests/Makefile && ./config.status callgrind/tests/Makefile
make -C callgrind/tests check
```
Makefile.in / Makefile are gitignored (generated); only Makefile.am is tracked.
