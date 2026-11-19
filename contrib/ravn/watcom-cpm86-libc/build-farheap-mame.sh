#!/bin/bash
# build-farheap-mame.sh -- MAME rc759 cross-check of the first-class far-heap seam.
#
# Independent-oracle end check (per user 2026-08-20): the far-heap round-trip is
# already PASSing under cpm86run_unicorn.py; this runs the SAME program on the
# cycle-faithful MAME rc759 (real i80186 + the real CP/M-86 loader that carves the
# type-3 Extra group) to confirm Unicorn's far-heap / loader model matches metal.
#
# 1. build FHMAME.CMD  = farheap_smalltest.c -DMAME_DONE, linked with the far-heap
#    recipe against the INSTALLED lib286/cpm86/clibs.lib (the first-class farheap).
# 2. install it as autostart menu.cmd on a copy of a bootable rc759 image.
# 3. boot MAME rc759; farheap_done_dump.lua stops on the guest's mame_done() word
#    AND dumps all RAM.
# 4. gate: DONE-SIGNAL word HIGH byte must be 0 (0 blocks failed) and the LOW byte
#    (= far blocks obtained, n) must be > 0; the independent RAM-dump scan
#    (verify_farheap_dump.py --seg $SEG --count n) must then find all n blocks.
#    MAME rc759 has only 384 KB RAM, so it grants FEWER far blocks than Unicorn's
#    1 MB -- that difference is expected and is exactly "analyse what you got".
#
# NEVER search outside /Users/ravn/z80/.
set -e
unset WCC WASM WLIB WLINK
cd "$(dirname "$0")"
LIBC="$(pwd)"
OW="$(cd ../../.. && pwd)"; B="$OW/bld"
WCC="${OWCC_BIN:-$B/cc/i86/osxa64/binbuild/wcc.exe}"
WLINK="${OWLINK_BIN:-$B/wl/osxa64/wlink.exe}"
LIBDIR="$OW/lib286/cpm86"
INC="-i=$B/clib/h -i=$B/watcom/h -i=$B/hdr/dos/h -i=$B/clib/heap/h"
FARHEAP_SIZE=0xF0000                  # ask for ~960 KB; MAME grants what fits in 384 KB
SEG=${SEG:-16384}                     # VARIABLE segment size (<= 64 KB Watcom cap)
TEST_SRC="${TEST_SRC:-test/farheap_smalltest.c}"

MAMEDIR=/Users/ravn/z80/scratch/rc759-cmd-toolchain/mame-tests
MAME_BIN=/Users/ravn/z80/mame/regnecentralend
MAME_ROOT=/Users/ravn/z80/mame
IMAGES=/Users/ravn/z80/scratch/rc759-pce/images
FMT=drc-rc759
CPMCP=$HOME/.local/bin/cpmcp
CPMRM=$HOME/.local/bin/cpmrm
CPMLS=$HOME/.local/bin/cpmls
OUTDIR="${OUTDIR:-build-farheap-mame}"; mkdir -p "$OUTDIR"
DUMP="$OUTDIR/mame_ram.bin"

[ -f "$LIBDIR/clibs.lib" ]     || { echo "missing $LIBDIR/clibs.lib"; exit 1; }
[ -f "$LIBDIR/cstartcpm.obj" ] || { echo "missing $LIBDIR/cstartcpm.obj"; exit 1; }

echo "== 1. build FHMAME.CMD (-DMAME_DONE) =="
"$WCC" -bt=dos -0 -ms -zastd=c99 -DMAME_DONE -DSEG=${SEG}u -i="$MAMEDIR" $INC \
    "$TEST_SRC" -fo="$OUTDIR/t.obj"
# memtest128.c is a pure BDOS-128 (M_ALLOC) raw-syscall probe -- it never
# touches an "option farheap" Extra group, so don't request one at link time.
# Requesting a huge Extra group (op farheap=$FARHEAP_SIZE) makes the LOADER
# itself reserve nearly the whole TPA for that unused group before main()
# even runs, starving the test's own runtime BDOS-128 calls of everything but
# a sliver -- an artifact of the shared build recipe, not a real fidelity bug.
if [ "$TEST_SRC" = "test/memtest128.c" ]; then
    "$WLINK" format cpm86 op dosseg op quiet op start=_cstart_ \
        name "$OUTDIR/FHMAME.CMD" \
        file "$LIBDIR/cstartcpm.obj" file "$OUTDIR/t.obj" library "$LIBDIR/clibs.lib"
