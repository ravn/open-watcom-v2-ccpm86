#!/bin/bash
# build-bss-mame-allmodels.sh -- build bss_assert for all 4 memory models and
# verify BSS zero-fill on MAME rc759 in a SINGLE boot.
#
# What this does:
#   1. Build BSSAS/BSSAM/BSSAC/BSSAL.CMD (small/medium/compact/large models).
#   2. Install all 4 on a copy of mandel.img (no menu.cmd -> CCP A> prompt).
#   3. Boot MAME rc759 with bss_all_models.lua, which injects each CMD name
#      in sequence, collects results from port 0x2FE (mame_out protocol), and
#      prints the summary before exiting.
#
# Prerequisites: lib286/cpm86/clibs.lib + clibc.lib + clibm.lib + clibl.lib
#                and matching cstart*.obj must be INSTALLED (run build-lib.sh
#                for each model if absent).
#
# NEVER search outside /Users/ravn/z80/.
set -e
unset WCC WASM WLIB WLINK
cd "$(dirname "$0")"
LIBC="$(pwd)"
OW="$(cd ../../.. && pwd)"; B="$OW/bld"
WCC="${OWCC_BIN:-$OW/rel/armo64/wcc}"
WLINK="${OWLINK_BIN:-$OW/rel/armo64/wlink}"
LIBDIR="$OW/lib286/cpm86"
INC="-i=$B/clib/h -i=$B/clib/streamio/h -i=$B/watcom/h -i=$B/hdr/dos/h"

MAMEDIR=/Users/ravn/z80/scratch/rc759-cmd-toolchain/mame-tests
MAME_BIN=/Users/ravn/z80/mame/regnecentralend
MAME_ROOT=/Users/ravn/z80/mame
IMAGES=/Users/ravn/z80/scratch/rc759-pce/images
FMT=drc-rc759
CPMCP=$HOME/.local/bin/cpmcp
CPMRM=$HOME/.local/bin/cpmrm
CPMLS=$HOME/.local/bin/cpmls
OUTDIR="${OUTDIR:-build-bss-mame}"; mkdir -p "$OUTDIR"
LOG="$OUTDIR/mame.log"

[ -x "$MAME_BIN" ]             || { echo "missing MAME binary at $MAME_BIN"; exit 1; }
[ -f "$LIBDIR/clibs.lib" ]     || { echo "missing $LIBDIR/clibs.lib -- run MODEL=s bash build-lib.sh"; exit 1; }
[ -f "$LIBDIR/clibc.lib" ]     || { echo "missing $LIBDIR/clibc.lib -- run MODEL=c bash build-lib.sh"; exit 1; }
[ -f "$LIBDIR/clibm.lib" ]     || { echo "missing $LIBDIR/clibm.lib -- run MODEL=m bash build-lib.sh"; exit 1; }
[ -f "$LIBDIR/clibl.lib" ]     || { echo "missing $LIBDIR/clibl.lib -- run MODEL=l bash build-lib.sh"; exit 1; }

echo "== 1. build bss_assert for all 4 models =="

build_model() {
    local model="$1" lib="$2" crt="$3" cmdname="$4" farheap="$5" zmflag="$6"
    printf "   model=%-8s  compile... " "$model"
    "$WCC" -bt=dos -0 -m$model $zmflag -zastd=c99 $INC \
        test/bss_assert.c -fo="$OUTDIR/t_${model}.obj" 2>&1 | grep -v "^Open Watcom" | grep -v "^Version\|^Copyright\|^Portions\|^Source" || true
    printf "link... "
    cat > "$OUTDIR/link_${model}.lnk" <<LNKEOF
format cpm86
option dosseg
option quiet
option start=_cstart_
${farheap:+option farheap=0x30000}
libpath $LIBDIR
name $OUTDIR/$cmdname
file $LIBDIR/$crt
file $OUTDIR/t_${model}.obj
library $lib
LNKEOF
    "$WLINK" @"$OUTDIR/link_${model}.lnk" 2>&1 | grep -v "Warning.*clibcpm" || true
    [ -f "$OUTDIR/$cmdname" ] || { echo "LINK-FAIL"; exit 1; }
    echo "OK ($(stat -f%z "$OUTDIR/$cmdname") bytes)"
}

build_model s clibs.lib cstartcpm.obj BSSAS.CMD ""         ""
build_model m clibm.lib cstartmm.obj  BSSAM.CMD ""         "-zm"
build_model c clibc.lib cstartcm.obj  BSSAC.CMD "farheap"  ""
build_model l clibl.lib cstartlm.obj  BSSAL.CMD "farheap"  "-zm"

echo ""
echo "== 2. install on disk image (mandel.img base) =="
echo "   BSSAS.CMD -> menu.cmd (BIOS autostart); others injected via CCP"
IMG="$IMAGES/bss_allmodels.img"
cp "$IMAGES/mandel.img" "$IMG"
( cd "$IMAGES"
  # Remove unneeded files; keep CP/M system
  for f in menu.cmd comal80.cmd comal80.erm diskvedl.cmd filadm.cmd function.cmd \
           function.sys asm86.cmd ddt86.cmd chset.cmd ed.cmd filex.a86 filex.cmd \
           gencmd.cmd help.hlp mandel.cmd; do
      "$CPMRM" -f "$FMT" "$IMG" "0:$f" 2>/dev/null || true
  done
  # Small model -> menu.cmd (BIOS auto-runs it after boot)
  "$CPMCP" -f "$FMT" "$IMG" "$LIBC/$OUTDIR/BSSAS.CMD" "0:menu.cmd"
  printf "   installed 0:menu.cmd (BSSAS small)\n"
  # Other models as individual files for CCP injection
  for cmdname in BSSAM.CMD BSSAC.CMD BSSAL.CMD; do
      "$CPMCP" -f "$FMT" "$IMG" "$LIBC/$OUTDIR/$cmdname" "0:$cmdname"
      printf "   installed 0:%s\n" "$cmdname"
  done
  "$CPMLS" -f "$FMT" -l "$IMG" | grep -iE "BSS\|menu" || true )

echo ""
echo "== 3. boot MAME rc759 -- bss_all_models.lua injects commands sequentially =="
rm -f "$MAME_ROOT/snap/rc759/"*.png
( cd "$MAME_ROOT"
  rm -f nvram/rc759/nvram 2>/dev/null || true
  SDL_VIDEODRIVER=dummy \
  "$MAME_BIN" rc759 -bios 0 -skip_gameinfo -rompath roms \
    -flop1 "$IMG" \
    -autoboot_script "$MAMEDIR/bss_all_models.lua" \
    -seconds_to_run 180 \
    -nothrottle -sound none -video none 2>&1 ) | tee "$LOG"

echo ""
echo "== 4. result =="
if grep -q "OVERALL: PASS" "$LOG"; then
    grep "BSS-RESULT\|OVERALL" "$LOG"
    echo ""
    echo "MAME BSS ALL-MODELS: PASS"
else
    grep "BSS-RESULT\|OVERALL\|SAFETY\|INJECT" "$LOG"
    echo ""
    echo "MAME BSS ALL-MODELS: FAIL"
    exit 1
fi
