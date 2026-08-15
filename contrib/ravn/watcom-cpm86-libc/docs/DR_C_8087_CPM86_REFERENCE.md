# DR C on CP/M-86 — how Digital Research C sets up floating point / the 8087
# emulator, used here as an INDEPENDENT oracle for the Watcom float route (#8)

**Why this file exists.** The RC759's own native C compiler is **Digital Research
C (DR C / DRC)**. When we retarget *Watcom's* float support to CP/M-86 we need an
independent check that our interrupt-vector-install technique is correct — a
source that shares **none** of Watcom's code so that "it agrees" means *right*,
not merely *self-consistent*. DR C is exactly that oracle. Its runtime startup is
readable assembler in the workspace, so the mechanism can be verified directly
rather than assumed.

**Source read (all citations below are to this file):**
`scratch/rc759-cmd-toolchain/rc759-drc-official/startup.a86`
("STARTUP for DRC V1.11" — several DRI startup fragments edited together:
`clear/minit/minitrel/minitsta/minithea/minitcmd/miniterr/minitos.a86`).
Opcode helper: `.../rc759-drc-official/8087def.a86` (RASM86 8087-mnemonic EQUs).

---

## 1. DR C installs interrupt handlers with a direct segment-0 IVT poke — no BDOS

`m.init.hardware.error` (startup.a86 ~L1317–1347) installs the integer
zero-divide handler. The whole routine is:

```asm
m.init.hardware.error:
        push    ds                      ; Save original DS.
        sub     bx,bx                   ; BX -> offset 0
        mov     ds,bx                   ; DS = 0        (segment 0 = the IVT)
        mov     [bx],offset zerodiv     ; IVT offset word at 0000:0000
        mov     2[bx],cs                ; IVT segment word at 0000:0002
        pop     ds                      ; Restore original DS.
        ret
```

The routine's own header comment states the algorithm outright: *"Place the
vector to the integer zero divide handler in absolute location 0:0."* There is
**no BDOS set-vector call and no `INT 21h`** — the far pointer (offset word then
segment word) is written straight into the segment-0 interrupt vector table.
INT N's slot is at physical `0000:N*4` (offset) + `N*4+2` (segment).

**This is the authoritative, code-verified confirmation of the mechanism our
`port/emu87cpm.asm` uses** for the 8087-emulator vectors INT 0x34–0x3D at
`0000:00D0…00F7`. It matches the CP/M-86 System Guide's BIOS "setup interrupt
vectors" sample and is proven here by a *second, independent* implementation.

> Note: DR C does **not** `cli`/`sti` around this single-vector write (it does
> disable interrupts elsewhere, when reloading SS:SP — ~L856/866). Our seam
> likewise doesn't need to guard the poke: the 0x34–0x3D vectors only ever fire
> from our own inline emulated float, which isn't running during startup.

## 2. DR C's 8087-emulator init is library-driven, not an explicit startup call

The startup's `m.init` sequence (~L474–486) calls
`m.init.stack / m.init.heap / m.init.cmd / m.init.hardware.error / m.init.os`,
and then:

```asm
; The following call is for Fortran 77 only, not for DRC.
     if 0 ;----------------------------------------------------------
        extrn   m.init.8087:    near
        call    m.init.8087     ; Initialize the 8087 or 8087 emulator.
     endif ;---------------------------------------------------------
```

So **`m.init.8087` is compiled OUT of the DR C (C-language) startup** — the
header comment at ~L444 says the same: *"Initialize the 8087 or 8087 emulator
(not for DRC)."* DR C's C float therefore brings the emulator up
**library-driven / self-registering** (the `m.init.8087` body lives in the DR C
math library `.l86`, pulled in only when float code links), rather than from an
unconditional startup call.

**Why this matters for us:** it independently validates the *shape* of the
Watcom plan. Watcom registers `__init_87_emulator`/`__fini_87_emulator` through
its `xinit`/`xfinin` init-table records (`INIT_PRIORITY_FPU`), which
`port/emu87cpm.asm` keeps verbatim — the same "the float runtime wires itself in,
the OS startup doesn't hard-code it" design. (In the current minimal milestone
build we invoke `__init_87_emulator` once explicitly before the first float, the
same way the stdio milestone calls `__InitFiles`, until/if a full `__InitRtns`
table-walking crt0 is wired.)

## 3. DR C detects an 8087 from the OS attributes, not a probe

`m.init.os` (`minitos.a86`, startup.a86 ~L1360+) obtains the OS
type/version/capabilities via BDOS, and the capability word `_os_ability`
carries an **`0200h` = "8087 is present"** bit (defined `is8087 equ 0200h`,
`em8087 equ 0100h` at ~L127–128). On the RC759 (no 8087) that bit is clear, so
the emulator path is taken.

*(Watcom instead runs its own `__x87id` probe in `emu8087.asm`; both reach the
same "use the emulator" conclusion on no-8087 hardware — a difference in
detection, not in the vector-install mechanism.)*

## 4. Emulator reentrant-data convention (context, not something we must copy)

DR C reserves, as the **first** item in its data segment:

```asm
offset.8087 dw 0    ; Offset from SS: of 8087 reentrant data area.
                    ; MUST BE FIRST IN DATA AREA FOR 8087 EMULATOR.
```

i.e. DR C's emulator keeps its scratch state stack-relative (reentrant) and
requires a fixed anchor at DGROUP:0. Watcom's `emu8087.asm` carries its own
equivalent emulator state, so we do **not** replicate `offset.8087`; this is
recorded only to explain DR C's data-layout constraint if its `.l86` is ever
disassembled.

---

## Verified vs. inferred (honesty ledger)

- **Verified from source:** the segment-0 IVT poke (§1), that `m.init.8087` is
  `if 0`-excluded from the DR C C startup (§2), the `0200h` 8087-present bit and
  its EQUs (§3), the `offset.8087` data convention (§4). All are readable in
  `startup.a86`.
- **Inferred (NOT read):** the *body* of `m.init.8087` and exactly how DR C's
  math `.l86` installs the emulator's ESC-trap vectors — those live in a binary
  library, not in the readable startup source. The claim "library-driven /
  self-registering" is inferred from the `if 0` exclusion plus the reentrant
  `offset.8087` anchor; it is consistent but has not been disassembled. It does
  not affect our route: §1 (the poke) is the part we depend on, and that is
  verified.

## Cross-links

- Watcom float design + the seam that uses this: `docs/FLOAT_8087_EMULATOR.md`.
- Durable workspace note: `tasks/memory/reference_cpm86_interrupt_vector_install.md`.
- The seam itself: `port/emu87cpm.asm` (only `xchg_vects` differs from Watcom's
  stock `initemu.asm`; assembles clean, 0× INT 21h).
