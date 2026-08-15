# 8087 hardware floating-point support — DEFERRED

**Status: intentionally NOT supported. Left to someone with real 8087 hardware.**

The RC759 target this libc is built for is an **80186 machine with no 8087
coprocessor**. Per the project owner's decision (2026), floating point on this
target is provided **purely in software** — Open Watcom's own call-based
soft-float (`-fpc`) — and **no 8087 hardware path is shipped, tested, or
supported**. The two Watcom routes that need (or emulate) an 8087 are documented
below so a future contributor *who has an 8087-equipped machine* can pick them up,
but they are **out of scope and unverified here**.

---

## What IS supported (and proven): `-fpc` pure soft-float

Compile with `-fpc`. The compiler emits plain `call __FDA/__FDS/__FDM/__FDD`
(add/sub/mul/div) and `__FDI4` (double→long) — **zero 8087 ESC opcodes, zero
INT traps**. At runtime Watcom's `fdmth086.asm` dispatches each `__FDx` through a
data word that `_chk8087` initialises from `__real87`: because our
`port/fpsoftstub.asm` sets `__real87 = 0`, the dispatch always selects the
**pure-software `__FDxemu` branch**. The dead hardware `__FDx87` branch (the only
place ESC opcodes live in that file) is never reached.

Result: no 8087, no software-emulator interrupt vectors, no INT 21h, no IVT poke.

Proof (`build-float.sh` + `test/floattest.c`, run under emu2):

```
soft-float: 9 runtime __FDx call site(s) in floattest.obj (not folded)
purity: INT21h(DOS)=0  INTE0h(BDOS)=2  INT34-3D(8087emu)=0
--- output ---
pi6=3141592 mul=40115 add=468 sub=242
PASS: Watcom OWN double soft-float (-fpc, __FDxemu) on CP/M-86, no 8087
```

The oracle (`355/113 = 3.14159292…`) is hand-computed, independent of the
compiler. The test uses `volatile` operands so the arithmetic is **not**
constant-folded — the `__FDx` runtime is genuinely exercised (a tripwire in
`build-float.sh` fails the build if the `__FDx` calls ever disappear).

> Still pending for the supported path: cross-check `-fpc` output on
> **cycle-accurate MAME rc759** (the authoritative no-8087 oracle). emu2 is green
> but may not faithfully model a no-8087 machine. This is a verification step for
> the *software* path, not 8087 hardware work.

---

## What is DEFERRED (needs an 8087-equipped machine to test)

### Route A — `-fpi` inline-emulated (INT 0x34–0x3D trap emulator)

`-fpi` (Watcom's default) emits `FWAIT`+ESC with **emulator FIXUP** records that
`wlink format cpm86` rewrites into `INT 0x34–0x3D`. Those interrupts must be
serviced by Watcom's software 8087 interpreter (`bld/fpuemu/i86/asm/emu8087.asm`)
whose entry vectors have to be installed into the low interrupt-vector table.

On CP/M-86 the vectors **cannot** be installed via DOS `INT 21h` (fatal — see the
purity gate). The DOS-free technique is a **direct segment-0 IVT poke**
(`mov ds,0 / mov word ptr [n*4],offset handler / mov word ptr [n*4+2],cs`),
independently verified against DR C's runtime (`startup.a86`
`m.init.hardware.error` installs its zero-divide vector exactly this way, with no
INT 21h). A seam that does this for the 8087-emulator vectors is drafted and
assembles: **`port/emu87cpm.asm`**. It is **not needed for the supported `-fpc`
path and is UNVERIFIED** — nobody has run the trap-emulator route on hardware.

Design details and the spike results are in
[`FLOAT_8087_EMULATOR.md`](FLOAT_8087_EMULATOR.md).

### Route B — `-fpi87` real coprocessor

`-fpi87` emits real 8087 ESC opcodes and requires a physical 8087. The RC759 has
none, so this route is **not applicable** to the target and is mentioned only for
completeness. A contributor with an 8087-equipped 8086/80186 CP/M-86 machine could
use it directly (no emulator, no IVT seam).

---

## Handover checklist for a contributor with 8087 hardware

1. The `-fpc` software path (above) is the reference for *correct* results — diff
   any hardware run against `pi6=3141592 mul=40115 add=468 sub=242`.
2. For Route A: link `port/emu87cpm.asm` (installs INT 0x34–0x3D → `emu8087`),
   build the test with `-fpi`, and confirm on real hardware / MAME. The purity
   gate must still show `INT21h=0`.
3. For Route B: build with `-fpi87` on a machine that actually has an 8087.
4. Report findings back; only then should this project claim any 8087 support.

Until then: **soft-float only, no 8087 support.**
