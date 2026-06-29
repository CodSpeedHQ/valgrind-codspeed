# AArch64 Callgrind: inverted call edges & fabricated recursion

**Status:** fixed — two distinct defects in `callgrind/callstack.c`:
1. `CLG_(unwind_call_stack)` — minpops/SP-lower accounting (the `malloc -> driftsort_main` inversion).
2. `CLG_(reconstruct_call_stack_from_native)` — seeded `ret_addr` off-by-one (inversion across seeded frames when instrumentation starts mid-run). See "Second defect" below.
**Linear:** COD-2985
**Related:** KDE bug 252091 (Callgrind on ARM mis-detects returns, converts a failed return into a fake call) — both defects are in that failure *class* but have different triggers.
**Scope:** issue #2 (workload functions wrongly shown recursive / inverted). Issue #1
(the `_dl_tlsdesc_return` resolver showing up at all) is a separate, out-of-scope
cosmetic item handled elsewhere.

---

## Symptom

On **AArch64 only**, callgrind/CodSpeed Simulation flamegraphs contain call edges
that point the wrong way and recursion that does not exist. The canonical example
from the Rust fractal benchmark:

```
malloc → core::slice::sort::stable::driftsort_main          (inverted: driftsort CALLS malloc)
malloc → hashbrown::raw::RawTable<T,A>::reserve_rehash'2     (inverted)
malloc'2, _int_malloc'2, free'2, driftsort_main'2, ...       (fabricated recursion clones)
```

Validated facts going in (from prior investigation):

- x86_64 is **clean**; only AArch64 is affected.
- Reproduces on **old** valgrind-codspeed versions (e.g. 3.16.0-codspeed1) — pre-existing,
  not a recent regression.
- **Not** caused by the instrumentation-start shadow-stack seeding.
- Affects native code (Rust / C / C++ / CPython C extensions). Node.js (JIT) is fine.
- Wall-time profiling is fine (this is specific to the Callgrind/Simulation call graph).

---

## Reproduction

Minimal C reproducer (`callgrind/tests/recursion_inversion.c`): a **non-recursive**
leaf that calls libc `malloc`/`free` (reached through the PLT), called from a
**genuinely recursive** parent.

```c
static long leaf_alloc(int n) {            /* NOT recursive; allocates via libc */
    char *b = malloc(n); memset(b,1,n); long a = b[0]+b[n-1]; free(b); return a;
}
static long rec_parent(int d) {            /* genuinely recursive */
    long a = leaf_alloc(32); if (d>0) a += rec_parent(d-1); return a;
}
```

- Bug present → `leaf_alloc → rec_parent'2` (inverted) and a `leaf_alloc'2` clone.
- Control (leaf does pure arithmetic, no libc call) → completely clean. This pinned
  the trigger to **a function reached through an emulated/PLT call returning on AArch64**.

To reproduce by hand against the real benchmark:

```
CODSPEED_RUNNER_MODE=instrumentation CODSPEED_PROFILE_FOLDER=/tmp/out \
setarch aarch64 --addr-no-randomize ./vg-in-place -q --cache-sim=yes \
  --instr-atstart=no --read-inline-info=yes --tool=callgrind --compress-strings=no \
  --combine-dumps=yes --dump-line=no --callgrind-out-file=/tmp/out/%p.out \
  <bench_fractal binary> rust_fractal_computation
```

---

## Root cause

The call/return classifier in `callgrind/bbcc.c::CLG_(setup_bbcc)` reads the live
guest SP (`VG_(get_SP)`) and uses it to decide what to pop on a return. The relevant
property:

- On **x86**, `call` pushes the return address, so the SP **decreases** across a call.
  A callee's shadow-stack entry frame therefore records an SP that is **strictly below**
  the SP the caller will have after the matching `ret`.
- On **AArch64**, `bl`/`blr` put the return address in `x30` and leave SP **unchanged**
  (`VEX/priv/guest_arm64_toIR.c`). A callee's entry frame records the **caller's** SP, so
  after the callee restores its frame and executes `ret`, the return-target SP is **equal**
  to the entry frame's recorded SP. (PPC `b(c)l` has the same property — hence the existing
  "SP can stay the same over CALL/RET" handling for PPC.)

