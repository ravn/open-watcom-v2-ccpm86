#!/bin/bash
# build-deflate-fheap-mame.sh -- reproduce the Info-ZIP deflate far-heap hang as
# a self-checking oracle on the real MAME rc759 CCP/M-86.
#
# Builds test/deflate_fheap_test.c in LARGE model with the SAME far-heap recipe
# as build-zip-cpm86.sh (clibl.lib + the M9 farheap.c, FARHEAP=0x10000), installs
# it as autostart menu.cmd, boots MAME headless, and gates on the guest's
# mame_done() word: HI byte = fail-mask (0 == PASS), LO byte = far blocks obtained.
#
# Expected: PASS under emu2, FAIL on real CCP/M-86 MAME until port/farheap.c is
# fixed -- this is the oracle for that fix.  NEVER search outside /Users/ravn/z80/.
set -e
cd "$(dirname "$0")"
LIBC="$(pwd)"
OW="$(cd ../../.. && pwd)"; B="$OW/bld"
LIBDIR="$OW/lib286/cpm86"
PORTDIR="$LIBC/port"
ENVSH="$LIBC/../cpm86-clib/env.sh"
MAMEDIR=/Users/ravn/z80/scratch/rc759-cmd-toolchain/mame-tests
MAME_BIN=/Users/ravn/z80/mame/regnecentralend
MAME_ROOT=/Users/ravn/z80/mame
IMAGES=/Users/ravn/z80/scratch/rc759-pce/images
FMT=drc-rc759
CPMCP=$HOME/.local/bin/cpmcp
CPMRM=$HOME/.local/bin/cpmrm
CPMLS=$HOME/.local/bin/cpmls
OUTDIR="${OUTDIR:-build-deflate-fheap}"; mkdir -p "$OUTDIR"

: "${FARHEAP:=0x10000}"                       # match zip's hang scenario
FARHEAP_PARAS=$(( FARHEAP / 16 ))

[ -f "$LIBDIR/clibl.lib" ]     || { echo "missing $LIBDIR/clibl.lib (build-lib.sh MODEL=l)"; exit 1; }
[ -f "$LIBDIR/cstartlm.obj" ]  || { echo "missing $LIBDIR/cstartlm.obj"; exit 1; }

# shellcheck disable=SC1090
. "$ENVSH" >/dev/null 2>&1 || true

INC="-I$B/hdr/dos/h -I$B/clib/h -I$B/clib/intel/h -I$B/watcom/h -I$B/lib_misc/h -I$MAMEDIR"
PORTINC="-I$B/clib/h -I$B/clib/heap/h"
CFLAGS="-bcpm86 -march=i186 -mcmodel=l -zm -zt64 -Os"

echo "== 1. build FHDEFL.CMD (large model, farheap=$FARHEAP) =="
owcc $CFLAGS -DMAME_DONE $INC -c test/deflate_fheap_test.c -o "$OUTDIR/t.obj"
owcc $CFLAGS $PORTINC -DCPM86_FARHEAP_PARAS=$FARHEAP_PARAS \
    -c "$PORTDIR/farheap.c" -o "$OUTDIR/farheap.obj"
"$B/wl/osxa64/wlink.exe" format cpm86 op dosseg op quiet op start=_cstart_ op farheap=$FARHEAP \
    name "$OUTDIR/FHDEFL.CMD" \
    file "$LIBDIR/cstartlm.obj" file "$OUTDIR/t.obj" file "$OUTDIR/farheap.obj" \
    library "$LIBDIR/clibl.lib"
echo "   FHDEFL.CMD = $(stat -f%z "$OUTDIR/FHDEFL.CMD") bytes"

# emu2 smoke (expected PASS -- masks the bug)
if [ -x /Users/ravn/z80/emu2-cpm86/emu2 ]; then
  echo "== emu2 smoke =="
  ( cd "$OUTDIR" && /Users/ravn/z80/emu2-cpm86/emu2 FHDEFL.CMD 2>/dev/null | grep -iE "block|PASS|FAIL" || true )
fi

echo "== 2. install as autostart menu.cmd on a copy of mandel.img =="
IMG="$IMAGES/fhdefl.img"
cp "$IMAGES/mandel.img" "$IMG"
( cd "$IMAGES"
  for f in menu.cmd comal80.cmd comal80.erm diskvedl.cmd filadm.cmd function.cmd \
           function.sys asm86.cmd ddt86.cmd chset.cmd ed.cmd filex.a86 filex.cmd \
           gencmd.cmd help.hlp mandel.cmd mandeldr.cmd; do
      "$CPMRM" -f "$FMT" "$IMG" "0:$f" 2>/dev/null || true
  done
  "$CPMCP" -f "$FMT" "$IMG" "$LIBC/$OUTDIR/FHDEFL.CMD" 0:menu.cmd
  "$CPMLS" -f "$FMT" -l "$IMG" | grep -i "menu.cmd" || { echo "install failed"; exit 1; } )

echo "== 3. boot MAME rc759 headless; stop on guest mame_done() =="
( cd "$MAME_ROOT"
  rm -f snap/rc759/*.png nvram/rc759/nvram 2>/dev/null || true
  SDL_VIDEODRIVER=dummy "$MAME_BIN" rc759 -bios 0 -skip_gameinfo -rompath roms \
    -flop1 "$IMG" \
    -autoboot_script "$MAMEDIR/farheap_done_dump.lua" -seconds_to_run 120 \
    -nothrottle -sound none -video none 2>&1 \
  | tee /tmp/fhdefl_mame.log | grep -iE "DONE-SIGNAL" || true )

echo "== 4. result =="
WORD=$(grep -iE "DONE-SIGNAL" /tmp/fhdefl_mame.log | grep -oE "word=0x[0-9A-Fa-f]+" | head -1 | cut -d= -f2)
LAST=$(ls -t "$MAME_ROOT"/snap/rc759/*.png 2>/dev/null | head -1)
[ -n "$LAST" ] && echo "snapshot: $LAST"
if [ -z "$WORD" ]; then
  echo "NO DONE-SIGNAL within the cap -- guest hung before signalling (also a FAIL:"
  echo "the far-heap corruption can dead-loop deflate-style before main() returns)."
  exit 1
fi
W=$(( WORD )); HI=$(( (W >> 8) & 0xFF )); N=$(( W & 0xFF ))
echo "guest word=$WORD -> fail-mask=0x$(printf %02x $HI), blocks obtained=$N"
if [ "$HI" = "0" ] && [ "$N" = "3" ]; then
  echo "PASS: far heap gives deflate 3 valid, distinct, zeroed 8 KB blocks"
else
  echo "FAIL: far-heap corruption reproduced (mask 0x$(printf %02x $HI): 01=NULL 02=not-zeroed 04=alias 08=overlap)"
  exit 1
fi
