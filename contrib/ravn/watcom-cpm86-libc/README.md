# watcom-cpm86-libc — retargeting Open Watcom's own C library to CP/M-86

Tracking issue: **ravn/rc7xx-work#6**.

**Status:** foundation **proven end-to-end** (run-verified under emu2). This
directory demonstrates that Open Watcom's *own* clib can be retargeted to
CP/M-86 (Intel 8086/80186, e.g. RC759) by supplying only a **thin BDOS
low-level seam** — no DR C bridge, no Aztec C recompile.

Run `./build.sh` — it compiles everything from the scratch OW build tree,
links a CP/M-86 `.CMD`, checks the image is DOS-free, runs it under emu2, and
gates the output against an independent oracle. Expected tail:

```
purity: INT21h(DOS)=0  INTE0h(BDOS)=3
PASS: matches independent oracle (123456*789=97406784 via __I4M)
```

Then run `./build-heap.sh` to prove Watcom's **genuine near-heap** (`malloc`/
`free`/`calloc`/`realloc`) + `qsort` on CP/M-86, resolved only by the arena
`__brk`/`sbrk` seam. Expected tail:

```
purity: INT21h(DOS)=0  INTE0h(BDOS)=3
PASS: Watcom near-heap (malloc/free/calloc/realloc) + qsort on CP/M-86
```

## Why this exists (the pivot)

Earlier work tried two other routes to a CP/M-86 libc: (a) bridge to the
proprietary **DR C** runtime (`owc-drc/`, works but non-free + a float-ABI
seam), and (b) **recompile Aztec C** stdlib source (ABI mismatch vs Watcom's
`long`/`double`, plus Manx-syntax asm for `str*`/`mem*`). Both are dead ends
for a clean, self-owned, single-toolchain libc.

The insight that unlocked this: **Watcom's clib is explicitly layered and
retargetable.** The high-level formatter/`stdio` code is OS-agnostic; only a
handful of *low-level primitives* (`read`/`write`/`open`/`close`/`lseek`/
`sbrk`) carry the OS trap. For 16-bit DOS those live in a single file
(`bld/clib/_dos/a/io086.asm`, `mov ah,40h; int 21h`). A CP/M-86 port =
reuse everything above and swap that thin bottom for BDOS (INT E0h).

## Architecture (three layers)

```
  printf / __prtf / itoa / str* / long+float math   <- Layer 1: Watcom clib,
                                                        REUSED UNCHANGED
  ------------------------------------------------
  cprintf.c  : __prtf(...) + output callback         <- Layer 2: thin CP/M-86
  crt0sm.asm : startup, SS setup, BDOS exit             seam WE own
  ------------------------------------------------
  BDOS console C_WRITE (INT E0h, CL=2)               <- Layer 3: CP/M-86 OS
```

`__prtf(dest, fmt, args, callback)` is Watcom's pure formatter core
(`bld/clib/streamio/c/prtf.c`) — verified to contain **0× INT 21h**. It emits
each formatted character through a caller-supplied callback. `port/cprintf.c`
supplies a callback that writes to the CP/M-86 console via BDOS. That one
function is the entire retarget seam for console output.

The 32-bit `long` path (`%ld`) is handled by Watcom's own compiler-helper
runtime `__I4M`/`__U4M`/`__I4D`/`__U4D` (`bld/clib/cgsupp/a/i4m.asm`,
`i4d.asm`) — assembled unchanged. This is the "Watcom kernel for int and
double/float we keep" the project wants; only the OS seam is ours.

## What the demo proves

`test/main.c` prints `%s %d %ld` (with `123456L*789L = 97406784`, exercising
`__I4M`), then `%x`, width `[%5d][%-5d]`, and `%c`. A byte-exact match to a
hand-computed / host-`printf` oracle proves the full path: **Watcom formatter +
Watcom long-helpers + BDOS callback**, linked by our paragraph-packing-fixed
`wlink`, running on CP/M-86.

Note (16-bit gotcha): in this `-ms` (16-bit `int`) target, a decimal literal
that exceeds `INT_MAX` (e.g. `40000`) is a **`long`**, so it must be printed
with `%lu`/`%ld` (or cast), never `%u`/`%d`. That is standard C vararg width
matching, not a library bug — the formatter itself was verified correct for
`%x`/`%u` in isolation.

## What the heap proof adds (`build-heap.sh`)

`test/heaptest.c` exercises Watcom's **genuine near-heap** — `malloc`, `free`,
`calloc`, `realloc` (plus `qsort`) — resolved *only* by the thin `lowlevel.c`
seam. It sorts a fixed permutation to `0 1 2 3 4 5 6 7 8 9`, checks `calloc`
zeroes, grows a block with `realloc` preserving its prefix, and re-`malloc`s
after `free`. A byte-exact match to the hand-computed oracle (in the script),
with **zero INT 21h** in the image, proves the retarget's central claim:
Watcom's whole near-heap manager runs on CP/M-86 with only its bottom
primitive swapped.