Because of that, on AArch64 a returning function's own entry frame is an **SP-equal**
frame, and it sits on the shadow stack **beneath the SP-lower frames** of any sub-calls
the function made (e.g. the `_int_malloc` / arena frames opened while inside `malloc`).

When the function returns, `setup_bbcc` computes `popcount_on_return` (how many SP-equal
frames this return should pop, via the `ret_addr`-matching loop) and calls
`CLG_(unwind_call_stack)(sp, popcount_on_return)`. The bug was in that unwinder:

```c
/* BEFORE */
if ((top_ce->sp < sp) || ((top_ce->sp == sp) && minpops>0)) {
    minpops--;                 /* <-- decremented for SP-LOWER pops too */
    unwind_count++;
    CLG_(pop_call_stack)();
    continue;
}
```

`minpops` is meant to bound only the **SP-equal** pops. But it was decremented for
**SP-lower** pops as well. So the still-open sub-call frames (SP-lower) consumed the
budget, it hit 0 before reaching the function's **SP-equal entry frame**, and that entry
frame was **left stuck** on the shadow stack.

Two consequences flow from the stuck frame:

1. **Inverted edges.** The stuck frame keeps the callee's context (`current_state.cxt`)
   active. The caller's continuation after the call is then recorded *inside* the callee,
   so when the caller makes its next call the edge is logged as `callee → next`
   (e.g. `malloc → driftsort_main`, or in the reproducer `leaf_alloc → rec_parent`).
2. **Fabricated recursion.** The stuck frame's per-function depth counter
   (`get_fn_entry`, incremented in `push_call_stack`) is never decremented, so the
   function looks like it is on the stack ≥2 times and callgrind emits a `'2`
   separate-recursion clone of a function that never recurses (`malloc'2`, etc.).

This cascades: every libc-internal level that returns "through" a stuck SP-equal frame
leaves its own entry frame stuck, which is why the whole allocator subtree was affected.

### Why x86 was immune

On x86 a function's entry frame is **SP-lower** (the `call` pushed a return address), so
it is popped by the always-pop SP-lower branch and never depends on `minpops`. Returns
never enter the SP-equal/`ret_addr`-match path that the bug lives in. PLT/tail-call
**emulated** frames borrow the parent SP on every arch (`bbcc.c`), but on x86 the parent
frame itself is SP-lower (real `call`), so those borrowed frames are SP-lower too and
also pop freely. Only AArch64/PPC produce SP-**equal** entry frames.

---

## The fix

`callgrind/callstack.c::CLG_(unwind_call_stack)` — split the condition so that SP-lower
frames always pop and **do not** consume the `minpops` budget; only SP-equal pops are
budget-bounded:

```c
/* AFTER */
if (top_ce->sp < sp) {                       /* left below new SP: always unwind */
    unwind_count++;
    CLG_(pop_call_stack)();
    continue;
}
if ((top_ce->sp == sp) && minpops>0) {       /* SP-equal entry frame: budget-bounded */
    minpops--;
    unwind_count++;
    CLG_(pop_call_stack)();
    continue;
}
break;
```

After the fix, a return pops all the SP-lower sub-call frames freely, then pops the
function's SP-equal entry frame within the (correctly preserved) budget, restoring the
caller's context.

The change is minimal, targets the **general** missed-return mechanism (no symbol-name
matching), and is provably inert on x86 (returns there never take the SP-equal path).
It also fixes the same latent bug on PPC, though that was not exercised here.

---

## Validation (this machine, aarch64, Cortex-A72)

Trace-level: with `--ct-verbose`, the instruction immediately after `malloc` returns
into `leaf` is attributed to **`malloc`** before the fix and to **`leaf`** after — the
context is correctly restored.

Real `bench_fractal` benchmark, clean A/B (same binary, fix stashed vs applied):

