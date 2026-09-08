# Known issues, gaps and limitations

The Watcom C library retargeted to CP/M-86 (RC759 / Concurrent CP/M-86 3.1).
This is the honest list of what is **not** done, what is **verified only under
emu2** (and so still needs the MAME/RC759 oracle), and what is an **inherent
CP/M limitation** rather than a bug. Verified-and-working functionality is in
`README.md`; this file is deliberately the pessimist's view.

## Console `scanf`/`getchar` (stdin) hangs in MEDIUM model (PARKED)

`port/diskio.c`'s `con_read()` implements stdin as BDOS `C_READ` (fn 1) byte
input; `scanf`/`getchar` from the console work in **small and compact** (verified
under both Unicorn and emu2, `test/coninput_test.c`, `conin` row of
`run-all-models.sh`). In **medium** the same program hangs with no output on BOTH
runners. It is NOT the console seam: a direct `_bdos(1,0)` read echoes correctly
in medium, and console *output* (fn 2) works in medium. The hang is in the
stdin-side FILE\* fill path (`getchar` → `__qread(0,...)` → `con_read`) under the
medium far-code model specifically — disk-file `fgetc` works in medium, so it is
narrowed to handle-0 (stdin). Root cause not yet found; the `conin` test SKIPs
medium. Note `sscanf` (the `scanf` row) exercises the same `%`-parser + `__cnvs2d`
float read in all three models and passes — only the console *byte source* in
medium is affected.

**Narrowed 2026-08-23: it is the NEAR-data + far-code combination, not far code.**
The LARGE model (far code + FAR data) runs `conin` AND `redir` GREEN. So the hang
is specific to medium's far-code / **near-data** layout, not far code per se —
which points at the stdin FILE\* buffer / handle-0 state living in DGROUP (near)
being reached wrongly from the far-code stdin path. Large model (same far code,
but data far) does not hit it. Good next probe: compare the `__qread(0)`/`getc`
buffer access in a medium vs large disassembly.

