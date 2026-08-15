# Floating point on CP/M-86 with no 8087 — the Watcom software-emulator route

> **STATUS / CORRECTION (supersedes Section 1 below).** This document is now the
> **DEFERRED-hardware design note**, not the shipped route. Its original Section 1
> premise — *"there is no pure-integer soft-float; both `-fpi` and `-fpc` execute
> real 8087 ESC opcodes"* — was **empirically refuted**: `-fpc` emits plain
> `call __FDA/__FDS/__FDM/__FDD` with **zero ESC, zero INT traps**, and Watcom's
> `fdmth086.asm` runs the pure-software `__FDxemu` branch whenever `__real87 == 0`
> (which `port/fpsoftstub.asm` guarantees). So the no-8087 target needs **no
> emulator and no interrupt-vector install at all**. The shipped, proven route is
> `-fpc` soft-float — see
> [`8087_HARDWARE_SUPPORT_DEFERRED.md`](8087_HARDWARE_SUPPORT_DEFERRED.md) for the
> supported path and the proof. The INT 0x34–0x3D emulator design below is kept
> only as the **deferred** alternative for a contributor with 8087 hardware; the
> `port/emu87cpm.asm` IVT seam it describes is unused by the `-fpc` build.

This is the design note for the float/double milestone (tracks rc7xx-work#8):
running Open Watcom's **unchanged** floating-point path on the RC759 (an 80186
machine with **no 8087 coprocessor**) through our thin Layer-2 seam. It records
the verified findings and the one new OS seam this milestone needs — installing
the 8087-emulator interrupt vectors **without the fatal DOS INT 21h**.

> Ultimate oracle: this must work on **real RC759**. Cycle-accurate MAME rc759 is
> the authoritative cross-check; a green emu2 alone is not sufficient (emu2 may
> not faithfully model a no-8087 machine).

## 1. Watcom 8086 float *is* the 8087 emulator (verified)

There is **no pure-integer soft-float** in the Watcom 8086 C library. Both `-fpi`
(inline emulated) and `-fpc` (library calls) ultimately execute real 8087 ESC
opcodes:

- `bld/clib/cgsupp/a/fdmth086.asm` (`__FDA/__FDS/__FDM/__FDD`) contains genuine
  `fld/fadd/fmul/fstp/fwait` instructions.
- `bld/fpuemu/i86/asm/emu8087.asm` is the pure-software interpreter (0 native
  8087 opcodes, 0 INT 21h) that executes the *trapped* operations. **The emulator
  is the soft-float.**

So to run float on a no-8087 RC759 we must link Watcom's own 8087 software
emulator and route the trapped opcodes to it.

## 2. How the trapped opcodes reach the emulator (verified by spike)

The compiler emits `FWAIT`+ESC with **emulator FIXUP** records. `wlink
format cpm86` rewrites them into `INT 0x34–0x3D` (the magic entry constants
`FIARQQ=0FE32H`, `FISRQQ=0632H`, … published by `bld/fpuemu/i86/asm/initemu.asm`).

Spike result on a minimal `a*b + a/b - b` program linked `format cpm86`:

```
INT 0x34-0x3D counts: {'0x38': 3, '0x39': 3, '0x3a': 1, '0x3b': 1, '0x3d': 1}
total emulator INTs: 9      raw ESC bytes: 0      FWAIT 0x9B: 0
```

Observed mapping: **0x38** = arithmetic (add/sub/mul/div), **0x39** = load/store
(fld/fst), **0x3a** = stack ops (faddp), **0x3b** = int store (fistp), **0x3d** =
control/fwait. The emulator only has to service INT **0x34–0x3D** (10 vectors).

## 3. Installing the emulator vectors on CP/M-86 — the IVT conclusion

**Authoritative sources (both in-workspace):**
- **CP/M-86 System Guide (Jun 1983)** — `../../CPM-86_System_Guide_Jun83.txt` (+.pdf),
  i.e. `open-watcom-v2/contrib/ravn/CPM-86_System_Guide_Jun83.txt`.
- **Siemens Concurrent CP/M-86 Programmer's Reference Guide** (the actual RC759 OS)
  — `../../Siemens_Concurrent_CPM-86_Programmers_Reference_Guide.txt`, i.e.
  `open-watcom-v2/contrib/ravn/Siemens_Concurrent_CPM-86_Programmers_Reference_Guide.txt`
  (moved here alongside the other DRI manuals).

**Conclusion: a transient installs an interrupt handler by writing the far pointer
straight into the segment-0 interrupt vector table. There is no BDOS set-vector
call, and no INT 21h is involved or permitted.**

- The System Guide's own BIOS "Setup all interrupt vectors in low memory" sample
  does exactly this: `mov ax,0 / mov ds,ax` then store `offset handler` and `cs`
  into the vector slot. **INT N's vector lives at physical `0000:N*4` (offset
  word) and `0000:N*4+2` (segment word).**
- The reserved interrupt region is **0–3FFH** (256 vectors × 4 bytes), always
  present and writable, and is deliberately excluded from a transient's Memory
  Region Table. BDOS entry is itself just such a reserved software interrupt —
  **INT 224 (0E0H)**.