The swap is one function. Watcom's DOS heap grows DGROUP via `sbrk.c`'s `__brk`,
whose *only* OS act is `INT 21h AH=4Ah` (`TinySetBlock`, resize the program's
block). `lowlevel.c` replaces `__brk`/`sbrk` with a pure in-DGROUP bump over a
static `_BSS` arena and defines the `_curbrk` RT-data word `grownear.c` reads —
no OS call, because a CP/M-86 `.CMD` already owns its whole TPA. Every heap
manager above `__brk` (`nmalloc`/`nfree`/`calloc`/`nrealloc`/`grownear`/`mem`…)
is Watcom's unmodified clib.

## What the stdio proof adds (`build-stdio.sh`)

`test/stdiotest.c` drives Watcom's **genuine `stdio` FILE\* write-path** —
`printf`, `fprintf`, `puts`, `fputs` — end to end on CP/M-86. This is the real
buffered `FILE` machinery, not the direct-`__prtf` console shortcut: each call
runs `__fprtf`/`fputc` into a `malloc`'d `FILE` buffer, then `__flush` →
`__qwrite`. A byte-exact match to the oracle (`printf 42 ok` / `puts line` /
`fputs line` / `fprintf 97406784`) with **zero INT 21h** proves the whole
`stdio` upper layer is OS-agnostic.

Only two thin seams make it run, both in `port/stdioshim.c`:

- **`__qwrite`** — the single low-level write `__flush` calls (DOS bottoms it
  out in `TinyWrite` → `INT 21h AH=40h`). Ours writes console handles 1/2 via
  BDOS `C_WRITE` (`INT E0h`, `CL=2`), bytes verbatim — Watcom's text-mode
  `fputc` has already turned `\n` into `\r\n` in the buffer.
- **`isatty`** — `__ioalloc`→`__chktty` calls it to pick a buffering mode (DOS
  bottoms out in `INT 21h AH=44h` IOCTL). Ours reports the three standard
  handles as a tty, which also line-buffers `stdout`.