| Metric | Unfixed | Fixed |
|---|---|---|
| Inverted allocator→workload edges | **6** (incl. `malloc→driftsort_main`, `malloc'2→driftsort_main'2`, `free'2→analyze_fractal_tree'2`, `free'2→recursive_path_score`) | **0** |
| `driftsort_main` parent | `malloc` (wrong) | `graph_benchmark::analyze_fractal_tree` (correct) |
| Fabricated alloc-entry clones | `malloc'2`, `__rust_alloc'2`, `__rdl_alloc'2` | removed |
| Output determinism | — | identical across runs |

Callgrind regression suite (`perl tests/vg_regtest callgrind`):

- The fix turns `inline-crossfile` and `inline-samefile` from **FAIL → PASS** (inline
  tracking depends on correct call attribution, which the bug corrupted on AArch64).
- No test regresses. The only remaining failures are pre-existing and environment/arch
  specific: `bug497723` (its memcheck test binary isn't built) and `find_debuginfo`
  (its expected output hardcodes `ld-linux-x86-64.so.2`).

New regression test `callgrind/tests/recursion_inversion`:

- **Fails** on the unfixed baseline with exactly the reported signature
  (`leaf_alloc'2`, `leaf_alloc → rec_parent'2`).
- **Passes** on the fix. Wired into `Makefile.am` (`make check` builds it; arch-independent
  — it only asserts that non-recursive `leaf_alloc` has no clone and that no allocator
  function appears as a parent of the workload).

---

## Second defect: seeded-frame return off-by-one

A separate but related defect in the same class (independently spotted; KDE 252091 is the
historical reference). It is **not** the cause of `malloc -> driftsort_main` — the A/B above
isolates that to the minpops fix (the seeding code was untouched in both arms, yet the fixed
arm had 0 inversions). This second defect bites a **different** trigger: returns that cross a
**seeded** frame when instrumentation is started mid-run (`CALLGRIND_START_INSTRUMENTATION`,
as CodSpeed does).

`CLG_(reconstruct_call_stack_from_native)` seeds a shadow-stack frame per native caller using
`VG_(get_StackTrace)`. That API reports caller frames at the **last byte of the call
instruction** (`coregrind/m_stacktrace.c:1265` → `ips[i] = uregs.pc - 1`), i.e. `return_PC - 1`,
**not** the return PC. But `setup_bbcc`'s return matcher compares `ret_addr == bb_addr(return-target)`
(= the return PC), and `push_call_stack` stores exactly the return PC for real calls. So the
seeded `ret_addr = ips[frame+1]` was off by one. On AArch64 a `ret` lands at SP **equal** to the
seeded entry SP, so the match relies on `ret_addr`; the off-by-one fails it → the return is
demoted to a jump and re-promoted to a call → an inverted edge across the seeded frame.

**Fix** (`callstack.c`): `ce->ret_addr = ips[frame+1] + 1` (normalize to the return PC).

**Reproducer / test** `callgrind/tests/seeded_return_inversion.c`: `outer -> mid -> inner` with
`CALLGRIND_START_INSTRUMENTATION` inside `inner` (so the calls are seeded and only the returns
are measured). Before the fix the output contains a spurious callee→caller edge (observed:
`mid -> outer`); after the fix there are no call edges among `inner/mid/outer` (only `inner`'s
real `malloc/free/memset`). The test fails on the unfixed build and passes on the fix.

This matters for CodSpeed because scoped profiling always starts instrumentation several frames
deep, so the workload returns back through seeded frames at the measurement boundary.

---

## Out of scope / follow-ups

- Residual libc-internal `'2` clones (`strncmp'2`, `malloc_consolidate'2`, `_int_malloc'2`,
  `sin'2`, …) are **pre-existing** (present in the unfixed baseline too) and are the same
  cosmetic class as issue #1. They are unchanged by this fix; all **workload-function**
  inversions and fabrications are eliminated.
- This fix is orthogonal to the `cod-2985-tlsdesc-pop-on-jump` work: that addresses the
  TLSDESC resolver's *tail-jump-away* (a frame that never executes `ret`); this addresses
  the *return* path.
- The Python `json_decode` / C++ cases are the same root-cause class as the Rust
  `driftsort` case and should benefit from this fix — recommend confirming on the
  Python and C++ e2e benchmarks before closing COD-2985.
