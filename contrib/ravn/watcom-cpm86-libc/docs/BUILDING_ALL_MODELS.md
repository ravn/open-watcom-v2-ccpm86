# Building & testing the CP/M-86 clib in all memory models

The library builds for **three memory models** — small (`s`), medium (`m`),
compact (`c`) — from the SAME source, differing only by the `-m$MODEL` compile
flag. This document is the canonical build/test process.

## The two-step process

The build and the test gate are separate steps (there is no single Makefile/CI
driver — these are shell scripts run against the scratch Open Watcom build tree).

### 1. Build + install the libraries — `build-lib.sh`

```bash
for MODEL in s m c; do OUTDIR="build-lib-$MODEL" MODEL=$MODEL bash build-lib.sh; done
```

Each run compiles the whole clib with `-m$MODEL` and installs, into the
`.gitignored` `../../../lib286/cpm86/`:

| Model | C library    | startup        | math library |
|-------|--------------|----------------|--------------|
| s     | `clibs.lib`  | `cstartcpm.obj`| `libms.lib`  |
| m     | `clibm.lib`  | `cstartmm.obj` | `libmm.lib`  |
| c     | `clibc.lib`  | `cstartcm.obj` | `libmc.lib`  |

`clib$MODEL.lib` is the C runtime (string/ctype/stdlib/stdio FILE*, near+far
heap, soft-float double arithmetic, time, the crt0). `libm$MODEL.lib` is the
transcendentals (`sin`/`cos`/`atan`/`exp`/`log`/`sqrt`), kept separate — the
classic `-lc`/`-lm` split — so a program that never uses `<math.h>` pays nothing.

**libm is per-model, not one-for-all**, because although the arithmetic is
identical the objects differ for two model-driven reasons: (1) **code model** →
near vs far `RET` (a medium far-code caller must far-call; `mm/atan.obj` uses
`retf`, `ms`/`mc` near `ret`); (2) **data model** → a function's private
coefficient tables sit in DGROUP for near-data (s/m) but embed in the code
segment for far-data compact (`mc/atan.obj`'s `_TEXT` is larger). Watcom itself
ships mc/mm/ms/ml/mh mathlib for the same reason.

### 2. Run the all-models gate — `run-all-models.sh`

```bash
bash run-all-models.sh            # all models, all tests
MODELS="s c" bash run-all-models.sh   # subset
```

It **requires step 1 first** (it checks for the installed libs and aborts with
`run 'MODEL=x bash build-lib.sh' first` if missing — it does not build them).
For each model it compiles a test, links it against the installed
`clib$MODEL.lib` (+ `libm$MODEL.lib` where needed), runs it, and gates on an
oracle. Linking against the FULL installed lib (not a bespoke object list) is
deliberate: a link failure is a genuine "routine missing from the archive" gap.

| Test    | Exercises                    | libm? | Runner  |
|---------|------------------------------|-------|---------|
| heap    | near+far malloc/free/qsort   | no    | Unicorn |
| stdio   | FILE\* write path            | no    | Unicorn |
| float   | double soft-float arithmetic | no    | Unicorn |
| math    | sin/cos/atan/exp/log/sqrt    | yes   | Unicorn |
| fltfmt  | real %e/%f/%g/%a (opt-in)    | yes   | Unicorn |
| scanf   | sscanf %f/%d (opt-in)        | yes   | Unicorn |
| disk    | FILE\* + POSIX file I/O      | no    | emu2    |

**7 tests × 3 models = 21, all green.**

## Runners: why two

- **Unicorn** (`cpm86run_unicorn.py`) is a CPU + console/string-BDOS harness. It
  applies P_LOAD load-time relocation, so it runs small, medium AND compact
  `.CMD`s. It does NOT emulate the file BDOS, so disk tests can't use it.
- **emu2** emulates the file BDOS, so it runs the disk test. It now ALSO applies
  P_LOAD relocation (ravn/emu2-cpm86#1), so it runs medium/compact `.CMD`s too —
  which is why disk is covered in all three models, not just small.

Neither emulates an 8087; a stray FPU opcode would trap. All float code compiles
`-fpc` (the pure-software `__FDxemu` path), so this doubles as a no-8087 gate.

## Float printf

`%e`/`%f`/`%g` output is opt-in (call `__setEFGfmt()` + link libm) — see
[`FLOAT_PRINTF.md`](FLOAT_PRINTF.md). The other tests print scaled `%ld`.

## Adding a routine / test

- Missing library routine: add its source to the matching section of
  `build-lib.sh` and to the `wlib` archive list, rebuild, re-run the gate.
- New test: drop `test/<name>.c` with a computable oracle, add one `run_test`
  line to `run-all-models.sh`.
