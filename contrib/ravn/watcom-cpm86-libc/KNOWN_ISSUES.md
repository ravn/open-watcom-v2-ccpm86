# Known issues, gaps and limitations

The Watcom C library retargeted to CP/M-86 (RC759 / Concurrent CP/M-86 3.1).
This is the honest list of what is **not** done, what is **verified only under
emu2** (and so still needs the MAME/RC759 oracle), and what is an **inherent
CP/M limitation** rather than a bug. Verified-and-working functionality is in
`README.md`; this file is deliberately the pessimist's view.

Status legend: **BUG** (wrong result), **GAP** (unimplemented), **LIMIT**
(inherent to the platform, not fixable in the library), **UNVERIFIED** (works
under emu2 but not yet confirmed on the authoritative MAME/RC759 oracle).

---

## Disk FILE\* seam (`port/diskio.c`)

### 1. Binary `SEEK_END` after reopen rounds up to a 128-byte record — LIMIT / UNVERIFIED

- **Within a single open handle**, `SEEK_END` (and `ftell`) is byte-exact on
  every CP/M: the true length is tracked locally in `fp->len` and extended by
  every write. Verified: `test/disktest.c` writes 200 bytes (200 % 128 = 72,
  not a record multiple) and confirms `SEEK_END` reports 200, not 256.
- **After close + reopen of a binary file**, the length can only come from the
  directory, which on a **CP/M 2.2 filesystem (plain CP/M-86)** knows length
  only to the nearest 128-byte record. A 200-byte file reopens as 256. This is
  a filesystem **LIMIT**, not a library bug — there is nowhere on disk to store
  the sub-record byte count.
- **CCP/M-86 / CP/M 3+** *does* carry an exact length via the **Last Record
  Byte Count (LRBC)**. `diskio.c` reads it (runtime-gated on BDOS fn 12 version
  >= 0x30) and would reopen byte-exact — **but** our own write path does not yet
  transmit an LRBC on close, so a binary file *we* wrote still reopens
  record-rounded even on CCP/M-86. Only files a prior tool stored with an LRBC
  come back exact. See issue #2 below.
- **UNVERIFIED:** the LRBC read/decode path is smoke-tested under emu2 (which
  reports CP/M 3.1) only. **emu2 is not authoritative for LRBC semantics — the
  RC759 running real Concurrent CP/M-86 under MAME is.** No MAME confirmation
  yet.
- Text files are unaffected: `text_eof()` recovers the byte-exact end by
  scanning the last record back past its Ctrl-Z (0x1A) padding, on any CP/M.

### 2. No write-side LRBC protocol on close — GAP

To make our *own* binary output reopen byte-exact on CCP/M-86, `__close` must
tell the OS the last-record byte count (LRBC) so it persists in the directory.
This is not implemented. Requires: (a) a CP/M 3 close-with-LRBC sequence, and
(b) a MAME/RC759 run to confirm the on-disk directory actually records it
(emu2's behaviour here is not trustworthy).

### 3. Gold-standard `clibtest` disk oracle not yet wired — GAP

Watcom ships its own self-checking regression tests
(`bld/clibtest/streamio/c/iotest.c`, `handleio/c/iotest.c`,
`file/c/filetest.c`). Running them unchanged — the way `build-owtests.sh` runs
`float01–04` — is the independent gold-standard disk oracle. Blocked on missing
seam primitives:

- `streamio`: `tmpnam` / `tmpfile`, `fscanf` read path, `fopen("CON")` (console
  as a named file).
- `handleio`: `chsize`, `dup`, `filelength`, `eof`.
- `file`: `rename`, `access`, `chmod`, `stat`, `utime`.

Until these exist the disk path is proven only by our own `test/disktest.c`
round-trip oracle (511 self-checks, PASS), not by Watcom's suite.

### 4. Currently implemented seam surface — for reference

Working (verified under emu2, purity gate INT21h=0): `fopen`/`fclose`,
`fread`/`fwrite`, `fgetc`/`fputc`/`fgets`/`fputs`/`fprintf`, `fseek`/`ftell`
(byte-granular), `remove`/`unlink`, text (Ctrl-Z) and binary modes, `O_APPEND`,
`O_TRUNC`, `O_CREAT`. Backed by CP/M random-record BDOS calls (fn 33/34) with
per-record DMA (fn 26/51).

---

## Float / 8087

### 5. `INT34-3D` purity gate false-positives on libm tables — BUG (tooling)

Tracked: **ravn/open-watcom-v2#15**. The raw-byte emulator-trap scan flags
IEEE-double coefficient bytes inside `exp`/`log` tables (e.g. a `CD 3B` byte
that is data, not an `int 3Bh` instruction). Temporarily disabled in
`build-whetstone.sh`. Fix: a code-vs-data disassembly check (like
`assert_no_8087` / `assert_no_286`) that counts only real trap *instructions*.

### 6. 8087 hardware paths deferred — GAP

Tracked: **ravn/rc7xx-work#8**. `-fpi` (trap-emulator via
`port/emu87cpm.asm`) and `-fpi87` (real chip) are deferred to a contributor
with 8087 hardware. Design + verified findings in
`docs/8087_HARDWARE_SUPPORT_DEFERRED.md` and `docs/FLOAT_8087_EMULATOR.md`.
The RC759 has no 8087, so `-fpc` soft-float is the production path.

---

## Oracle / verification

### 7. emu2 is a smoke oracle, not the authority — process note

emu2-cpm86 is fast and convenient for round-trip and purity checks, but it is
**not** cycle-accurate and does **not** faithfully model either a no-8087
machine or CP/M 3 LRBC directory semantics. The authoritative oracle is the
**RC759 (i80186 @ 6 MHz) under MAME**. Whetstone + Mandelbrot are already
MAME-verified (see README milestone notes); the disk LRBC path is not.
