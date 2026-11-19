# Open Watcom compiler / assembler / linker flags used in the CP/M-86 builds

Reference for the `wcc` (C), `wpp` (C++), `wasm` (assembler) and `wlink` (linker)
flags used across the `contrib/ravn/` CP/M-86 build scripts, why each is chosen,
and which notable flags are deliberately **not** used. Flag meanings are taken
from the build scripts' own legends where present, otherwise from the Open
Watcom C/C++ User's Guide.

## Flag sets actually used

### `watcom-cpm86-libc/` — the OW-clib port

| Variable | Flags | Used for |
|---|---|---|
| `CLIB` | `-bt=dos -0 -ms -zastd=c99 -zl -x` | compiling **Open Watcom's own clib sources** unchanged |
| `USER` | `-bt=dos -0 -ms -zl -zastd=c99` | compiling **our port seams** (`port/*.c`) + demo/test drivers |
| `USER` (float builds) | `… -fpc …` (+ `-i=$B/mathlib/h`) | float-bearing builds (`build-float.sh`, `build-whetstone.sh`, `build-owtests.sh`, `build-fscanf.sh`, `build-streamio.sh`) |
| `CLIB` (stdcbench) | `… -otexan` | the stdcbench benchmark only, where speed is the metric |
| C++ compile (`wpp`) | `-bt=dos -0 -ms [-xs] …` | `build-cpp.sh`; `-xs` added only with `--eh` |
| assembler (`wasm`) | `-ms -0 [-q] -i=…` | crt0 / `i4m`/`i4d` / `setjmp86` etc. |
| linker (`wlink`) | `format cpm86 op dosseg op quiet [op nodefaultlibs]` | all links |

### `owc-drc/` — Watcom C linked against the genuine DR C runtime (different context)

```
-0 -ms -s -zl -ecc -fpi87 -nt=CODE -fi=compat.h
```

This set exists to interoperate with Digital Research's own C library, so it
differs from the port on purpose (see per-flag notes below).

## What each flag means

- **`-0`** — generate **8086/8088** (16-bit) code. The lowest common denominator; see "Not used" below for why not `-1`.
- **`-ms`** — **small memory model** (64 KB code + 64 KB data). The only model currently working for CP/M-86 here.
- **`-bt=dos`** — build **target OS = DOS**, i.e. select the DOS 16-bit header/runtime set. The port borrows Open Watcom's DOS 16-bit C runtime and retargets only the thin OS seam, so it compiles against the DOS headers.
- **`-zl`** — emit **no default-library references** (no library-name records in the object). All libraries are linked explicitly by full path, so the linker never auto-pulls a host library.
- **`-x`** — ignore the standard/`INCLUDE`-environment include search; use **only** the `-i=` paths given. Isolates the compile to the exact CP/M-86 header set.
- **`-zastd=c99`** — compile in **C99** standard mode (needed for the tests' C99 hex-float literals and `fpclassify`).
- **`-s`** — **no stack-overflow checking** (drops the `__STK` probe calls; the port supplies only a stub).
- **`-ecc`** — default calling convention = **cdecl** (owc-drc only; matches DR C's ABI).
- **`-fpc`** — floating point via **library calls (soft-float)**, no inline 8087. The production route: the RC759 has no 8087, and `-fpc` selects Watcom's own `__FDxemu` software path with **no** interrupt-vector install and **no** INT 0x34–0x3D traps.
- **`-fpi87`** — **inline 8087** instructions (owc-drc only, against DR C's library which lacks the Watcom float helpers). **Not** used by the port.
- **`-fpi`** — 8087 **trap-emulator** route (segment-0 IVT install). Deferred / not used — see `docs/8087_HARDWARE_SUPPORT_DEFERRED.md`.
- **`-nt=CODE`** — name the text/code segment `CODE` (owc-drc: so it merges with DR C's `CODE` segment).
- **`-fi=compat.h`** — force-include a header (owc-drc: the no-underscore naming shim).
- **`-xs`** — enable **C++ exception handling** (`wpp`); links the `iosx_s`/`plbxs` EH runtime variants.
- **`-otexan`** — Watcom **aggressive speed** optimization (favor execution time, inline expansion, max opt, relaxed aliasing, relaxed FP ordering). Used **only** for the stdcbench speed benchmark.
- **`-q`** (wasm) — quiet.
- **`wlink` options** — `format cpm86` (emit a CP/M-86 `.CMD`), `op dosseg` (DOS segment ordering so crt0 is first), `op quiet`, `op nodefaultlibs` (silence W1008 for the embedded `DEFAULT LIBRARY` directives; all libs are linked explicitly).

## Notable flags deliberately NOT used

- **`-1` / `-2` / `-3` …** (80186 / 80286 / 80386 code generation). The port builds `-0` (8086) for lowest-common-denominator safety, but **the RC759's CPU is an 80186**, so `-1` is an un-exploited, HW-safe code-density lever (immediate-count shifts, `push imm`, `pusha`/`popa`, `enter`/`leave`). Tracked as **ravn/open-watcom-v2-ccpm86#18**.
- **Optimization on the production seams.** Most library/seam/demo compiles use **no `-o` flag** (default) — the port has prioritized correctness and the purity gate over speed. Only the stdcbench benchmark uses `-otexan`. Turning on optimization (and `-1`) for the production images is measurable future work (#18).

## No compiler flag turns `__I4M` into a single `imul` (issue #9)

The portable fixed-point idiom `FP_MUL(a,b) = (int)((long)a * b >> 8)` is lowered
to a full 32×32 `call __I4M` + an 8-iteration `sar`/`rcr`/`loop` shift, even at
`-ox`. **No flag changes this** — it is a structural gap in the 16-bit code
generator, verified in the Open Watcom cg source (three independent layers):

1. **Instruction selection is purely result-type-driven.** `bld/cg/intel/i86/c/i86optab.c` OP_MUL row: an `I2`-typed multiply → `MUL2` (hardware `imul`), but an `I4`-typed (`long`) multiply → `RTN4C` = the `__I4M` runtime call — unconditionally. The `(long)` cast in `FP_MUL` makes the multiply node `I4`, so `__I4M` is always selected. No CPU-target flag gates this cell (the 16-bit CPU simply has no 32-bit register multiply).
2. **The front end never requests a widening multiply.** The cg *has* an `OP_EXT_MUL` / `emul` primitive (16×16→32) in the same table, but `bld/cc` / `bld/plusplus` never emit it for `(long)(int)a * (int)b`. The machinery exists but is not wired to the idiom.
3. **The one multiply optimizer is out of scope.** `bld/cg/c/multiply.c` (`MulToShiftAdd` / `CheckMul`) only strength-reduces multiply-**by-constant** and only for `type_class == WD/SW` = the **native machine word** (`bld/cg/h/targsys.h`: `WD/SW = U2/I2` on 16-bit). `FP_MUL` is variable×variable **and** `I4`, so it is excluded on both counts.

So the "narrow a wider-than-native multiply of narrower operands to a native
widening `imul`" optimization is simply **not implemented** — not an explicitly
documented `NYI`, just absent. It is structurally a **16-bit-only** gap: on a
386 target the same `(long)a*b` is native (`i86optab` 386 OP_MUL row → `MUL4`
hardware), so the opportunity never arises there; it only reappears at
`long long` (I8 → `__I8M`), which is rare. Because active Open Watcom
development targets 32/64-bit hosts where this has no value, the 16-bit lever
was never built. Enhancement request: **ravn/open-watcom-v2-ccpm86#9**;
per-operation impact estimate on #18.
