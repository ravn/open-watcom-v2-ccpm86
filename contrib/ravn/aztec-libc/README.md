# Aztec C stdlib -> Watcom C library for CP/M-86 (ravn/open-watcom-v2#13)

Goal: build a Watcom CP/M-86 C standard library by **recompiling Aztec C's
stdlib *source*** with `wcc`/`wasm`, so a whole program is one ABI and Watcom
retains full optimization visibility into libc. This sidesteps the ABI wall of
linking DR C's binary-only `.L86` (see #12): DR ships no source, so its routines
and their internal arithmetic helpers keep a foreign calling convention that
Watcom can neither bridge cleanly nor optimize across.

## License — sources are NOT committed
Aztec C is proprietary (Manx / Harry Suckow copyright; aztecmuseum.ca
redistributes for Fair-Use educational/enthusiast purposes only). Therefore the
Aztec **sources and any derived `.lib`/`.obj` binaries are never committed** —
they are fetched and rebuilt locally. Only our own scripts, glue, crt0, tests
and docs live in git. See `.gitignore`.

## Layout
- `scripts/fetch-aztec-src.sh` — fetch `az8634b.zip` and extract the CP/M-86
  library source archives into `src/` (uncommitted) via Aztec's `ARCV.COM` under
  emu2.
- `scripts/build-hello.sh` — **Milestone 1** (done): recompile Aztec `puts.c`
  with `wcc`, link with our crt0 + BDOS glue, run under emu2-cpm86, assert the
  exact output.
- `port/crt0sm.asm` — small-model CP/M-86 crt0 (from the #10 work; verified on
  real RC759).
- `port/cpm86_glue.c` — our BDOS console glue (`putchar`) so early milestones run
  without the full Aztec FILE/channel subsystem.
- `milestone-hello/` — the Milestone 1 program.
- `src/`, `work/` — fetched Aztec sources + scratch (gitignored).

## Milestones
1. **Hello world via Aztec `puts()` recompiled by Watcom — DONE.**
   `./scripts/fetch-aztec-src.sh && ./scripts/build-hello.sh` prints
   `hello, world -- aztec puts() recompiled by watcom on cpm86` under emu2-cpm86.
   The build asserts the exact line (falsifiable: a no-op `puts` fails it).
2. OS/glue layer + one Aztec leaf C routine through the differential harness.
3. Port pure-C groups (`string` -> `ctype` -> `stdlib` -> `stdio`), each gated by
   tests.
4. Float/math (deferred).
5. Package into an auto-fetched `clibs.lib` (reuse the #10 `specs.sp` mechanism).

See issue #13 for the full plan and the verified asm-vs-C survey (the critical
asm-porting burden is ~6 CPM86 OS-glue files; the bulk of libc is plain C).

## Prereqs / env
- `OW`   — built Open Watcom tree with osxa64 binaries (default
  `scratch/open-watcom-v2`).
- `EMU2` — emu2 with CP/M-86 support (default `scratch/cpm86-tools/...`).
- `curl`, `unzip`, and an emu2 for `ARCV.COM`.
