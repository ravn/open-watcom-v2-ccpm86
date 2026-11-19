# Porting Open Watcom's C library to CP/M-86 (RC759) — the process

This is the narrative of how Open Watcom's *own* C library was retargeted to
run on 16-bit CP/M-86 (Intel 8086/80186), verified on a real Regnecentralen
**RC759 "Piccoline"** running **Concurrent CP/M-86 3.1** under MAME.

For the technical *how* see [`README.md`](README.md); for remaining gaps see
[`KNOWN_ISSUES.md`](KNOWN_ISSUES.md). This file is the *why* and the *journey*.

## The goal

**Watcom's C toolchain — compiler *and* its own clib — fully working on the
RC759 in MAME**, with no proprietary runtime and no second toolchain. The
authoritative success criterion was a self-checking test suite passing on the
real RC759 hardware model, not just an emulator.

## The pivot — three routes, one winner

Two earlier routes to a CP/M-86 libc were tried and rejected:

- **(a) Bridge to DR Digital Research C** (`owc-drc/`). Works, but the runtime
  is proprietary/non-free and needs a float-ABI shim between Watcom code and
  DR's integer/long helpers — a permanent seam and a licensing dead end.
- **(b) Recompile Aztec C's stdlib.** ABI mismatch against Watcom's `long`/
  `double`, Manx-syntax assembler for `str*`/`mem*`, and — verified from the
  Aztec source — a CP/M-2.2-era file model with no `stat`/timestamps. Also a
  dead end for a clean, self-owned libc.

**The unlocking insight:** Watcom's clib is explicitly *layered and
retargetable*. The high-level formatter / `stdio` / math code is OS-agnostic;
only a handful of low-level primitives (`read`/`write`/`open`/`close`/`lseek`/
`sbrk`) carry the OS trap. On 16-bit DOS those live in one file
(`mov ah,4xh; int 21h`). A CP/M-86 port therefore = **reuse everything above,
and swap only that thin bottom for BDOS (`INT E0h`)**. Nothing in the port
patches Watcom's clib sources; the entire retarget is an additive seam under
`port/`.

## Verification methodology — two oracles, one purity gate

Every milestone is a **self-checking pass/fail oracle**, run two ways:

1. **emu2-cpm86** — fast smoke test for round-trips and correctness. Not
   cycle-accurate and does *not* model Concurrent-CP/M lock lists or CP/M-3
   directory semantics, so it is a convenience oracle, never the authority.
2. **RC759 under MAME** — the authoritative oracle: the real i80186 @ 6 MHz
   machine running Concurrent CP/M-86 3.1. Results are streamed out of the
   guest on undecoded port `0x2FE` and captured by a Lua tap.

A **purity gate** runs on every build: a raw-image scan asserts
`INT21h(DOS) = 0`. The port must reach the OS *only* through CP/M-86's
`INT E0h` BDOS — never an MS-DOS call. This is what proves it is a genuine
CP/M-86 port and not accidentally leaning on DOS emulation.

## The journey, milestone by milestone

Each item links Watcom clib functionality to the thin BDOS primitive that
backs it. Commit hashes are on `master`.

1. **Console `printf`** (`a3320efd7d`) — proof of concept. Watcom's pure
   formatter core `__prtf` (verified 0× INT 21h) drives a callback that writes
   to the CP/M-86 console via BDOS `C_WRITE`. That one callback is the whole
   console retarget.
2. **Near-heap `malloc`/`free`/`calloc`/`realloc` + `qsort`** (`bb1c7f6878`) —
   resolved entirely by an arena `__brk`/`sbrk` seam.
3. **Genuine `stdio` FILE\* write path** (`dd3f048ee3`) — Watcom's real
   buffered `stdio`, not a stub.
4. **`stdcbench` 0.8** (`d2981616ab`) — a real benchmark; Watcom clib **beats
   DR C** on the same machine.
5. **Float without an 8087** — the RC759 has no 8087. Designed the trap-vector
   approach and IVT seam (`ce4b5e4998`, `e78d4686be`), then proved Watcom's
   **own `-fpc` double soft-float** with no 8087 (`1a9d85a4a0`).
