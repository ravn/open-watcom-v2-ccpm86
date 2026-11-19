#!/bin/bash
# build-stackguard.sh -- build + demo the crt0 stack-overflow canary (stkfree()).
#
# Builds a diagnostic small-model clib with a chosen stack size and the 0xA5
# sentinel fill, compiles test/stackguard_test.c against it, links a CP/M-86
# .CMD, and (unless STKG_NORUN=1) runs it under emu2. Run it twice --
#   WC_STACK_BYTES=512  bash build-stackguard.sh
#   WC_STACK_BYTES=2048 bash build-stackguard.sh
# -- to see the deeper max-recursion-depth / larger headroom the bigger stack
# buys. The canary guard stops before a real overflow, so neither run crashes.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

STK="${WC_STACK_BYTES:-512}"
OUTDIR="build-stackguard"
# 1. diagnostic clib with the 0xA5 sentinel (precise low-water counts).
OUTDIR="$OUTDIR" WC_STACK_BYTES="$STK" WC_STACK_FILL=0A5h bash build-lib.sh >/tmp/stkg_lib.log 2>&1 \
    || { echo "clib build failed:"; tail -20 /tmp/stkg_lib.log; exit 1; }

OW="${OW:-$(cd ../../.. && pwd)}"; B="$OW/bld"
WCC="${OWCC_BIN:-$B/cc/i86/osxa64/binbuild/wcc.exe}"
WLINK="${OWLINK_BIN:-$B/wl/osxa64/wlink.exe}"
INC="-i=$B/watcom/h"

cd "$OUTDIR"
"$WCC" -bt=dos -0 -ms -zastd=c99 $INC ../test/stackguard_test.c -fo=stkg.obj
# crt0.obj provides _cstart_ + stkfree(); the clib resolves wc_heap_init /
# __CommonInit; bdos_conout is an inline pragma (no link needed).
"$WLINK" format cpm86 op dosseg,quiet,nodefaultlibs name STKGUARD.CMD \
    file crt0.obj file stkg.obj library clibcpm.lib >/tmp/stkg_link.log 2>&1 \
    || { echo "link failed:"; cat /tmp/stkg_link.log; exit 1; }
echo "built $OUTDIR/STKGUARD.CMD (stack=$STK, fill=0xA5): $(stat -f%z STKGUARD.CMD 2>/dev/null || stat -c%s STKGUARD.CMD) bytes"

if [ "${STKG_NORUN:-0}" != "1" ]; then
    EMU2="${EMU2:-/Users/ravn/z80/emu2-cpm86/emu2}"
    echo "--- run (stack=$STK) ---"
    ( printf '\n'; sleep 1 ) | "$EMU2" -P 255 STKGUARD.CMD 2>&1 | sed -n '1,12p'
fi
