#!/bin/bash
# build-medium.sh -- compile + link the MEDIUM-model end-to-end smoke test
# (test/mediumtest.c + test/mediumtest_b.c) against the medium CP/M-86 C library
# (lib286/cpm86/{clibm.lib,cstartmm.obj}, produced by `MODEL=m ./build-lib.sh`).
#
# Emits MEDTEST.CMD. Because medium model relies on P_LOAD relocation, verify it
# on genuine CP/M-86 (MAME rc759) -- emu2 does NOT apply the fixups:
#     ../../../.. /scratch/rc759-cmd-toolchain/mame-tests/run-mame-prebuilt.sh \
#         "$(pwd)/MEDTEST.CMD"
# Expect: DONE-SIGNAL word 0x0008 (pass) and "medium clib: 6 far calls, 0 fail"
# + "PASS" on the console.
set -e
cd "$(dirname "$0")"
unset WCC WASM WLIB WLINK
OW="${OW:-$(cd ../../.. && pwd)}"; B="$OW/bld"
WCC="${OWCC_BIN:-$B/cc/i86/osxa64/binbuild/wcc.exe}"
WLINK="${OWLINK_BIN:-$B/wl/osxa64/wlink.exe}"
SRC="$(pwd)"
LIBDIR="$OW/lib286/cpm86"

[ -f "$LIBDIR/clibm.lib" ]   || { echo "missing $LIBDIR/clibm.lib -- run 'MODEL=m ./build-lib.sh' first" >&2; exit 1; }
[ -f "$LIBDIR/cstartmm.obj" ] || { echo "missing $LIBDIR/cstartmm.obj -- run 'MODEL=m ./build-lib.sh' first" >&2; exit 1; }

INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/string/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/watcom/h -i=$B/hdr/dos/h"
# -mm -zm: far code + a per-function *_TEXT segment (Stage B convention).
CFLAGS="-bt=dos -0 -mm -zm -zl -zastd=c99"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
"$WCC" $CFLAGS $INC "$SRC/test/mediumtest.c"   -fo="$WORK/mediumtest.obj"
"$WCC" $CFLAGS $INC "$SRC/test/mediumtest_b.c" -fo="$WORK/mediumtest_b.obj"

# Default packing: -mm -zm already gives each function its own *_TEXT; wlink
# coalesces them into one Code Group Descriptor (each <=64 KB). Do NOT force
# `op packcode=8` here -- that splits mid-function and breaks intra-function
# near branches (E2052); it is only safe for the byte-sized hand-asm oracles.
"$WLINK" format cpm86 op dosseg op quiet op start=_cstart_ \
  name "$SRC/MEDTEST.CMD" \
  file "$LIBDIR/cstartmm.obj" \
  file "$WORK/mediumtest.obj" file "$WORK/mediumtest_b.obj" \
  library "$LIBDIR/clibm.lib"

ls -l "$SRC/MEDTEST.CMD"
echo "built MEDTEST.CMD."

# --- Unicorn oracle: apply the loader fixups + run, assert exact output -------
# The Unicorn runner (contrib/ravn/cpm86run_unicorn.py) is a faithful port of
# the genuine CCP/M-86 load.sup loader: it applies the header-byte-127 / ch_fixrec
# P_LOAD relocation records, so it CAN run a medium .CMD (unlike emu2). This is
# the same consumer the Stage B far-call/far-ptr oracles use.
RUNNER="$OW/contrib/ravn/cpm86run_unicorn.py"
PY="${PYTHON:-python3}"
if [ -f "$RUNNER" ] && "$PY" -c "import unicorn" >/dev/null 2>&1; then
    echo "==> Unicorn oracle:"
    OUT="$("$PY" "$RUNNER" "$SRC/MEDTEST.CMD" 2>/dev/null | tr -d '\r\000')"
    printf '%s\n' "$OUT" | sed 's/^/    /'
    EXP="$(printf 'medium clib: 6 far calls, 0 fail\nPASS')"
    if [ "$OUT" = "$EXP" ]; then
        echo "PASS: medium-model clib runs end-to-end under Unicorn (fixups applied)"
    else
        echo "FAIL: unexpected Unicorn output (medium far-call/clib regression)" >&2
        exit 1
    fi
else
    echo "(skipped Unicorn oracle: $RUNNER or python 'unicorn' module not available)"
    echo "verify on MAME rc759 instead (see header of this script)."
fi
