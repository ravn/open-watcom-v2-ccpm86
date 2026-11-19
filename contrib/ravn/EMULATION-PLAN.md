# CP/M-86 Emulation Plan

A staged plan for turning the current proof-of-concept runners into an
emulator capable of executing real CP/M-86 `.CMD` programs. All section and
table references are to the *CP/M-86 System Guide* (Jun 1983),
`CPM-86_System_Guide_Jun83.txt`, in this folder.

## Scope

Instruction-level execution of a CP/M-86 **transient program** in a faithful
CP/M-86 *environment*: correct program load (memory model, segment registers,
base page), the BDOS system-call interface (INT 224 / `0E0h`), and enough of
the BIOS to support console + file I/O. We are **not** emulating real hardware,
disk controllers, the CCP shell, or MP/M multitasking.

The CPU itself is provided by Unicorn (QEMU core) in `cpm86run_unicorn.py`;
this plan is about the CP/M-86 **operating-environment** layer on top of it.
`cpm86run.py` (our own decoder) stays as a lightweight secondary check.

## Key facts extracted from the System Guide

### Memory models and program entry (Section 2, Section 3.4)

The CCP selects the memory model from the group descriptors in the `.CMD`
header (Table 3-2 / Table 2-1):

| Model   | Groups present                     | CS / DS / ES / SS + SP        | Entry IP |
|---------|------------------------------------|-------------------------------|----------|
| 8080    | code only                          | CS=DS=ES=code base; SS:SP in CCP | **0100H** |
| Small   | code + data                        | CS=code; DS=ES=data; SS:SP in CCP | 0000H |
| Compact | code + data + extra/stack/aux      | each seg reg to its group base   | 0000H |

**Correction to earlier assumption.** The 8080 model does **not** start at
`CS:0000` — Section 2.3 states IP is set to **0100H**, with the base page
occupying the first 100H bytes of the (shared) code group, exactly like a
CP/M-80 `.COM`. The Small model starts at `CS:0000` with the base page at
`DS:0000`. Our current `HELLO.CMD` is an 8080-model file whose code sits at
offset 0, so a *spec-faithful* loader would jump to `CS:0100H` and execute the
base page as code. This must be fixed (see Phase 0).

- SS:SP are **not** set to a program stack; they point at a 96-byte stack in
  the CCP. A program that needs its own stack must set SS:SP itself.
- Control is entered via a **Far Call**; a Far Return (or BDOS function 0)
  terminates.

### Base page (Section 2.6, Figure 2-4), all relative to DS

| Offset | Field | Meaning |
|--------|-------|---------|
| 0000H  | LC0..LC2 | last code group location (24-bit) |
| 0003H  | BC0..BC1 | code group base paragraph |
| 0006H  | LD / BD  | data group last position + base |
| ...    | LE/BE, LS/BS, LX/BX | extra, stack, aux group descriptors |
| 0005H  | M80      | =1 when 8080 model in use |
| 005CH  | FCB 1    | default FCB parsed from command tail |
| 006CH  | FCB 2    | second parsed filename |
| 0080H  | DMA buf  | default 128-byte DMA buffer / command tail |
| 0100H  | —        | begin user data |

DMA is split into **DMA base (segment)** + **DMA offset**; defaults are DS and
0080H (Section 2.7). Functions 26/51/52 manage it.

### BDOS interface (Section 4)

- Entry: software interrupt **224** (`INT 0E0h`). `CL`=function, `DL`=byte
  arg, `DX`=word arg, `DS`=data segment for buffer/FCB addresses (Table 4-1).
- Returns: byte in `AL`; word in **both** `AX` and `BX`; double-word as offset
  in `BX`, segment in `ES`. All segment registers except `ES` are preserved.
- Full function set (Table 4-2): 0–40 plus CP/M-86 extensions 50–59.

Grouping for implementation:

1. **Simple / console (0–12)** — *done* in `cpm86run_unicorn.py`
   (0,1,2,5,6,9,10,11); still missing 3 (reader), 4 (punch), 7/8 (I/O byte),
   12 (version — should return 0x0000 CP/M, system-type byte).
2. **File system (13–40)** — select disk, open/close/make/delete/rename,
   search first/next, sequential + random read/write, set DMA, compute size,
   set/get user code, drive reset. Needs FCB handling + a host-backed disk.
3. **Memory + load (50–59)** — direct BIOS call (50), set/get DMA base
   (51/52), max/region memory (53–58), program load (59). Mostly stubs for a
   single-program environment; 50 must dispatch to the BIOS layer.

### BIOS interface (Section 5)

