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
  **Hardware-verified 2026-08-15 on the RC759 under MAME (Concurrent CP/M-86
  3.1): `DISKIO: PASS (650 tests, 0 failures)` on the real machine** — see
  README milestone + `mame-tests/disk-mame.sh`.
- **After close + reopen of a binary file**, the length can only come from the
  directory, which on a **CP/M 2.2 filesystem (plain CP/M-86)** knows length
  only to the nearest 128-byte record. A 200-byte file reopens as 256. This is
  a filesystem **LIMIT**, not a library bug — there is nowhere on disk to store
  the sub-record byte count.
- **CCP/M-86 / CP/M 3+** *does* carry an exact length via the **Last Record
  Byte Count (LRBC)**. `diskio.c` reads it (runtime-gated on BDOS fn 12 version
  >= 0x30 — the RC759 reports 3.1, so this path IS live on the real target) and
  reopens byte-exact. Our own write path now transmits the LRBC on close via the
  CP/M 3 F_ATTRIB/F6' byte-count protocol (see issue #2, now CLOSED), so a binary
  file *we* wrote reopens byte-exact on CCP/M-86 — **hardware-verified 2026-08-15
  on the RC759 under MAME**: `test/disktest.c` writes a 100-byte file, closes it,
  reopens cold and asserts `SEEK_END == 100` (not 128); `DISKIO: PASS (650 tests,
  0 failures)` on the real machine.
- **Still UNVERIFIED:** the LRBC *decode-to-exact-length* value on a file a
  third-party tool stored with an LRBC. The version gate and code path run on
  the real RC759 (the 650-check suite passed there), but no test yet reopens a
  foreign LRBC-tagged binary file and asserts the decoded length, so the decode
  arithmetic itself is confirmed only under emu2. Closing this needs a fixture
  file carrying a known partial last record.
- **The `os_has_lrbc()==false` fallback itself** (record-rounded reopen on a
  genuinely pre-CP/M-3 target) is tracked + PARKED in **ravn/open-watcom-v2#17**.
  An IBM 5150 + CP/M-86 1.0 MAME oracle was built to exercise it, since the RC759
  (Concurrent CP/M-86 3.1) always takes the LRBC path and cannot reach this
  branch. The harness boots and injects keystrokes reliably, but `disktest.cmd`
  hits `MEMORY NOT AVAILABLE` — CP/M-86 1.0 self-caps its TPA at 128 KB
  regardless of installed RAM. Full state + next steps in #17.
- Text files are unaffected: `text_eof()` recovers the byte-exact end by
  scanning the last record back past its Ctrl-Z (0x1A) padding, on any CP/M.

### 2. Write-side LRBC protocol on close — DONE (MAME-verified 2026-08-15)

To make our *own* binary output reopen byte-exact on CCP/M-86, `__close` tells
the OS the last-record byte count (LRBC) so it persists in the directory. This
is now implemented: after `_bdos(BD_CLOSE,…)`, for a BINARY file we actually
wrote whose length is not a 128-multiple, `__close` re-issues **F_ATTRIB (BDOS
fn 30)** with the **F6' request flag** (bit 7 of FCB byte 6) set and the byte
count (`len & 0x7F`) in **FCB+32**; on CP/M 3+ the OS records that count in the
directory. Runtime-gated on `os_has_lrbc()` (BDOS version ≥ 0x30) so plain
CP/M-86 2.2 is untouched, and skipped for text files (they use Ctrl-Z EOF).

**Why F_ATTRIB and not FCB+32-at-F_CLOSE:** the close path treats FCB+32 as the
sequential current-record byte, so it will not honour a byte count there for a
handle that has written (confirmed against emu2's close handler, which
deliberately refuses to truncate a written handle — FCB+32 is CR-contaminated).
The documented CP/M 3 / DOS-Plus protocol is the post-close F_ATTRIB/F6' call,
which resolves the closed file by name.

**Verified:** `test/disktest.c` (gated on `os_reports_lrbc()`) writes 100 bytes,
closes, reopens cold, asserts `SEEK_END == 100`. PASS under emu2 and — the
authority — **PASS on the real RC759 under MAME (Concurrent CP/M-86 3.1):
`DISKIO: PASS (650 tests, 0 failures)`** via `mame-tests/disk-mame.sh`.

### 3. Gold-standard `clibtest` disk oracle — streamio LANDED

Watcom ships its own self-checking regression tests
(`bld/clibtest/streamio/c/iotest.c`, `handleio/c/iotest.c`,
`file/c/filetest.c`). Running them unchanged — the way `build-owtests.sh` runs
`float01–04` — is the independent gold-standard disk oracle.

