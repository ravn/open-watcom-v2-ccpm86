#!/bin/sh
# build-compact-farheap.sh -- diagnostic oracle for the COMPACT-model (-mc) far
# heap. Compiles test/compact_farheap_test.c with -mc, links it against the
# INSTALLED clibc.lib + cstartcm.obj with `op farheap=<size>`, dumps the group
# descriptors, and runs it under the Unicorn oracle.
#
# Prints a line "C <c1c2c3c4> L.. H.. S.. D.. M.." -- expect "C 1111 ..." on a
# correct build. "C 0000 L0001 H0000 S0000" means the near-heap arena clobbered
# the base page (see the test header + reference_cpm86_p_load_fixups.md sec 8).
#
# Prereq: MODEL=c ./build-lib.sh  (installs lib286/cpm86/{clibc.lib,cstartcm.obj}).
set -e
cd "$(dirname "$0")"
unset WCC WASM WLIB WLINK
OW="${OW:-$(cd ../../.. && pwd)}"; B="$OW/bld"
WCC="${OWCC_BIN:-$B/cc/i86/osxa64/binbuild/wcc.exe}"
WLINK="${OWLINK_BIN:-$B/wl/osxa64/wlink.exe}"
INC="-i=$B/clib/h -i=$B/watcom/h -i=$B/hdr/dos/h -i=$B/clib/heap/h"
LIBDIR="$OW/lib286/cpm86"
FARHEAP_SIZE="${FARHEAP_SIZE:-0x30000}"
OUTDIR="${OUTDIR:-build-compact-farheap}"; mkdir -p "$OUTDIR"

[ -f "$LIBDIR/clibc.lib" ]    || { echo "missing $LIBDIR/clibc.lib -- run 'MODEL=c ./build-lib.sh' first"; exit 1; }
[ -f "$LIBDIR/cstartcm.obj" ] || { echo "missing $LIBDIR/cstartcm.obj -- run 'MODEL=c ./build-lib.sh' first"; exit 1; }

echo "==> compile -mc"
"$WCC" -bt=dos -0 -mc -zastd=c99 $INC test/compact_farheap_test.c -fo="$OUTDIR/t.obj"

echo "==> link (format cpm86, op farheap=$FARHEAP_SIZE)"
"$WLINK" format cpm86 op dosseg op quiet op start=_cstart_ op farheap=$FARHEAP_SIZE \
    op map="$OUTDIR/t.map" name "$OUTDIR/CFHT.CMD" \
    file "$LIBDIR/cstartcm.obj" file "$OUTDIR/t.obj" library "$LIBDIR/clibc.lib"

echo "==> group descriptors:"
grep -E 'Group|Segment|DGROUP|FAR_DATA|EXTRA|BEGDATA' "$OUTDIR/t.map" | head -20 || true

echo "==> static .CMD check (far-heap/far-data overlap + base-page arena):"
python3 ../cmd_check.py --map "$OUTDIR/t.map" "$OUTDIR/CFHT.CMD" || \
    echo "   (static check FAILED -- see [F1]/[F2] above; this is the open design conflict)"

echo "==> run under Unicorn (expect: C 1111111 ...):"
python3 ../cpm86run_unicorn.py "$OUTDIR/CFHT.CMD"
