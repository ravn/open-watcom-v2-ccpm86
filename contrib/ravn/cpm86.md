# Open Watcom C/C++ for CP/M-86

> NOTE:  This is early software which runs for me but has not yet been thoroughly tested by others.  Notably **far heap** and **far code** were really hard to get working so please test thoroughly if you use those and report any problems you find.

This document describes the CP/M-86 platform support in the ravn fork of
Open Watcom v2.  The target is the 16-bit Intel 8086/80186 processor running
under Digital Research CP/M-86 or Concurrent CP/M-86 (CCP/M-86).  Testing has been done on the Regnecentralen "Piccoline" Rc759 with 384 Kb RAM running CCP/M-86 3.1 as running in MAME, and on the emu2 CP/M-86 emulator.   

This is a full port to a new platform  strongly based on the 16-bit MS-DOS work, and using Digital Research C v. 1.11 as the oracle for CMD-files. 

Note:  `<file` and `>file` can be used to redirect input and output if the diskio layer has been pulled in by the linker.  CP/M-86 does not support this natively.

## Compiler invocation

Use `owcc` with `-bcpm86`:

```sh
owcc -bcpm86 -O2 -o hello.cmd hello.c
```

The resulting `.cmd` file is a native CP/M-86 executable that can be run
directly under CP/M-86, CCP/M-86, or an emulator such as emu2.

### Docker

Pre-built Docker images are available (image names are not final):

```sh
# Compile
docker run --rm -v "$PWD":/work open-watcom-cpm86:latest \
    owcc -bcpm86 -O2 -o hello.cmd hello.c

# Run
docker run --rm -v "$PWD":/work emu2-cpm86:latest emu2 hello.cmd
```
## Pitfalls

* CCP uppercases the command line tail passed to the program.  No proper workaround has been found yet.


## Memory models

| Flag | Model | Description |
|------|-------|-------------|
| `-mcmodel=s` | small | One CODE + one DATA segment (≤64 KB each). Near pointers only. Default. |
| `-mcmodel=m` | medium | Multiple CODE segments, one DATA segment. Far code pointers. |
| `-mcmodel=c` | compact | One CODE segment, multiple DATA segments. Far data pointers. |
| `-mcmodel=l` | large | Multiple CODE and DATA segments. All pointers far. |
| `-mcmodel=h` | huge | Like large; individual arrays may exceed 64 KB. |

For most CP/M-86 programs the **small** model is the right choice.  

### Far heap (compact/large model)

> Memory management was difficult to get right and may be buggy for large models. Test well!

To allocate far memory from a compact or large model program, pass
`OPTION FARHEAP=<n>` to `wlink` (or `-Wl,option -Wl,farheap=<n>` via
`owcc`).  The linker adds a type-3 EXTRA group descriptor so the
CP/M-86 loader reserves `<n>` bytes of far memory at load time.  The
program calls `malloc()` / `__AllocSeg()` to use it.

## Predefined macros

| Macro | Value | Meaning |
|-------|-------|---------|
| `__CPM86__` | 1 | CP/M-86 target |
| `__CPM__` | 1 | CP/M family target |
| `_M_I86` | 1 | 16-bit Intel x86 |
| `M_I86SM` / `M_I86MM` / ... | 1 | Active memory model |

## Compiler flags

| Flag | Meaning |
|------|---------|
| `-bcpm86` | Select CP/M-86 target platform |
| `-O2` | Full optimisation (recommended) |
| `-march=i186` | Generate 80186 instructions (IMUL, PUSHA, etc.) |
| `-msoft-float` / `-fpc` | Soft-float (no 8087); use when no FPU is present |
| `-Wl,option -Wl,farheap=N` | Reserve N bytes of far heap (compact/large model) |

## CMD file format

A `.cmd` file starts with a 128-byte header of up to 8 group descriptors
(9 bytes each), followed by the group images concatenated in descriptor order.

Each descriptor:

| Offset | Size | Field | Meaning |
|--------|------|-------|---------|
| 0 | 1 | type | 1=CODE 2=DATA 3=EXTRA 4=STACK |
| 1 | 2 | length | paragraphs stored in file |
| 3 | 2 | base | absolute base (0 = relocatable) |
| 5 | 2 | min | minimum paragraphs to allocate |
| 7 | 2 | max | maximum paragraphs to allocate |

The loader allocates memory between `min` and `max` paragraphs for each
group, reads `length` paragraphs from the file, and zero-fills the rest up
to the allocated size.  This means **BSS does not need to be stored in the
file** — the linker sets `length` to cover only the initialised data and
`min` to include BSS, so BSS is zero-filled by the loader at no file cost.

Relocatable `.cmd` files (the default) have `base=0` in every descriptor.
Load-time relocation is applied by the loader via a P_LOAD fixup table
appended after the last group image (header byte 0x7F bit 7 = 1, table
record offset at header word 0x7D).

## Runtime library

The CP/M-86 clibs (`lib286/cpm86/clibs.lib`, `clibm.lib`, etc.) provide:

- Standard C library: `stdio.h`, `stdlib.h`, `string.h`, `time.h`, …
- CP/M-86 BDOS calls via inline INT 0xE0/0xE1
- Console I/O via BDOS function 2 (C_WRITE) / 1 (C_READ)
- File I/O via BDOS FCB functions (15–40) and the stdio FILE* layer
- Near heap via `malloc()`/`free()` (small/medium model)
- Far heap via `__AllocSeg()`/`__FreeSeg()` (compact/large model)

### Concurrent CP/M-86 extensions

On Concurrent CP/M-86 (CCP/M-86), additional BDOS functions are available in emu2:

| Function | Number | Description |
|----------|--------|-------------|
| T_GET | 105 | Get date and time |
| T_SECONDS | 155 | Get date and time including seconds |
| T_SET | 104 | Set date and time (accepted; no-op in emulators) |
| M_ALLOC | 128 | Allocate far memory (MPB interface) |
| M_FREE | 130 | Free far memory |

## Known limitations

- **No protected mode**: 80286 protected mode is not supported.
- **No threads**: CP/M-86 is single-tasking; CCP/M-86 processes are
  independent programs, not threads.
- **Near heap limit**: in small/medium model the near heap shares the 64 KB
  DATA segment with global variables and the stack.  Use compact/large model
  with `OPTION FARHEAP` for programs needing more than ~30 KB of heap.
- **No `double` / 64-bit float**: on CP/M-86 hardware without an 8087 FPU,
  use `-msoft-float` and avoid `double`; use `float` instead.  CMD-format for this is untested.
- **`putchar` pulls in stdio**: the standard `putchar()` links the full
  stdio/file I/O layer, but also provides redirection of data.  For minimal programs, call BDOS function 2 directly.

## Examples

See `contrib/ravn/owc-drc/` for benchmark test cases:

- `mandel.c` — fixed-point Mandelbrot (portable, 80×25 ASCII)
- `mandel-ow.c` — same with `#pragma aux` IMUL optimisation (~8× faster)
- `mandelf.c` — floating-point Mandelbrot (requires 8087 or `-msoft-float`)

Build and run all variants:

```sh
./contrib/ravn/bench-mandel.sh
```