6. **Transcendental math + real `%e` `printf`** — Whetstone runs the libm
   transcendentals and IEEE float formatting, no 8087 (`d98f624934`); wired to
   MAME rc759 external-timing hooks and recorded timings
   (`3cafc21d19`, `91628fcd88`).
7. **Watcom's own float01–04 regression suite** on CP/M-86 (`5f73b5d257`).
8. **Self-initialising crt0** via `__CommonInit` (`751f601c63`) and a C99
   default (`7aaabeb593`).
9. **Disk FILE\* path** via the FCB random-record BDOS seam (`6a3d0cf315`),
   with byte-exact `SEEK_END` and a runtime-gated CP/M-3 **Last-Record Byte
   Count (LRBC)** decode for exact file lengths (`5e2e3509bd`).
10. **First authoritative pass** — the disk FILE\* seam **MAME-verified on the
    real RC759 / Concurrent CP/M-86 3.1** (`9787ed015a`).
11. **`clibtest` group completion** — low-level POSIX `handleio` +
    `rename` (`f6d09797c1`), `tmpnam`/`tmpfile` (`c06e12c0e1`), Watcom's
    **unchanged** `fscanf` (`4fa2fd8e41`) and `streamio`/`iotest`
    (`53aa9d29de`) passing as-is.
12. **Write-side LRBC** — persist exact binary length on close so a reopen is
    byte-exact (`f21c99328f`, MAME-verified).
13. **`chmod`** — W-bit only, mapped to CP/M's sole writability attribute, the
    read-only bit, via F_ATTRIB (`2acda6790d`, MAME-verified).
14. **`access` / `stat` / `utime`** — file-status probe via **SEARCH FIRST
    (BDOS fn 17)**, deliberately *not* F_OPEN. F_OPEN allocates a Concurrent-
    CP/M lock-list entry that only F_CLOSE frees; an open-without-close probe
    leaks locks until the BDOS aborts the program to the CCP — a bug emu2
    (which models no lock list) never showed, only the real RC759 did. F_SFIRST
    reads the directory entry with no lock and no close (`9e013e363b`).
15. **C++ layer** — Watcom's own **iostreams** (`cout`/`cin`/`cerr`),
    **exceptions** (`try`/`throw`/`catch`, unwinding) and **`setjmp`/`longjmp`**
    layered over the disk FILE\* seam, MAME rc759-verified (`cppfeat` 8/0,
    `mame_cpptest` 6/0). Only three seams beyond the C port: `port/cpprt.c`
    (`__clib_malloc`/`__clib_free` → Watcom `nmalloc`/`nfree`), `port/ehsupp.c`
    (near `__longjmp_handler`, overlay-stack nulls, `__clib_exit`/`__clib_fatal`),
    and a C++-aware startup `port/crt0cpp.asm` that walks the `XI`/`YI` global
    ctor/dtor tables (the one thing plain C never needs). This retargets the C++
    layer that first ran on a hand-written scratch mini-clib onto Open Watcom's
    own library (ravn/rc7xx-work#9, consolidated under #12).

## Result

The production C-library surface — buffered `stdio` FILE\*, low-level
`handleio`, `remove`/`rename`, `chmod`, `access`/`stat`/`utime`,
`tmpnam`/`tmpfile`, `fscanf`, `streamio`, `printf`, and `-fpc` soft-float —
is **implemented and MAME-verified on the real RC759**: the self-checking disk
oracle passes **686 checks / 0 failures**, with `INT21h(DOS) = 0` on every
build.

Remaining items are known and **non-blocking** (see `KNOWN_ISSUES.md` §8–9):
reading CP/M-3 SFCB datestamps into `stat` via F_TIMEDATE (fn 102), decoupling
`fscanf`'s float scanner from integer-only use, the 8087 *hardware* emulator
path (irrelevant to the 8087-less RC759, where `-fpc` soft-float is the
production route), and a second-platform fallback cross-check on IBM-PC
CP/M-86 1.0 (parked, TPA-capped).

## The lesson

A well-layered library ports by **replacing its thinnest bottom layer**, not by
rewriting it. Watcom's clib carried its OS dependency in a few primitives;
supplying a ~BDOS seam under `port/` reused the entire formatter, `stdio`,
heap, and math stack unchanged. And an emulator is a convenience, never an
oracle: the lock-leak abort proved that only the real target hardware model can
certify the port.