**`streamio/c/iotest.c` now PASSES unchanged** under emu2, purity INT21h=0
(`build-streamio.sh` -> "Tests completed (iotest)."). It exercises
`fopen("CON")`, `freopen` onto std streams and onto CON, `fcloseall`/`flushall`,
`dup(fileno(stdout))`, `fdopen`, `setbuf`/`setvbuf`, `ungetc`, `perror`, the
`scanf`/`vscanf`/`vfprintf`/`vprintf` family, `tmpfile` (NUM_FILES=10 at once),
byte-exact past-EOF `fgetc`, C append semantics, and cross-handle read-after-
`fflush`. Seam work that made it pass: `fopen("CON")` console device; per-handle
iomode registration (gated by `-DDISKIO_IOMODE`); `dup`/`exit` seams; crt0
`argc`/`argv`; `tmpfile` slot count raised to 16; byte-exact EOF via `fp->len`
for a written handle plus on-demand disk-length re-derivation for a pure reader;
and an emu2 fidelity fix (flush host handle after a random write so a second FCB
/ `stat()` sees it, matching real CP/M-86 write-through).

Still blocked (other clibtest members) on missing seam primitives:

- `handleio`: `chsize` (sparse zero-fill), `dup2` (shared file position),
  `umask`/`chmod` R/O-attribute enforcement, `_hdopen`/`_os_handle`.
- `file`: `access`, `chmod`, `stat`, `utime`.

Implemented + emu2-verified (via `test/disktest.c`, purity gate INT21h=0):
- low-level POSIX handleio subset — `open`, `creat`, `read`, `write`, `close`,
  `lseek`, `tell`, `filelength`, `eof` (byte-exact within a single open handle;
  see #1 for the reopened-binary record-rounding limit).
- `rename` (BDOS fn 23).
- `tmpnam` / `tmpfile` — `"TMPnnnnn.$$$"` names, uniqueness by open()-probe,
  auto-removal on `fclose` via Watcom's own `_TMPFIL` / `__RmTmpFileFn` hook.
- `fscanf` — Watcom's UNCHANGED `streamio/c/scnf.c` scan engine, proven by the
  dedicated `build-fscanf.sh` harness (`test/disktest.c -DFSCANF_TEST`, PASS 661
  self-checks, INT21h=0). `scnf.c` compiles `scan_float()` unconditionally, so
  fscanf drags the soft-float + ctype + mbyte stack (FIDRQQ/FIERQQ/FIWRQQ,
  `__Bits`/`isdigit`/`isspace`, `mbtowc`, strtod); those are resolved 8087-free
  by reusing `build-whetstone.sh`'s `-fpc` `__FDxemu` objects + LIB-searched
  `msdos.086` clib + `msdos.286` mathlib. Kept OUT of `build-diskio.sh` so the
  disk-I/O purity oracle stays float-free.

The disk path is proven by `build-streamio.sh` (Watcom's UNCHANGED iotest.c),
`build-diskio.sh` (650 round-trip self-checks), and `build-fscanf.sh` (661), all
PASS, all INT21h=0.

### 4. Currently implemented seam surface — for reference

Working (verified under emu2, purity gate INT21h=0): `fopen`/`fclose`,
`fread`/`fwrite`, `fgetc`/`fputc`/`fgets`/`fputs`/`fprintf`, `fseek`/`ftell`
(byte-granular), `remove`/`unlink`/`rename`, low-level POSIX
`open`/`creat`/`read`/`write`/`close`/`lseek`/`tell`/`filelength`/`eof`,
`tmpnam`/`tmpfile` (auto-removed on `fclose`), `fscanf` (Watcom's unchanged
scan engine, via the float-coupled `build-fscanf.sh` harness), text
(Ctrl-Z) and binary modes, `O_APPEND`, `O_TRUNC`, `O_CREAT`. Backed by CP/M
random-record BDOS calls (fn 33/34) with per-record DMA (fn 26/51).

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
**RC759 (i80186 @ 6 MHz) under MAME**. Whetstone + Mandelbrot were already
MAME-verified; the disk FILE\* seam is now **MAME-verified too** — the full
650-check `disktest.c` suite passes on the real RC759 running **Concurrent
CP/M-86 3.1** (`mame-tests/disk-mame.sh`, snapshot shows `DISKIO: PASS`). The
one disk item still emu2-only is the foreign-LRBC decode value (issue #1).
