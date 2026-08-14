#!/bin/bash
# run-stdcbench-mame.sh -- run the WATCOM-clib stdcbench (SCB-WC.CMD) on the REAL
# MAME rc759 driver, where the cycle-accurate emulated 80186 makes the score
# comparable to the Digital Research C reference (final score 13).  This is the
# RC759-comparable half of the wc-stdcbench gate; build-stdcbench.sh's emu2 run
# only proves functional execution (its score reflects the host Mac's speed).
#
# The benchmark is built with -DMAME_DONE, so it ends with OUT 0x2FE,score; the
# scb_mame.lua write-tap stops the emulator and prints the score exactly when the
# run finishes (no OCR, no fixed timer).  Same mechanism the DR C scb-mame.sh
# used -- we only swap in the Watcom-clib CMD.
#
# Prereqs (all inside /Users/ravn/z80): mame/regnecentralend, the rc759 turnkey
# image scratch/rc759-pce/images/mandel.img, the drc-rc759 diskdefs there, the
# cpmtools (cpmcp/cpmrm/cpmls) on PATH, and mame-tests/scb_mame.lua.
# NEVER search outside /Users/ravn/z80/.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
Z80=/Users/ravn/z80
MAME_DIR="$Z80/mame"
IMAGES="$Z80/scratch/rc759-pce/images"
LUA="$Z80/scratch/rc759-cmd-toolchain/mame-tests/scb_mame.lua"
FMT=drc-rc759

echo "== 1. build SCB-WC.CMD (Watcom clib + our shim, -DMAME_DONE) =="
( cd "$HERE" && SCB_EXTRA="-DMAME_DONE" SCB_NORUN=1 bash build-stdcbench.sh \
    >/tmp/scb_wc_build.log 2>&1 ) || { echo "build failed:"; tail -20 /tmp/scb_wc_build.log; exit 1; }
CMD="$HERE/SCB-WC.CMD"
[ -f "$CMD" ] || { echo "build did not produce SCB-WC.CMD"; exit 1; }
echo "   SCB-WC.CMD = $(stat -f%z "$CMD") bytes"

echo "== 2. install SCB-WC.CMD as autostart menu.cmd on a copy of mandel.img =="
IMG="$IMAGES/scbwc.img"
cp "$IMAGES/mandel.img" "$IMG"
cd "$IMAGES"                         # cpmtools reads ./diskdefs from CWD
# The turnkey disk is packed (~10 KB free); SCB is ~47 KB, so free space by
# removing large optional apps not needed to boot + autorun.
for f in menu.cmd comal80.cmd comal80.erm diskvedl.cmd filadm.cmd function.cmd \
         function.sys asm86.cmd ddt86.cmd chset.cmd ed.cmd filex.a86 filex.cmd \
         gencmd.cmd help.hlp mandel.cmd; do
    cpmrm -f "$FMT" "$IMG" "0:$f" 2>/dev/null || true
done
cpmcp -f "$FMT" "$IMG" "$CMD" 0:menu.cmd
cpmls -f "$FMT" -l "$IMG" | grep -i "menu.cmd" || { echo "install failed (disk full?)"; exit 1; }

echo "== 3. boot MAME rc759; stop on stdcbench done-signal =="
cd "$MAME_DIR"
rm -f snap/rc759/*.png nvram/rc759/nvram 2>/dev/null || true
./regnecentralend rc759 -bios 0 -skip_gameinfo -rompath roms \
  -flop1 "$IMG" \
  -autoboot_script "$LUA" -seconds_to_run 600 \
  -nothrottle -sound none -video bgfx -window -nomax 2>&1 \
  | tee /tmp/scb_wc_mame.log | grep -i "DONE-SIGNAL" || true

echo "== 4. result =="
if grep -qi "DONE-SIGNAL" /tmp/scb_wc_mame.log; then
  LINE=$(grep -i "DONE-SIGNAL" /tmp/scb_wc_mame.log | tail -1)
  echo "$LINE"
  echo "Watcom-clib stdcbench finished on real MAME rc759 (compare score to DR C reference 13)."
else
  echo "WARNING: no DONE-SIGNAL within the cap -- stdcbench did not complete."
  echo "(If it spun, the disk XIOS may not maintain BDOS T_GET fn 105; the DR C"
  echo " reference used T_SECONDS fn 155 -- see test/scbport.c clock note.)"
  LAST=$(ls snap/rc759/ 2>/dev/null | tail -1)
  [ -n "$LAST" ] && echo "Latest diagnostic snapshot: $MAME_DIR/snap/rc759/$LAST"
fi
