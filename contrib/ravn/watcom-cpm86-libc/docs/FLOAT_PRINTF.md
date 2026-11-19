# Real `%e` / `%f` / `%g` / `%a` printf + `scanf` float on CP/M-86 (opt-in)

By default the CP/M-86 clib links the **`noefgfmt` stub**: `printf("%f", ...)`
compiles and links, but the float conversion prints nothing — `__EFG_printf`
points at `_no_support_loaded`. This keeps integer-only programs small (the
double→decimal `dtoa`/`cvt` subsystem is ~4 KB and pulls 64-bit helpers).

## Enabling it

Two requirements — a **reference** so the linker pulls the formatter, and a
**runtime install** because our minimal crt0 does not walk Watcom's auto-init
table:

1. Compile with **`-fpc`** (soft-float, no 8087 — same as every float program here).
2. Call **`__setEFGfmt()` once** before your first `%f`/`%e`/`%g`:

```c
#include <stdio.h>
extern void __setEFGfmt( void );   /* install real _EFG_Format */

int main( void )
{
    __setEFGfmt();                 /* one call, before any %f */
    printf( "%.4f\n", 3.14159265 );  /* -> 3.1416 */
    return 0;
}
```

3. Link the model's **libm** as well as clib — the formatter core `_EFG_Format`
   and `__cnvs2d` (scanf side) live in `libm{s,m,c}.lib`:

```
wlink format cpm86 ... library clib$MODEL.lib library libm$MODEL.lib
```

That is all: `__setEFGfmt()`'s reference pulls `setefg` → `efgfmt` (`_EFG_Format`)
→ `cvt`/`ldcvt`/`efcvt`/`gcvt` → `cvtbuf` → `__U8LS` (64-bit shift) from the clib
archive, and `__cnvs2d` (scanf side) from libm. A program that never calls
`__setEFGfmt()` pulls none of it.

## `scanf` / `sscanf` / `fscanf` with `%f`

The read side is symmetric: the same `__setEFGfmt()` call points `__EFG_scanf` at
`__cnvs2d` (in libm). The scanf family (`scnf` core + `sscanf`/`fscanf`/`scanf`
entries + `isdigit`/`isspace`/`mbtowc`/`__U8M`) is archived in clib, pulled only
when you call a `*scanf`. So:

```c
__setEFGfmt();
double v; int i;
sscanf( "3.14159 42", "%lf %d", &v, &i );   /* v=3.14159, i=42 */
```

Link libm (for `__cnvs2d`) as for printing. Regression: `test/scanffmt_test.c`
(oracle `n=2 f=314159 i=42`), the `scanf` row of `run-all-models.sh`.

## `%a` hex-float

`printf("%a", 1.5)` -> `0x1.8p+0` works — but ONLY because the clib builds
`efgfmt.c` from CURRENT source (the prebuilt libm formatter object predates `%a`
and silently drops it). `build-lib.sh` archives the source `efgfmt` and clib links
before libm, so the `%a`-capable `_EFG_Format` wins. Covered by the `fltfmt` row.

## What is archived where

- **clib** (`clib{s,m,c}.lib`, built by `build-lib.sh`): `setefg`, `efgfmt`
  (source, `%a`-capable), `cvt`, `ldcvt`, `efcvt`, `gcvt`, `cvtbuf`, `i8ls086` —
  pulled only via `__setEFGfmt`; and the scanf family (`scnf`/`sscanf`/`fscanf`/
  `scanf` + `isdigit`/`isspace`/`mbtowc`/`i8m086`) — pulled only via `*scanf`.
- **libm** (`libm{s,m,c}.lib`): `cnvs2d` (`__cnvs2d`, scanf's double parser) and
  all the transcendentals — Watcom's stock 80186-safe soft-float mathlib. (Its
  own `efgfmt` is shadowed by clib's source build, which adds `%a`.)

## Verification

`test/floatfmt_test.c` is the regression: it calls `__setEFGfmt()` then prints
`%.4f`, `%.3e`, `%g` with the oracle **`f=3.1416 e=2.500e+00 g=0.001`**. It is the
`fltfmt` row of `run-all-models.sh` and PASSES in all three memory models
(small/medium/compact) under the Unicorn runner. No 8087 opcode is emitted or
executed (the `-fpc` `__FDxemu` path; the Unicorn harness has no FPU and would
trap on one).

## Not done

`scanf("%a")` (hex-float READ) is untested — `__cnvs2d` is the prebuilt libm
parser, which may share the `%a` gap the prebuilt printf formatter had. Console
`scanf` (stdin) is exercised only via `sscanf` here; the stdin path depends on the
runner's console-input BDOS.