- 21-entry jump vector (Table 5-1) at offset 2500H from the OS base: INIT,
  WBOOT, CONST, CONIN, CONOUT, LIST, PUNCH, READER, HOME, SELDSK, SETTRK,
  SETSEC, SETDMA, READ, WRITE, LISTST, SECTRAN, SETDMAB, GETSEGB, GETIOB,
  SETIOB. Params in CX (first) / DX (second); byte return in AL, word in BX.
- Reached directly via BDOS function 50 (Direct BIOS Call) with a 5-byte
  descriptor `Func, value(CX), value(DX)`.
- For our scope, the character BIOS calls map onto the same host console the
  BDOS console group already uses; the disk BIOS calls (SELDSK/SETTRK/SETSEC/
  READ/WRITE + disk parameter block, Section 6) are only needed if we emulate
  at the sector level rather than the FCB/BDOS level.

### FCB and disk (Section 4.3, Section 6)

- 33/36-byte FCB; directory entries 32 bytes (Table 6-5); disk geometry via a
  Disk Parameter Block (Table 6-2). Random record = 3-byte field.
- **Design fork for file I/O** (see Phase 2): either (a) map BDOS file calls
  onto a **host directory** (simple, no real CP/M disk layout — recommended
  first), or (b) emulate a **CP/M disk image** at the BIOS sector level with a
  real DPB/allocation/directory (faithful, needed only for programs that do
  raw BIOS disk access or depend on allocation details).

### RC759 XIOS interface (Int 28h) — PICCOLINE Programmer's Guide, App. A

Beyond the standard CP/M-86 BDOS/BIOS, the RC759 Piccoline exposes its machine
specific XIOS (extended I/O system) through **software interrupt `INT 28h`**,
with the function selector in **`AL`**. This is *not* part of portable CP/M-86 —
it is RC759 hardware access (graphics, floppy, cassette, sound, mouse, real-time
tick, …). Two neighbouring vectors exist for completeness: `INT 29h` = net
driver, `INT 30h` = IMC (App. C, Interrupt Vector Assignment).

Our emulator implements **only function 19** (the 16 ms relative-time counter,
the finest program-visible clock — the 80186's own timers drive sound/cassette,
not timekeeping, per §2.3). Every other `INT 28h` function fails loud
(`BdosUnimplemented`, kind `RC759 XIOS (Int 28h)`), so a program that reaches
for unmodelled RC759 hardware stops with a clear message instead of misbehaving.

Full `AL` function map (App. A). †19 is implemented; the rest are documented
here for reference and currently fail loud:

| AL | Function |
|----|----------|
| 0 | Change console to graphics mode (`AH`=1 hi-/2 medium-res; `DX:CX`→graphics control block) |
| 1 | Change console to character mode |
| 2 | Reserved |
| 3 | Return address of a copy of the nonvolatile-memory contents (`ES:SI`) |
| 4 | Return address of a configuration description (`ES:SI`) |
| 5 | Recalibrate floppy drive (stack: drive/head/cyl/bytecount/DMA seg:off; `AL`=FDC status) |
| 6–7 | Reserved |
| 8 | Step floppy head one track in (stack as fn 5; `AL`=FDC status) |
| 9 | Step floppy head one track out |
| 10 | Write a track to floppy |
| 11 | Read a track from floppy |
| 12 | Write a byte to the sound generator (`DL`=byte) |
| 13 | Get address of disk-driver statistics (`ES`=segment; read/write/error counters) |
| 14–18 | Reserved |
| **19†** | **Return 16 ms counter** — `AL`=19 in; `DX:AX`=seconds since boot, `CX`=elapsed 16 ms periods of the current second (16 ms resolution, relative only) |
| 20 | Define a character in the alternative character set (`CL`=char 0-255; `DS:DX`→definition) |
| 21 | Return pointer to a console display list (Intel 82730) |
| 22 | Return current cursor position (`BH`=row, `BL`=column) |
| 23 | Return status of iSBX351 controller (if installed) |
| 24 | Initialise iSBX351 controller (stack: parameter block seg:off) |
| 25 | Reserved |
| 26 | Read file-header record from cassette tape (`CX`=max bytes, `DX`=buf off, stack: buf seg; `AL`=result) |
| 27 | Write file header on cassette tape |
| 28 | Read next data record from cassette tape |
| 29 | Write next data record on cassette tape |
| 30 | Mouse: `CL`=1 init / 2 deinit / 3 status (status: `AL`=0 idle/1 button+`AH`/2 moved+`BX,CX` delta) |
| 31 | Define palette contents (`DS:DX`→palette definition) |
| 32–34 | Reserved |
| 35 | Write a string direct to the console buffer (`DL`=col, `DH`=row, `CX`=count, `DS:SI`→string) |
| 36 | Set cursor position (`BH`=row, `BL`=column) |
| 37 | Return current attributes (`AH`) |
| 38 | Set attributes (`AH`) |
| 39 | Update physical screen |
| 40 | Write an end-of-file record on cassette tape |
| 41 | DPC parallel interface: `AH`=1 reserve / 2 release |
| 42 | Shared disk: `AH`=1 reserve / 2 release |
| 43–49 | Reserved |
| 50 | Reset iSBX351 |
| 51 | Get font — return a character from the set (`CX`=char 0-1023; `DS:DX`→block) |
| 52 | Define font — define a character in the set (`CX`=char 0-1023; `DS:DX`→block) |
| 53 | Get XIOS version (`AH`=year, `AL`=version, `BH`=month, `BL`=day, all BCD) |