- Concurrent CP/M-86 §3.4 confirms transients set their own vectors: a child
  "inherits interrupt vectors **0, 1, 3, 4, 224, and 225**, which the parent
  process initialized." The same direct mechanism extends to any free vector.
- The 8087-emulator INT 0x34–0x3D are **synchronous software traps** fired inline
  by the emulated ESC/FWAIT in the *same* process context — so no scheduler
  cooperation (`DEV_SETFLAG`/`DEV_WAITFLAG`, which are for asynchronous hardware
  interrupts) is needed. A plain IVT poke suffices.
**Independent confirmation from DR C (Digital Research C, the RC759's own C
compiler).** Cross-checked against the actual DR C runtime source
`scratch/rc759-cmd-toolchain/rc759-drc-official/startup.a86` — an implementation
that shares *none* of Watcom's DOS code path, so it is a genuine correctness
oracle (not an equivalence check):
- DR C's `m.init.hardware.error` (startup.a86 ~lines 1339–1347) installs the
  integer zero-divide handler by the **identical direct segment-0 poke**:
  `sub bx,bx / mov ds,bx` (DS = segment 0) then `mov [bx],offset zerodiv /
  mov 2[bx],cs` — offset word then segment word straight into the IVT, no BDOS,
  no INT 21h. This is exactly what `port/emu87cpm.asm`'s `xchg_vects` does for
  INT 0x34–0x3D at `0000:00D0`.
- DR C's own `m.init.8087` call is **`if 0` / compiled out** of the C startup
  (startup.a86 ~lines 483–486, commented *"for Fortran 77 only, not for DRC"*).
  So DR C sets its emulator up **library-driven / self-registering** via the math
  library rather than an explicit startup call — which is precisely how Watcom's
  `xinit`/`xfinin` init-table registration works, and which `emu87cpm.asm` keeps
  verbatim.

Emulator vectors INT 0x34–0x3D occupy physical **`0x34*4 = 0x00D0 … 0x00F7`**
(10 vectors × 4 = 40 bytes), inside the reserved 0–3FFH region — clear of BDOS
INT 0xE0 (whose vector slot is at `0xE0*4 = 0x0380`).

## 4. The single new OS seam

The *entire* DOS coupling in Watcom's emulator is **two `INT 21H`** in
`initemu.asm`'s `xchg_vects` proc (get/set-vector fns 35H/25H, lines 116 & 126).
Everything else — the `FIxRQQ` entry constants, the `i34off/seg … i3doff/seg`
table, the `__int34…__int3d` INT handlers, `__init_87_emulator`,
`__fini_87_emulator`, `__x87id`, and the whole `emu8087.asm` engine — is
**DOS-free** (verified: 0× INT 21h in `emu8087.asm`).

Because the purity gate counts INT 21h *bytes statically*, we cannot link the
stock `initemu.asm` even if `xchg_vects` were never executed. So we carry a
CP/M-86 variant in `port/` (`port/emu87cpm.asm`) that is a faithful copy of
`initemu.asm` with **only** the `xchg_vects` body replaced by a direct segment-0
IVT **swap** (a swap, not a plain write, so that the second call from
`__fini_87_emulator` restores the previous vectors — identical semantics to the
DOS original):

```
xchg_vects proc near
    push ax / bx / cx / si / di / es
    xor  ax,ax
    mov  es,ax                 ; ES -> segment 0 (the IVT)
    lea  si,i34off             ; DS:SI -> 40-byte {off,seg} x10 table (DS = DGROUP)
    mov  di,34H*4              ; 0D0H = IVT byte offset of INT 0x34
    mov  cx,20                  ; 20 words = 10 vectors (off+seg each)
swapw:                          ; swap our table word <-> IVT word
    mov  ax,es:[di] / mov bx,[si] / mov es:[di],bx / mov [si],ax
    add  si,2 / add di,2 / loop swapw
    pop  es / di / si / cx / bx / ax
    ret
xchg_vects endp
```

**Status: written and assembled clean** (`wasm -bt=dos -0 -ms -fpi87`, 0 errors);
`wdis` of the object confirms **0× INT 21h**, `ES=0`, `DI=0x00D0`, `CX=0x14`
(20 words), and the swap loop over the `i34off` table — matching the DR C
technique above.

`__init_87_emulator` still runs `__x87id` (finds no FPU on RC759), fills the
table with the `__int34/__int3c/__int3d` handler addresses, then calls our poke
instead of the DOS `xchg_vects`. It is invoked once at startup before the first
float op (the float analogue of the `__InitFiles` call the stdio milestone made).

Startup glue is exactly the Layer-2 we own; the Watcom clib proper stays
byte-for-byte unchanged.

## 5. Oracle — Watcom's own regression tests first

Primary proof is **Watcom's own float regression suite** (`bld/ctest/positive/
source/float01–04.c`), self-checking via `fail.h`/`_PASS` — an independent,
Watcom-authored oracle. A `-Dmain=owfloat_main` wrapper calls each and prints a
single PASS / `FAIL line N` marker via the already-proven `printf`. Purity gate:
0× INT 21h, >0 INT E0h. Run under emu2, then cross-checked on MAME rc759 (the
true no-8087 oracle). A double Whetstone with known-value checks is a bonus
score, not the proof.
