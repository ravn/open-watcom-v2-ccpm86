# CP/M-86 benchmarks — Open Watcom (`owcc -bcpm86`)

This directory contains benchmark test cases for the CP/M-86 target in the
ravn/open-watcom-v2 fork (`owcc -bcpm86`).

## History

The original work here attempted to make Open Watcom C generate code that
links against the genuine Digital Research C (DR C) run-time library
(`clears.l86`) through DR's own `LINK-86` linker.  That worked — the
calling conventions are compatible with the right shims — but the approach
required several non-trivial workarounds (no leading underscore on symbols,
segment name renaming, a hand-written startup object, 8087 inline float,
avoiding file-scope `static`, short object file paths).  When the fork
gained a proper CP/M-86 platform target (`-bcpm86`) with its own crt0 and
C library, the DRC link path became unnecessary.  **It is simpler and more
correct to implement CP/M-86 as a first-class Watcom platform than to glue
Watcom objects into the DR C runtime.**

The DRC-specific files (owcrt.asm, compat.h, build-owc-drc.sh, fetch-drc.sh,
glue.c, dhry21/, stdcbench/, etc.) have been removed.  The benchmark results
and the technical details of the DRC interop are preserved in the git history
(look for commits mentioning "owc-drc").

## Test cases

### Fixed-point Mandelbrot (`mandel.c`, `mandel-ow.c`)

80×25 ASCII Mandelbrot in 8.8 fixed-point arithmetic, 30 iterations/cell.
Two variants:

- **`mandel.c`** — portable; `FP_MUL(a,b) = (int)((long)a*b>>8)`.  Works
  with any C compiler.
- **`mandel-ow.c`** — Open Watcom specific; `FP_MUL` is a `#pragma aux`
  routine that lowers to one 16×16 `IMUL` plus a two-instruction byte
  extract of bits [8..23].  Runs ~8× faster than the portable variant on
  80186 because Watcom otherwise widens both operands to `long` and calls
  its 32×32 `__I4M` library + an 8-step shift loop.

Build and run:

```sh
docker run --rm -v "$PWD":/work open-watcom-cpm86:latest \
    owcc -bcpm86 -O2 -o mandel.cmd mandel.c
docker run --rm -v "$PWD":/work emu2-cpm86:latest emu2 mandel.cmd
```

### Float Mandelbrot (`mandelf.c`)

Same geometry, using `float` arithmetic.  Requires 8087 emulation in emu2.

```sh
docker run --rm -v "$PWD":/work open-watcom-cpm86:latest \
    owcc -bcpm86 -O2 -fpc -o mandelf.cmd mandelf.c
docker run --rm -v "$PWD":/work emu2-cpm86:latest emu2 mandelf.cmd
```

### Benchmark script (`../bench-mandel.sh`)

Builds and compares O0 / O2 / IMUL variants of the Mandelbrot kernel and
reports size and instruction count.
