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

## Files

| Path | Role |
|------|------|
| `build.sh` | reproducible build + purity gate + emu2 oracle gate |
| `port/cprintf.c` | Layer-2 seam: `__prtf` + BDOS `C_WRITE` callback |
| `port/crt0sm.asm` | CP/M-86 small-model startup (SS setup, BDOS exit, `__STK` stub) |
| `port/stubs.c` | never-reached closure stubs (`_ismbblead`, `__fatal_runtime_error`) |
| `test/main.c` | demo driver / oracle |

## Toolchain

Uses the pre-built cross tools + clib source from the scratch OW tree
(override with `OW=`): `wcc.exe`, `wasm.exe`, `wlink.exe`; runs under `emu2`
(override with `EMU2=`). `wlink` must include the CP/M-86 paragraph-packing
fix (`f21f6a9f`).

## Next milestones (not yet done)

Tracked in ravn/rc7xx-work#6:

1. **Low-level BDOS shim** — CP/M-86 `read`/`write`/`open`/`close`/`lseek`/
   `sbrk`/`exit` overriding the DOS primitives, so the *full* `stdio` FILE\*
   layer links unmodified (beyond this direct-`__prtf` console `printf`).
2. **stdcbench relink** — rebuild the existing `owc-drc/stdcbench` port against
   Watcom clib + this shim instead of the DR C runtime; gate on the
   c90base + c90lib score. This also retires the DR C float-ABI seam.
