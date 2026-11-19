#!/bin/bash
# Reproducible oracle: SMALL-model CP/M-86 program that moves 96 KB of buffers
# OFF DGROUP via explicit _fmalloc(), resolved by the INSTALLED clibs.lib.
#
# This differs from build-farheap.sh (which hand-picks individual clib objects):
# here we link against the shipped lib286/cpm86/clibs.lib to prove that
# build-lib.sh now ARCHIVES the far-heap members (_fmalloc/_ffree/farheap.c ...)
# into the small-model library -- so any program can call _fmalloc without a
# bespoke object list.  That archive gap is what previously left plain far-heap
# symbols undefined; test/farheap_smalltest.c is the regression guard.
#
# WHY small model + explicit _fmalloc (and NOT transparent -mc compact malloc):
#   see the header of test/farheap_smalltest.c and build-lib.sh's MODEL=c note.
#   Short version: compact model makes clib globals FAR, which wlink emits as a
#   second type=2 group the CP/M-86 loader can't place -> __heap_enabled reads 0
#   -> malloc() NULL.  Small model keeps clib globals near; the far HEAP still
#   works when asked for by name.  Verified PASS under cpm86run_unicorn.py.
set -e
unset WCC WASM WLIB WLINK
cd "$(dirname "$0")"
OW="${OW:-$(cd "$(dirname "$0")/../../.." && pwd)}"; B="$OW/bld"
WCC="${OWCC_BIN:-$B/cc/i86/osxa64/binbuild/wcc.exe}"
WLINK="${OWLINK_BIN:-$B/wl/osxa64/wlink.exe}"
LIBDIR="$OW/lib286/cpm86"
INC="-i=$B/clib/h -i=$B/watcom/h -i=$B/hdr/dos/h -i=$B/clib/heap/h"

FARHEAP_SIZE=0xF0000                 # ~960 KB ceiling; grab up to ~1 MB if RAM has it
SEG=${SEG:-16384}                    # VARIABLE segment size (<= 64 KB Watcom cap)
OUTDIR="${OUTDIR:-build-farheap-small}"; mkdir -p "$OUTDIR"

[ -f "$LIBDIR/clibs.lib" ]     || { echo "missing $LIBDIR/clibs.lib -- run ./build-lib.sh s first"; exit 1; }
[ -f "$LIBDIR/cstartcpm.obj" ] || { echo "missing $LIBDIR/cstartcpm.obj -- run ./build-lib.sh s first"; exit 1; }

"$WCC" -bt=dos -0 -ms -zastd=c99 -DSEG=${SEG}u $INC test/farheap_smalltest.c -fo="$OUTDIR/t.obj"
"$WLINK" format cpm86 op dosseg op quiet op start=_cstart_ op farheap=$FARHEAP_SIZE \
    name "$OUTDIR/FHSMALL.CMD" \
    file "$LIBDIR/cstartcpm.obj" file "$OUTDIR/t.obj" library "$LIBDIR/clibs.lib"

echo "=== .CMD group descriptors (expect ONE type=2 + ONE type=3) ==="
python3 - "$OUTDIR/FHSMALL.CMD" <<'PY'
import sys
d=open(sys.argv[1],'rb').read(128)
for i in range(0,128,9):
    t=d[i]
    if t==0: break
    v=[int.from_bytes(d[i+1+2*k:i+3+2*k],'little') for k in range(4)]
    print(f'  type={t} len={v[0]:#06x} min={v[2]:#06x} max={v[3]:#06x}')
PY
echo "=== Unicorn run (grab up to ~1 MB, analyse what we got) ==="
DUMP="$OUTDIR/ram.bin"
OUT=$(python3 ../cpm86run_unicorn.py --dump "$DUMP" "$OUTDIR/FHSMALL.CMD" 2>/dev/null | tr -d '\r\000')
echo "$OUT"
N=$(printf '%s' "$OUT" | grep -oE 'n=[0-9]+' | head -1 | cut -d= -f2)

echo "=== Independent RAM-dump content check (oracle OUTSIDE the guest) ==="
if [ -n "$N" ]; then
    python3 test/verify_farheap_dump.py "$DUMP" --seg "$SEG" --count "$N"
else
    python3 test/verify_farheap_dump.py "$DUMP" --seg "$SEG"
fi
