#!/usr/bin/env bash
# build-hello.sh -- Milestone 1 of ravn/open-watcom-v2#13.
#
# Recompiles Aztec C's stdlib puts() with Watcom wcc, links it with our own
# CP/M-86 crt0 + BDOS console glue, and runs the resulting .CMD under emu2-cpm86.
# Proves the core thesis of #13: Aztec stdlib *source* rebuilds cleanly under
# Watcom into a single-ABI CP/M-86 program (unlike DR C's binary-only .L86, #12).
#
# Prereqs (paths overridable by env):
#   OW    = built Open Watcom tree (osxa64 binaries)   [scratch/open-watcom-v2]
#   EMU2  = emu2 built with CP/M-86 support            [scratch/cpm86-tools/...]
# The Aztec source is fetched (uncommitted) by ../scripts/fetch-aztec-src.sh first.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"          # contrib/ravn/aztec-libc
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"

WASM="$OW/bld/wasm/osxa64/wasm.exe"
WCC="$OW/bld/cc/i86/osxa64/binbuild/wcc.exe"
WLINK="$OW/bld/wl/osxa64/wlink.exe"

AZ_PUTS="$here/src/STDIO/puts.c"                  # Aztec source (uncommitted)
CRT0="$here/port/crt0sm.asm"
GLUE="$here/port/cpm86_glue.c"
HELLO="$here/milestone-hello/hello.c"
out="$here/milestone-hello/build"

for f in "$WASM" "$WCC" "$WLINK"; do
    [ -x "$f" ] || { echo "ERR: missing tool $f (set OW)"; exit 1; }
done
if [ ! -f "$AZ_PUTS" ]; then
    echo "ERR: $AZ_PUTS not found -- run ../scripts/fetch-aztec-src.sh first."; exit 1
fi
[ -x "$EMU2" ] || { echo "ERR: emu2 not found at $EMU2 (set EMU2)"; exit 1; }

rm -rf "$out"; mkdir -p "$out"; cd "$out"

echo "INF: assembling crt0 ..."
"$WASM" -0 -q "$CRT0" -fo=crt0.obj

# -0 = 8086, -ms = small memory model, -bt=cpm86 selects the CP/M-86 target so
# the object carries the right default-lib / segment conventions.
CFLAGS="-0 -ms -bt=cpm86"
echo "INF: recompiling Aztec puts.c with wcc ..."
"$WCC" $CFLAGS "$AZ_PUTS" -fo=puts.obj
echo "INF: compiling console glue ..."
"$WCC" $CFLAGS "$GLUE" -fo=glue.obj
echo "INF: compiling hello.c ..."
"$WCC" $CFLAGS "$HELLO" -fo=hello.obj

echo "INF: linking hello.cmd ..."
"$WLINK" format cpm86 op dosseg op quiet name hello.cmd \
    file crt0.obj file hello.obj file puts.obj file glue.obj

echo "INF: running under emu2-cpm86 ..."
expected="hello, world -- aztec puts() recompiled by watcom on cpm86"
got="$("$EMU2" hello.cmd 2>/dev/null | tr -d '\r' | sed -n '1p')"
echo "----------------------------------------"
echo "$got"
echo "----------------------------------------"
# Real pass/fail oracle: the exact line must come back from Aztec puts()
# walking putchar() over the string.  A broken puts (early return, missing
# newline handling, wrong loop) changes this line and fails the check.
if [ "$got" = "$expected" ]; then
    echo "PASS: Aztec puts() recompiled by Watcom prints correctly on CP/M-86."
    echo "OK:   $out/hello.cmd"
else
    echo "FAIL: expected [$expected]"
    echo "         got  [$got]"
    exit 1
fi
