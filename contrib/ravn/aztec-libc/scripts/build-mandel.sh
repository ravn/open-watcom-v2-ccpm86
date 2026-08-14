#!/usr/bin/env bash
# build-mandel.sh -- Milestone 2 of ravn/open-watcom-v2#13.
#
# Compiles the canonical integer Mandelbrot (milestone-mandel/mandel.c, kernel
# verbatim from owc-drc/mandel-ow.c) with Watcom wcc, links it against Aztec C's
# recompiled puts() + our crt0 + BDOS console glue, and runs the .CMD under
# emu2-cpm86. Proves Aztec stdlib source (puts) drives a real compute workload
# on CP/M-86 as a single-ABI Watcom program.
#
# CORRECTNESS ORACLE (independent, genuine 16-bit): Digital Research C v1.11
# build owc-drc/MANDEL-DRC.CMD run under the same emu2, first 78 columns of each
# of its 25 rows. Our output must be byte-identical. This oracle does NOT share
# our compiler/link path, and being genuinely 16-bit it avoids the int-width
# mismatch a 32-bit host clang build would introduce. Visual oracle: the MAME
# screenshot scratch/rc759-cmd-toolchain/mame-tests/MANDEL_mame_rc759.png.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"          # contrib/ravn/aztec-libc
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
ORACLE="${ORACLE:-/Users/ravn/z80/open-watcom-v2/contrib/ravn/owc-drc/MANDEL-DRC.CMD}"

WASM="$OW/bld/wasm/osxa64/wasm.exe"
WCC="$OW/bld/cc/i86/osxa64/binbuild/wcc.exe"
WLINK="$OW/bld/wl/osxa64/wlink.exe"

AZ_PUTS="$here/src/STDIO/puts.c"                  # Aztec source (uncommitted)
CRT0="$here/port/crt0sm.asm"
GLUE="$here/port/cpm86_glue.c"
MANDEL="$here/milestone-mandel/mandel.c"
out="$here/milestone-mandel/build"

for f in "$WASM" "$WCC" "$WLINK"; do
    [ -x "$f" ] || { echo "ERR: missing tool $f (set OW)"; exit 1; }
done
[ -f "$AZ_PUTS" ] || { echo "ERR: $AZ_PUTS not found -- run ../scripts/fetch-aztec-src.sh first."; exit 1; }
[ -x "$EMU2" ] || { echo "ERR: emu2 not found at $EMU2 (set EMU2)"; exit 1; }

rm -rf "$out"; mkdir -p "$out"; cd "$out"

CFLAGS="-0 -ms -bt=cpm86"
echo "INF: assembling crt0 ..."
"$WASM" -0 -q "$CRT0" -fo=crt0.obj
echo "INF: recompiling Aztec puts.c with wcc ..."
"$WCC" $CFLAGS "$AZ_PUTS" -fo=puts.obj
echo "INF: compiling console glue ..."
"$WCC" $CFLAGS "$GLUE" -fo=glue.obj
echo "INF: compiling mandel.c ..."
"$WCC" $CFLAGS "$MANDEL" -fo=mandel.obj
echo "INF: linking mandel.cmd ..."
"$WLINK" format cpm86 op dosseg op quiet name mandel.cmd \
    file crt0.obj file mandel.obj file puts.obj file glue.obj

run_cmd() {  # run_cmd <cmd> -> rendered rows on stdout (CR stripped, noise dropped)
    "$EMU2" "$1" 2>&1 | tr -d '\r' | grep -vE 'unimplemented opcode|emu2:' | grep .
}

echo "INF: running mandel.cmd under emu2-cpm86 ..."
run_cmd "$out/mandel.cmd" > "$out/got.txt"

if [ -f "$ORACLE" ]; then
    run_cmd "$ORACLE" | cut -c1-78 > "$out/exp.txt"
    echo "oracle: genuine DR C 1.11 MANDEL-DRC.CMD ($(grep -c . "$out/exp.txt") rows, first 78 cols)"
else
    echo "ERR: DR C oracle $ORACLE absent -- cannot byte-verify."; exit 1
fi

echo "----------------------------------------"
cat "$out/got.txt"
echo "----------------------------------------"

rows=$(grep -c . "$out/got.txt")
maxw=$(awk '{ if (length > w) w = length } END { print w }' "$out/got.txt")
has=$(grep -c '#' "$out/got.txt" || true)

fail=0
[ "$rows" -eq 25 ] || { echo "FAIL: expected 25 rows, got $rows"; fail=1; }
[ "$maxw" -le 78 ] || { echo "FAIL: max width $maxw > 78"; fail=1; }
[ "$has" -gt 0 ]   || { echo "FAIL: no set body ('#') present"; fail=1; }
if diff -q "$out/exp.txt" "$out/got.txt" >/dev/null; then
    echo "byte-identical to genuine DR C oracle: YES"
else
    echo "FAIL: DIFFERS from DR C oracle:"; diff "$out/exp.txt" "$out/got.txt" | head -40; fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "PASS: Aztec puts() drives the Watcom-compiled Mandelbrot; output"
    echo "      byte-identical to genuine DR C 1.11 (25 rows x 78 cols)."
    echo "OK:   $out/mandel.cmd"
else
    echo "FAIL"; exit 1
fi
