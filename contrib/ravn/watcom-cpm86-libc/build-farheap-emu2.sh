#!/bin/bash
# build-farheap-emu2.sh -- emu2-cpm86 cross-check of the first-class far-heap seam.
#
# Third independent loader (alongside cpm86run_unicorn.py and MAME rc759): emu2
# has a 640 KB TPA (mcb_init 0x80..0xA000), between MAME's 384 KB and Unicorn's
# 1 MB, so it grants a middling far-heap size -- exactly "analyse what you got".
#
# emu2's Extra-group allocation (src/cpm86.c: `want = max>min?max:min`, then
# mem_alloc_segment(), falling back to the largest available block >= g_min) is
# the SAME grant-min(max,available) model as Unicorn's _load clamp and the real
# CCP/M-86 loader on MAME. The SAME farheap.c __AllocSeg then carves identical
# 64 KB slabs (3x16 KB each) on all three.
#
# Oracles (same rigour as the other two harnesses):
#   guest  : prints "PASS far-heap n=NN seg=SSSS kb=KKK" over BDOS conout.
#   host   : EMU2_RAMDUMP dumps the full 1 MB RAM; verify_farheap_dump.py scans
#            it for n independent ramp-runs (content oracle OUTSIDE the guest).
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
EMU2="${EMU2:-/Users/ravn/z80/emu2-cpm86/emu2}"
FARHEAP_SIZE=0xF0000                  # ask for ~960 KB; emu2 grants what fits in 640 KB
SEG=${SEG:-16384}                     # VARIABLE segment size (<= 64 KB Watcom cap)
OUTDIR="${OUTDIR:-build-farheap-emu2}"; mkdir -p "$OUTDIR"
DUMP="$OUTDIR/emu2_ram.bin"

[ -x "$EMU2" ]                 || { echo "missing emu2 binary at $EMU2 -- build it first"; exit 1; }
[ -f "$LIBDIR/clibs.lib" ]     || { echo "missing $LIBDIR/clibs.lib"; exit 1; }
[ -f "$LIBDIR/cstartcpm.obj" ] || { echo "missing $LIBDIR/cstartcpm.obj"; exit 1; }

echo "== 1. build FHEMU2.CMD =="
"$WCC" -bt=dos -0 -ms -zastd=c99 -DSEG=${SEG}u $INC \
    test/farheap_smalltest.c -fo="$OUTDIR/t.obj"
"$WLINK" format cpm86 op dosseg op quiet op start=_cstart_ op farheap=$FARHEAP_SIZE \
    name "$OUTDIR/FHEMU2.CMD" \
    file "$LIBDIR/cstartcpm.obj" file "$OUTDIR/t.obj" library "$LIBDIR/clibs.lib"

echo "== 2. run under emu2 (grab up to ~1 MB, analyse what we got) =="
rm -f "$DUMP"
OUT=$(EMU2_RAMDUMP="$LIBC/$DUMP" "$EMU2" -P 255 "$OUTDIR/FHEMU2.CMD" 2>/dev/null | tr -d '\r\000')
echo "$OUT"
N=$(printf '%s' "$OUT" | grep -oE 'n=[0-9]+' | head -1 | cut -d= -f2)
case "$OUT" in PASS*) : ;; *) echo "FAIL: guest did not report PASS"; exit 1 ;; esac

echo "== 3. independent RAM-dump content check (oracle OUTSIDE the guest) =="
if [ -n "$N" ]; then
    python3 test/verify_farheap_dump.py "$DUMP" --seg "$SEG" --count "$N"
else
    python3 test/verify_farheap_dump.py "$DUMP" --seg "$SEG"
fi
echo "PASS: emu2 corroborates Unicorn+MAME -- same far-heap algorithm, size scaled to 640 KB TPA"