else
    "$WLINK" format cpm86 op dosseg op quiet op start=_cstart_ op farheap=$FARHEAP_SIZE \
        name "$OUTDIR/FHMAME.CMD" \
        file "$LIBDIR/cstartcpm.obj" file "$OUTDIR/t.obj" library "$LIBDIR/clibs.lib"
fi
echo "   FHMAME.CMD = $(stat -f%z "$OUTDIR/FHMAME.CMD") bytes"

echo "== 2. install FHMAME.CMD as autostart menu.cmd on a copy of mandel.img =="
IMG="$IMAGES/fhmame.img"
cp "$IMAGES/mandel.img" "$IMG"
( cd "$IMAGES"                        # cpmtools reads ./diskdefs from CWD
  for f in menu.cmd comal80.cmd comal80.erm diskvedl.cmd filadm.cmd function.cmd \
           function.sys asm86.cmd ddt86.cmd chset.cmd ed.cmd filex.a86 filex.cmd \
           gencmd.cmd help.hlp mandel.cmd; do
      "$CPMRM" -f "$FMT" "$IMG" "0:$f" 2>/dev/null || true
  done
  "$CPMCP" -f "$FMT" "$IMG" "$LIBC/$OUTDIR/FHMAME.CMD" 0:menu.cmd
  "$CPMLS" -f "$FMT" -l "$IMG" | grep -i "menu.cmd" || { echo "install failed"; exit 1; } )

echo "== 3. boot MAME rc759 (headless); stop + dump on guest mame_done() =="
rm -f "$DUMP"
( cd "$MAME_ROOT"
  rm -f snap/rc759/*.png nvram/rc759/nvram 2>/dev/null || true
  FARHEAP_DUMP="$LIBC/$DUMP" SDL_VIDEODRIVER=dummy \
  "$MAME_BIN" rc759 -bios 0 -skip_gameinfo -rompath roms \
    -flop1 "$IMG" \
    -autoboot_script "$MAMEDIR/farheap_done_dump.lua" -seconds_to_run 120 \
    -nothrottle -sound none -video none 2>&1 \
  | tee /tmp/fhmame_mame.log | grep -iE "DONE-SIGNAL" || true )

echo "== 4. result =="
WORD=$(grep -iE "DONE-SIGNAL" /tmp/fhmame_mame.log | grep -oE "word=0x[0-9A-Fa-f]+" | head -1 | cut -d= -f2)
if [ -z "$WORD" ]; then
  echo "FAIL: no DONE-SIGNAL within the cap -- guest did not finish on MAME"
  exit 1
fi
W=$(( WORD ))
HI=$(( (W >> 8) & 0xFF ))       # blocks that FAILED the round-trip (expect 0)
N=$(( W & 0xFF ))               # far blocks obtained on MAME (< Unicorn: 384 KB RAM)
echo "MAME guest self-check word=$WORD -> n=$N far block(s), $HI failed"
if [ "$TEST_SRC" = "test/memtest128.c" ]; then
  python3 test/verify_memtest128_dump.py "$DUMP"
else
  echo "-- independent RAM-dump scan of the MAME snapshot (--seg $SEG --count $N) --"
  python3 test/verify_farheap_dump.py "$DUMP" --seg "$SEG" --count "$N" && SCAN=0 || SCAN=$?
fi
if [ "$TEST_SRC" = "test/memtest128.c" ]; then
  :
elif [ "$HI" = "0" ] && [ "$N" -gt 0 ] && [ "$SCAN" = "0" ]; then
  echo "PASS: MAME rc759 corroborates Unicorn -- far-heap works on real hardware"
  echo "      (MAME granted $N x $SEG B far heap; Unicorn's 1 MB grants more -- same algorithm)"
else
  echo "FAIL: MAME word=$WORD n=$N hi=$HI scan-rc=$SCAN"
  exit 1
fi