Note: the Guide's fn-19 text says "periods of *next* second" — a typo for the
*current* second (which is what the register layout and our model use).

## Staged plan

### Phase 0 — Faithful program load (correctness foundation) — **DONE**
- Parse **all** group descriptors from the 128-byte header, classify the
  memory model (8080 / Small / Compact) per Table 2-1, and lay each group into
  its own paragraph-aligned segment. *(8080 + Small implemented in
  `cpm86run_unicorn.py._load`; Compact treated as Small for now.)*
- Initialise segment registers and **entry IP per model** (8080→0100H,
  Small→0000H). Set SS:SP to an emulated 96-byte CCP stack with a far-return
  stub that terminates via BDOS 0. *(done)*
- Build a correct **base page** (group length/base fields, M80 byte at 05H, DMA
  default, parsed FCBs at 005CH/006CH, command tail at 0080H) — implemented in
  `ccp.py` and populated at load time from the command line. *(done)*
- **Build side reconciled:** the 100H-byte base page is reserved by the loader
  (`bin2cmd.py` did this before it was retired 2026-08-19 in favour of native
  `wl format cpm86`); `hello.asm`/`echoarg.asm` assemble at `org 100h`;
  `HELLO.CMD`/`ECHOARG.CMD` regenerated and verified.
- README corrected: the earlier "entry at CS:0000 for all models" note was
  wrong; only Small/Compact enter at 0000H.

### Phase 1 — Complete the console/character BDOS + BIOS char group
- Finish BDOS 3,4,7,8,12 and make 1/2/6/9/10/11 spec-exact (e.g. fn 6 special
  DL values 0FEH/0FDH, fn 12 version bytes, fn 10 editing keys Table 4-3).
- Implement the BIOS character jump-vector entries (CONST/CONIN/CONOUT/LIST/
  LISTST/PUNCH/READER) and wire BDOS fn 50 (Direct BIOS Call) to them.
- Return values exactly per Table 4-1 (AL, and AX==BX for words).

### Phase 2 — File system BDOS (host-directory backend)
- Implement an FCB layer and BDOS 13–26,30,32–37,40 over a host directory
  (8.3 name mapping, sequential + random records, set-DMA base/offset,
  login/current-disk/user-code). No real CP/M allocation needed.
- Add a small test corpus (create/write/close/reopen/read-back, random
  seek) driven through the emulator; assert host-file contents.

### Phase 3 — Memory-management BDOS + multi-segment programs
- Implement 51/52 (DMA base) fully and 53–59 as a simple linear allocator
  over the emulated 1 MB, so Small/Compact programs that request memory or
  use `Program load` behave.
- Exercise a Small-model and a Compact-model test program end-to-end.

### Phase 4 (optional) — Sector-level disk + BIOS DPB
- Only if a target program bypasses BDOS and does raw BIOS disk I/O: back a
  `.dsk` image with a real Disk Parameter Block (Section 6), SELDSK/SETTRK/
  SETSEC/READ/WRITE/SECTRAN, directory + allocation vector.

### Phase 5 — Harness, regression, and OW integration
- Golden-output regression tests for every sample `.CMD`; run both
  `cpm86run.py` and `cpm86run_unicorn.py` and diff their output.
- Once the OW toolchain is built, link real compiler output natively
  (`owcc -bcpm86` / `wl format cpm86`) and feed it through the emulator.
- Optionally cross-check against a full-machine emulator (86Box / PCem) for a
  handful of programs to validate loader fidelity.

## Open decisions (need a call before Phase 0/2)

1. **8080 vs Small model for the samples.** Recommend switching the samples to
   the **Small model** (entry 0000H, clean code/data split) — it matches how a
   C compiler emits code and avoids the base-page-as-code hazard. Alternative:
   stay 8080 but move code to `org 100h`.
2. **File I/O backend.** Recommend **host-directory mapping** (Phase 2) first;
   defer sector-level disk images (Phase 4) until a program actually needs it.

## Immediate next actions
1. Implement Phase 0 loader + model detection in `cpm86run_unicorn.py`.
2. Emit `.CMD` natively with `wl format cpm86`, regenerate `HELLO.CMD`.
3. Correct the entry-point description in `README-cpm86.md`.