**Also affects command-tail redirection in medium.** `test/redirtest.c`
(`redir` row, `< > >>` I/O redirection) copies stdin→stdout, so it too hits the
medium stdin read: with `<IN.TXT` the disk-fed `redir_in` read faults (emu2
"unimplemented opcode 63" — a wild fetch), and with console stdin it hangs like
`conin`. So `redir` is verified in **small + compact** and SKIPped in medium
(same #23 root cause). The argv build and stdout (`>`) redirect DO work in medium
(OUT.TXT gets the argv line); only the stdin read is broken. **Compact `redir`
was separately broken and is now FIXED**: `crt0cm.asm` built argv[] as 2-byte
near pointers, but compact `char *` is a 4-byte FAR pointer and `main`/`
__CommonRedirect` take argv as a far pointer in CX:BX — so every `argv[i]` read as
garbage (`argc=3 [] []`), the `>`/`<` operands were unrecognised, and no
redirection happened. Fixed by emitting far argv entries (`dd`, offset+DS) and
passing argv in CX:BX.

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

> The "650 tests" figures in this document are historical: they record the
> suite size at the time each MAME run was made. Re-running that same August
> artifact on 2026-09-07 reports `686 tests, 0 failures` — the binary on disk
> has been rebuilt since those notes were written. The counts differ because
> the suite grew, not because results changed. Note also that no *rebuilt*
> binary passes on hardware today; see **ravn/open-watcom-v2-ccpm86#47**.

### 3. Gold-standard `clibtest` disk oracle — streamio LANDED

Watcom ships its own self-checking regression tests
(`bld/clibtest/streamio/c/iotest.c`, `handleio/c/iotest.c`,
`file/c/filetest.c`). Running them unchanged — the way `build-owtests.sh` runs
`float01–04` — is the independent gold-standard disk oracle.

**`streamio/c/iotest.c` now PASSES unchanged** under emu2 AND real MAME rc759,
purity INT21h=0 (`build-streamio.sh` -> "Tests completed (unzip)."). It
exercises `fopen("CON")`, `freopen` onto std streams and onto CON,
`fcloseall`/`flushall`, `dup(fileno(stdout))`, `fdopen`, `setbuf`/`setvbuf`,
`ungetc`, `perror`, the `scanf`/`vscanf`/`vfprintf`/`vprintf` family, `tmpfile`
(NUM_FILES=10 at once), byte-exact past-EOF `fgetc`, C append semantics, and
cross-handle read-after-`fflush`. Seam work that made it pass: `fopen("CON")`
console device; per-handle iomode registration (gated by `-DDISKIO_IOMODE`);
`dup`/`exit` seams; crt0 `argc`/`argv`; `tmpfile` slot count raised to 16;
byte-exact EOF via `fp->len` for a written handle; and, for the
cross-handle-read-after-`fflush` case specifically (`Test_Flushes`, the "one
'w' + one 'r' handle open simultaneously on the same file" pattern) --
**FIXED 2026-08-18** (`3f815e6c53`): a pure reader's own `BD_READRAND` is
unreliable once a still-open writer on the same file has written past what
CP/M's directory (synced only at `F_CLOSE`) reflects -- it can report
"unwritten" OR, worse, SUCCEED but return STALE data, silently clobbering the
correct copy already sitting in the shared `dma[]` cache from the writer's own
write. `load_record()` now routes a pure reader entirely through the writer's
FCB/cache in this case, never falling back to the reader's own FCB while an
open writer exists on the same file. (An earlier version of this note claimed
this already worked via "on-demand disk-length re-derivation for a pure
reader" alone -- that re-derives the correct LENGTH but does not by itself fix
the DATA read path, which is what the fix above addresses; the earlier claim
was stale/incomplete, not something that regressed.)

Still blocked (other clibtest members) on missing seam primitives:

- `handleio`: `chsize` (sparse zero-fill), `dup2` (shared file position),
  `umask`, `_hdopen`/`_os_handle`.

Implemented + emu2-verified (via `test/disktest.c`, purity gate INT21h=0):
- low-level POSIX handleio subset — `open`, `creat`, `read`, `write`, `close`,
  `lseek`, `tell`, `filelength`, `eof` (byte-exact within a single open handle;
  see #1 for the reopened-binary record-rounding limit).
- `rename` (BDOS fn 23).
- `chmod` (BDOS fn 30 F_ATTRIB) — **W-bit only** by design: CP/M-86's sole
  writability attribute is the read-only (R/O) bit (t1', bit 7 of FCB byte 9),
  so chmod maps *only* `S_IWRITE` (set → clear R/O, clear → set R/O) and ignores
  every other mode bit (read/execute + the CP/M system/archive attributes).
  Round-trip (set R/O, restore R/W, ENOENT on a missing file, data preserved)
  passes under emu2 AND on the real RC759 under MAME.
- `access` / `stat` / `utime` (the `file` clibtest group) — status probe via
  **SEARCH FIRST (BDOS fn 17, F_SFIRST)**, deliberately NOT F_OPEN. F_OPEN
  allocates a Concurrent-CP/M-86 open-file *lock*-list entry that only F_CLOSE
  releases; an open-without-close probe leaks locks and, after a handful, the
  system-wide lock list overflows and the BDOS aborts the program to the CCP
  (observed: an F_OPEN-based probe exited disktest to A> on the real RC759 while
  emu2 — which models no lock list — passed). F_SFIRST reads the 32-byte
  directory entry into the DMA with no lock and no close. From that entry:
  `access` answers W_OK via the R/O bit (EACCES) / else ENOENT; `stat` fills
  `st_size` (F_SIZE fn 35, record-rounded), `st_mode` (S_IFREG + R/O→S_IWRITE +
  `.CMD`→S_IEXEC), `st_dev=0`; `utime` is a no-op success (ENOENT on a missing
  file). **Timestamp gap:** `st_atime/st_mtime/st_ctime` are 0 for now. The RC759
  medium IS CP/M 3 (disk label `PIC 2-3.1-31`, create+update datestamps enabled)
  and carries real SFCB datestamps (verified by raw decode: 1985-03-26 12:54),
  but reading them via F_TIMEDATE (fn 102, runtime-gated off on emu2 which lacks
  it) is a tracked enhancement, not yet implemented. Passes under emu2 (686) AND
  on the real RC759 under MAME (686/0).
- `tmpnam` / `tmpfile` — `"TMPnnnnn.$$$"` names, uniqueness by open()-probe,
  auto-removal on `fclose` via Watcom's own `_TMPFIL` / `__RmTmpFileFn` hook.
- `fscanf` — Watcom's UNCHANGED `streamio/c/scnf.c` scan engine, proven by the
  dedicated `build-fscanf.sh` harness (`test/disktest.c -DFSCANF_TEST`, PASS 672
  self-checks, INT21h=0). `scnf.c` compiles `scan_float()` unconditionally, so
  fscanf drags the soft-float + ctype + mbyte stack (FIDRQQ/FIERQQ/FIWRQQ,
  `__Bits`/`isdigit`/`isspace`, `mbtowc`, strtod); those are resolved 8087-free
  by reusing `build-whetstone.sh`'s `-fpc` `__FDxemu` objects + LIB-searched
  `msdos.086` clib + `msdos.286` mathlib. Kept OUT of `build-diskio.sh` so the
  disk-I/O purity oracle stays float-free.

The disk path is proven by `build-streamio.sh` (Watcom's UNCHANGED iotest.c),
`build-diskio.sh` (661 round-trip self-checks), and `build-fscanf.sh` (672), all
PASS, all INT21h=0.

### 3b. Seeking outside the file — gap content is UNDEFINED (tested/characterized)

Raised in **ravn/open-watcom-v2-ccpm86#46**. What happens when you seek past
the end is now pinned by `test/disktest.c` (the "seeking OUTSIDE the file"
block), but only for the parts that are actually defined:

- **Seek past end succeeds** and reports the requested offset — the position is
  a plain number, never clamped to the length. Seeking *before* the start is an
  error (`-1`), not a clamp to 0.
- **A seek alone never extends the file**; `filelength()` is unchanged until a
  write happens.
- **Reads at or after the end always return 0 bytes.** `__qread` clamps against
  the exact tracked length, so neither the last record's Ctrl-Z padding nor the
  post-LRBC tail can leak out as data. This is the strict answer to #46: unlike
  several other CP/M runtimes, you cannot read past EOF into the record tail
  here. Asserted at three positions — far beyond the last record, exactly on
  the end, and inside the final record's padding.
- **Writing at a far offset extends the length over the gap** and the written
  bytes read back intact.

**Undefined, and deliberately NOT asserted: the content of the gap.** It is not
a UNIX sparse hole. Measured under emu2 (10-byte file, then `write("XY")` at
offset 500) the gap is inconsistent *within a single file*: records touched by
the seam's read-modify-write come back `0x1A` (the Ctrl-Z fill `load_record()`
puts into a fresh record), while records CP/M never allocated come back `0x00`.
On-disk verification of that same file: bytes 10..127 are `1a`, bytes 128..383
are `00`. On real hardware an allocated-but-unwritten block may instead hold
whatever was there previously. Gap content is therefore emulator- and
filesystem-dependent; a test pinning it would be pinning an accident.

Verified under emu2 against the first-class clib (`bld/clib/_cpm/c/diskio.c`):
`DISKIO: PASS (719 tests, 0 failures)`, up from a 686-test baseline — i.e. all
33 new checks execute, none are skipped. The assertions were mutation-checked
(flipping the expected read count makes the run report `***FAIL***`), so they
are observably falsifiable rather than vacuous.

**Run against `port/diskio.c` too** (the copy `build-diskio.sh` compiles): the
earlier `wasm` segfault was a bad `OUTDIR`, not a toolchain fault, so that path
builds fine. Result under emu2: `DISKIO: PASS (719 tests, 0 failures)`, matching
the clib copy exactly. The functions the test exercises — `__lseek`, the
`__qread` end-of-file clamp, `filelength`, `eof`, and the write-side length
extension — are byte-identical between the two copies.

**On real MAME hardware these 33 checks are still unverified, and not because
of anything in this test.** Any `disktest.cmd` built today fails on the RC759 in
`chmod`/`access`/`stat`/`utime` long before reaching them — see
**ravn/open-watcom-v2-ccpm86#47**. That failure was proven pre-existing: the
*baseline* test (without this block) fails identically, `54 tests / 41
failures`, and so does the Aug-15 `diskio.c` source rebuilt through the same
path. Both `port/` and the first-class clib copy are affected. The only binary
that passes `686 / 0` on hardware is the August artifact built with the native
`osxa64` toolchain, which is no longer present in the tree.

### 3c. ~~BSS is never zeroed — statics start dirty on real hardware~~ FIXED

**ravn/open-watcom-v2-ccpm86#47. RESOLVED** — `port/crt0{sm,cm,mm,lm,cpp}.asm`
now zero-fill BSS before `wc_heap_init_` using wlink's own `_edata`/`_end`
bounds (emitted by `GetBSSSize()` under `option dosseg`). Verified under
`emu2 -P 255`: `bss_assert` → PASS (0 failures), `bssprobe` → 0 of 16384
bytes non-zero. See `BSS_FIX_PLAN.md` for full analysis.

**Historical record (pre-fix):** A `.CMD` data group descriptor carries both
G-Length (paragraphs of initialised data actually in the file) and G-Min
(paragraphs the loader must allocate); the difference is BSS. Nothing cleared
it — `port/crt0sm.asm` had no zero-fill loop — so on the RC759 every C
`static`/global without an initialiser started as garbage, violating the C
guarantee for static storage duration.

Pre-fix measurement with `test/bssprobe.c`:

| host | result |
| --- | --- |
| emu2 | `0 of 16384 bytes non-zero` — ZEROED (emulator hid bug) |
| real RC759 (MAME) | `16128 of 16384 bytes non-zero` — **DIRTY** |
| emu2 -P 255 (post-fix) | `0 of 16384 bytes non-zero` — ZEROED |

The August artifact that passed `686 / 0` on hardware was immune only by
accident: its toolchain wrote whole BSS as literal zeros in the file image
(G-Length == G-Min). Scope was not confined to the disk seam — any CP/M-86
program relying on zero-initialised statics was affected.

### 4. Currently implemented seam surface — for reference

Working (verified under emu2, purity gate INT21h=0): `fopen`/`fclose`,
`fread`/`fwrite`, `fgetc`/`fputc`/`fgets`/`fputs`/`fprintf`, `fseek`/`ftell`
(byte-granular), `remove`/`unlink`/`rename`, `chmod` (W-bit only -> R/O
attribute), `access`/`stat`/`utime` (status via SEARCH FIRST fn 17; timestamps
0 pending fn 102), low-level POSIX
`open`/`creat`/`read`/`write`/`close`/`lseek`/`tell`/`filelength`/`eof`,
`tmpnam`/`tmpfile` (auto-removed on `fclose`), `fscanf` (Watcom's unchanged
scan engine, via the float-coupled `build-fscanf.sh` harness), text
(Ctrl-Z) and binary modes, `O_APPEND`, `O_TRUNC`, `O_CREAT`. Backed by CP/M
random-record BDOS calls (fn 33/34) with per-record DMA (fn 26/51).

---

## C++ layer (`build-cpp.sh`, `port/crt0cpp.asm`, `port/cpprt.c`, `port/ehsupp.c`)

### C1. emu2 crashes on the C++ runtime's FF-form opcodes — process note

The C++ iostream/EH runtime uses a few `FF`-group encodings that `emu2`
(`scratch/cpm86-tools/emu2-cpm86/emu2`) does not implement, so a C++ `.CMD`
can HALT / hit "unimplemented opcode" on emu2 even when it is correct. This is
an emu2 gap, not a port bug: the same binaries pass on the full-8086 **MAME
rc759** oracle (`cppfeat` 8/0, `mame_cpptest` 6/0). Treat MAME as authoritative
for the C++ layer; emu2 is at best a partial smoke test here. (The original
global-ctor crash also first surfaced on emu2 — see C2.)

### C2. C++ global constructors need `crt0cpp.asm`, not the minimal C crt0 — RESOLVED

Predefined `cout`/`cin`/`cerr` (and any user global object) are constructed via
Watcom's `XI` (ctor) / `YI` (dtor) startup records. The minimal C `port/crt0sm.asm`
does not walk them, so a C++ build linked with it constructs nothing and the
first `std::cout <<` dereferences an uninitialised streambuf (HALT / opcode-`FF`
crash before any output). Fixed by `port/crt0cpp.asm`, which brackets
`XIB/XI/XIE`+`YIB/YI/YIE` into `DGROUP` and runs `__init_rtns` before `main` /
`__fini_rtns` after. Empty tables are a no-op, so it is safe for pure C — but
the seven C targets deliberately keep the leaner `crt0sm.asm`; only C++ builds
use `crt0cpp.asm`.

### C3. `__longjmp_handler` must be NEAR — LIMIT (carried from design)

`port/ehsupp.c`'s `__longjmp_handler` is a **near** routine. A far version would
`retf` against `longjmp`'s near `call` and unbalance the stack — and because
C++ EH overwrites the handler pointer at throw time, a far handler breaks C
`setjmp`/`longjmp` while C++ EH still appears to work (a confusing split
failure). Keep it near.

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

---

## Known non-blocking open items (2026-08-15)

The following are **known, non-blocking** gaps/enhancements. The core RC759
production libc surface is implemented and MAME-verified; none of these block it.
Recorded here so they are not lost; no action taken now.

### 8. Enhancements / deferred (not blocking)

- **stat timestamps via F_TIMEDATE (fn 102)** — Tracked: **ravn/open-watcom-v2-ccpm86#31**. `stat()` leaves
  st_atime/mtime/ctime = 0. The RC759 medium IS CP/M 3 (label `PIC 2-3.1-31`,
  create+update datestamps enabled; real SFCBs decode to 1985-03-26 12:54) so
  the data exists; reading it needs fn 102, runtime-gated off on emu2 (no fn
  102, max BDOS case 105). The version discriminator (real RC759 fn 12 return
  vs emu2 0x0031) is UNVERIFIED — must be MAME-probed empirically, not guessed.
  Independent assertion oracle = raw SFCB byte-decode.
- **fscanf float decouple** — Tracked: **ravn/open-watcom-v2-ccpm86#32**. Watcom's `scnf.c` compiles `scan_float()`
  unconditionally, so integer-only `fscanf` still drags the soft-float stack.
  Make float scanning link only when a floating conversion (%e/%f/%g) is used.
  Non-blocker (user-confirmed).
- **cpmtools diskdef os 2.2 vs os 3** — `scratch/rc759-cmd-toolchain/diskdefs`
  (`rc759-drc`) declares `os 2.2`, which under-describes the CP/M-3 medium.
  Kept deliberately: os-2.2 is datestamp-blind, so `cpmcp` preserves the
  authentic 1985 SFCB stamps instead of rewriting them; and `os 3` did NOT
  surface stamps in `cpmls -D` anyway. "Check later", not a bug.
- **8087 software-emulator link path** (`-fpi`) — compile+link the
  fpuemu/i86 emu8087 engine into the cpm86 image, plus a CP/M-86 vector-init
  seam that pokes INT 0x34–0x3D emulator vectors directly in the IVT (no DOS
  INT 21h `xchg_vects`). Deferred to a contributor with 8087 hardware; RC759
  has no 8087, so `-fpc` soft-float is the production path
  (see #6 / `docs/8087_HARDWARE_SUPPORT_DEFERRED.md`).
- **DDT86/SID86 debug symbols in the .CMD** — investigate how the DRI
  debuggers DDT86 and SID86 receive symbol/debug information (symbol table
  format), and whether Open Watcom `wlink` can embed it directly in the `.CMD`
  file. Today the cpm86 writer appends debug info AFTER the group images (the
  CMD loader ignores trailing bytes), but DDT86/SID86 consumption is
  unverified. "Check later", enhancement.
- **CP/M-86 8080 memory model** — `wlink FORMAT CPM86 8080` is now a fatal
  error (E3058 "8080 option not valid for a CP/M-86 executable"); only the
  MAME-validated small model is accepted. A genuine 8080 image needs a SINGLE
  combined type-1 group so the loader sets CS=DS=SS=ES (base-page 8080 flag
  `0x01`) — i.e. a single-segment / tiny link layout in wlink plus a new
  `crt08080.asm`; a small-model link cannot simply be concatenated (data
  offsets would point wrong). Once implemented, build + validate on real RC759
  under MAME, then re-enable the sub-option and document it. Non-blocker.

### 9. ibm5150 CP/M-86 1.0 fallback verification — PARKED (memory-capped)

Tracked: **ravn/open-watcom-v2#17**. Goal was to verify the
`os_has_lrbc()==false` fallback branch (record-rounded reopen / SEEK_END → 200
not 256) on a real, pre-CP/M-3 target — IBM PC CP/M-86 1.0 under the ibm5150
MAME driver — as a second-platform cross-check of the LRBC version gate. PARKED
2026-08-15: **CP/M-86 1.0 self-caps the TPA at 128 KB**, so `disktest.cmd`
reports `MEMORY NOT AVAILABLE` and cannot run. Sub-items (writable 160K IBM
diskdef, harness port + mame_out/Lua done-reader, keystroke autorun, result
capture) are all blocked on this. Not blocking RC759 production — the LRBC path
itself is MAME-verified on the real RC759; only the *false*-branch cross-check on
a second OS is outstanding.