The `FILE` buffer itself comes from the near-heap proof above (each `FILE`'s
`__stream_link` and buffer are `malloc`'d). `__InitFiles` — Watcom's own, and
genuinely DOS-free (it only calls the arena `malloc` to attach a `__stream_link`
to each std `FILE`) — is called once at startup; a fuller crt0 would run it off
the `XI` init table via `__InitRtns` (see
`tasks/memory/reference_watcom_cpm86_startup_initfini.md`).

## What the stdcbench proof adds (`build-stdcbench.sh`)

The three proofs above each exercise one library subsystem with a toy driver.
**stdcbench 0.8** is the opposite: a substantial, multi-module benchmark that
leans on the *whole* library at once — `printf`/`sprintf`, the `string` and
`ctype` families, `malloc`/`free`/`realloc`, `qsort`, and the 32-bit long
helpers. `build-stdcbench.sh` compiles the **byte-identical upstream stdcbench
sources** (the same tree `owc-drc/stdcbench` built on the DR C runtime) and
links them against Open Watcom's **own, unchanged clib** plus our thin CP/M-86
seams — no Digital Research C runtime, no `clears.l86`. Only `test/scbport.c`
(the stdcbench glue: a BDOS `T_GET` clock, `__InitFiles`, `main`) is ours.

**Two run-verified results:**

- **Functional (emu2):** the whole benchmark runs end to end — every module
  executes and both scores compute correctly through the retargeted clib. emu2's
  clock is the host wall clock, so *that* score reflects the Mac's speed, not the
  RC759; the emu2 run proves execution, not performance.
- **Comparable (MAME rc759):** the SAME `SCB.CMD`, built with `-DMAME_DONE`
  (`run-stdcbench-mame.sh`), on the cycle-accurate MAME rc759 (Concurrent
  CP/M-86 3.1, PICCOLINE XIOS 2.3) scores **c90base 12 · c90lib 8 · final 20**
  (Watcom full optimisation `-otexan`), versus the Digital Research C reference
  **8 · 5 · 13** on the identical machine — the retargeted Watcom clib is
  ~1.5× faster than the ~1984 DR C runtime on both the integer and the
  library-heavy modules, an independent correctness + performance cross-check.

The clock differs from the DR C port on purpose: DR C read BDOS `T_SECONDS`
(fn 155), which emu2 does not implement (so it would spin forever there); we read
`T_GET` (fn 105), which both emu2 (host clock) and the RC759 XIOS maintain, so
the same binary times on both. stdcbench's `c90base`/`c90lib` are integer +
standard-library work only (the `c90float`/`c90double` modules are upstream
stubs in 0.8), so this proof exercises **no floating point** — it neither uses
nor retires the `double` ABI seam.

## Files

| Path | Role |
|------|------|
| `build.sh` | printf proof: reproducible build + purity gate + emu2 oracle gate |
| `build-heap.sh` | heap proof: Watcom malloc/free/calloc/realloc + qsort on CP/M-86 |
| `build-stdio.sh` | stdio proof: Watcom genuine FILE\* printf/fprintf/puts/fputs on CP/M-86 |
| `build-stdcbench.sh` | stdcbench proof: full stdcbench 0.8 (c90base+c90lib) on Watcom clib + shim; purity gate + emu2 functional run |
| `run-stdcbench-mame.sh` | stdcbench cross-check: builds `-DMAME_DONE` + runs SCB.CMD on cycle-accurate MAME rc759 (score 20 vs DR C 13) |
| `port/cprintf.c` | Layer-2 seam: `__prtf` + BDOS `C_WRITE` callback (direct console printf) |
| `port/stdioshim.c` | Layer-2 seam: `__qwrite` (BDOS console write) + `isatty` for the FILE\* path |
| `port/lowlevel.c` | Layer-2 seam: arena `__brk`/`sbrk` + `_curbrk` (near-heap bottom) |
| `port/crt0sm.asm` | CP/M-86 small-model startup (SS setup, `wc_heap_init`, BDOS exit, `__STK` stub) |
| `port/stubs.c` | never-reached closure stubs (`_ismbblead`, `__fatal_runtime_error`, `errno`, read-path `__lseek`/`fsync`) |
| `test/main.c` | printf demo driver / oracle |
| `test/heaptest.c` | heap demo driver / oracle |
| `test/stdiotest.c` | stdio FILE\* demo driver / oracle |
| `test/scbport.c` | stdcbench glue: BDOS `T_GET` clock, `__InitFiles`, `main`, `mame_done` |
| `test/portme.h` | stdcbench 0.8 port config (integer c90base+c90lib set) |

## Toolchain

Uses the pre-built cross tools + clib source from the scratch OW tree
(override with `OW=`): `wcc.exe`, `wasm.exe`, `wlink.exe`; runs under `emu2`
(override with `EMU2=`). `wlink` must include the CP/M-86 paragraph-packing
fix (`f21f6a9f`).

## Next milestones (not yet done)

Tracked in ravn/rc7xx-work#6:

1. ~~**Near-heap bottom** — arena `__brk`/`sbrk` so Watcom `malloc`/`free`/
   `calloc`/`realloc` link and run.~~ **DONE** (`build-heap.sh`, run-verified).
2. ~~**stdio FILE\* write path** — override the DOS low-level primitives so the
   *full* buffered `stdio` FILE\* layer (`printf`/`fprintf`/`puts`/`fputs` →
   `__fprtf`/`fputc` → `__flush` → `__qwrite`) links and runs, beyond the
   direct-`__prtf` console `printf`.~~ **DONE for the console write-path**
   (`build-stdio.sh`, run-verified): `port/stdioshim.c` supplies `__qwrite`
   (BDOS `C_WRITE`) + `isatty`; Watcom's own DOS-free `__InitFiles` attaches the
   `FILE` buffers from the near-heap. Still open: the **disk** FILE\* path
   (`open`/`close`/`read`/`lseek`), which must honour the CP/M record model —
   files are 128-byte sectors with no exact byte length; text files use a
   Ctrl-Z (0x1A) terminator, binary files have none.
3. ~~**stdcbench relink** — rebuild the existing `owc-drc/stdcbench` port against
   Watcom clib + this shim instead of the DR C runtime; gate on the
   c90base + c90lib score, cross-checked against the DR C reference (final
   score 13) as an independent correctness oracle.~~ **DONE**
   (`build-stdcbench.sh` + `run-stdcbench-mame.sh`, run-verified): the
   byte-identical upstream stdcbench 0.8 integer suite links against Watcom's
   own unchanged clib + our seams and runs end to end. On cycle-accurate MAME
   rc759, built with Watcom full optimisation (`-otexan`), it scores **c90base
   12 · c90lib 8 · final 20**, versus DR C's **8 · 5 · 13** on the identical
   machine — ~1.5× faster than the DR C runtime. Note: `c90base`/`c90lib`
   exercise **no floating point** (the `c90float`/`c90double` modules are
   upstream stubs), so this does **not** retire the `double` ABI seam — that
   remains for a float-exercising target.

## Next milestones (not yet done)

Still open (tracked in ravn/rc7xx-work#6):

- **disk FILE\* path** (`open`/`close`/`read`/`lseek`) honouring the CP/M
  record model — 128-byte sectors, no exact byte length; text files use a
  Ctrl-Z (0x1A) terminator, binary files have none.
- a **float-exercising** run-target to drive and retire the `double` ABI seam.
