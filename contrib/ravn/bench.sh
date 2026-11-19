#!/usr/bin/env bash
#
# bench.sh -- build the Dhrystone 2.1 benchmark with Open Watcom's native
# one-step `owcc -bcpm86` driver and compare each variant against the genuine
# Digital Research C build, which stays the correctness ORACLE and the
# size/speed BASELINE.
#
# owcc-only, NO SEAMS (standing decision @ravn 2026-08-19, see
# tasks/memory/reference_owcc_cpm86_no_seams_softfloat_lib.md): every variant
# builds against owcc's STANDARD CP/M-86 runtime (cstartcpm.obj + clibs.lib) with
# no hand-written port objects and no DR LINK-86 hybrid step.  Dhrystone's self-
# timing block prints "%6.1f", so it references the float runtime even though
# drcify makes that branch dead code (fixed run count, timing disabled); we
# therefore link the one reusable soft-float closure `cpm-soft-float.lib`
# (auto-built by mk-cpm-soft-float-lib.sh) with `-msoft-float` (= wcc -fpc, pure
# software float: the RC759 has no 8087, so never -fpi/-fpi87).
#
# The comparison is apples-to-apples: every variant is built from the SAME
# drcified, deterministic Dhrystone source (fixed run count, timing disabled ->
# identical program output), in the SAME small/8080 memory model DR C uses.  The
# only thing that changes is the Open Watcom optimiser level:
#
#   O0     -O0    optimiser disabled
#   O2     -O2    full optimisation
#
# Speed is reported as INSTRUCTIONS and as estimated 80186 CLOCK CYCLES (the
# "ticks" metric, contrib/ravn/cycles186.py) and, at --mhz (default 6, the RC759
# Piccoline), as an estimated wall-clock time.  The clock figure is an estimate:
# Unicorn is a functional emulator with no timing, so cycles186.py layers an
# iAPX 186 clock table on top of a capstone decode.  It does NOT model the
# prefetch queue or memory wait-states; treat it as "cirka-ish".  For true
# cycle-accurate timing use MAME or PCE against the real RC759 XIOS.
#
# The DR C oracle (pure-drc/dhry.cmd) needs the DRI copyright toolchain, so it is
# not committed.  If present this script compares against it directly; otherwise
# it falls back to the persisted numbers in baseline.json.
#
# Usage:  ./bench.sh [--mhz N]
#
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/cpm86-clib/env.sh"               # owcc/wcc/wasm/wlib on PATH, $WATCOM set
SRC="$HERE/owc-drc/dhry21"                # unmodified Dhrystone 2.1 source
LIB="$HERE/cpm-soft-float.lib"            # reusable soft-float closure
RUNS=200
MHZ=6

while [ $# -gt 0 ]; do
    case "$1" in
        --mhz) MHZ="$2"; shift 2;;
        --mhz=*) MHZ="${1#*=}"; shift;;
        *) echo "usage: $0 [--mhz N]" >&2; exit 2;;
    esac
done

[ -f "$SRC/dhry_1.c" ] || { echo "error: $SRC/dhry_1.c missing" >&2; exit 1; }
# Dhrystone's timing block prints "%6.1f", so it references the float runtime;
# build (once) the reusable soft-float closure if it is not present yet.
[ -f "$LIB" ] || { echo "==> building $(basename "$LIB")"; "$HERE/mk-cpm-soft-float-lib.sh"; }

# build_variant <label> <optflag>  -> writes owc-drc/DHRY-<label>.CMD
build_variant() {
    local label="$1" opt="$2"
    local W; W="$(mktemp -d /tmp/bench.XXXXXX)"
    # Deterministic, drcified Dhrystone (watcom dialect: no NOENUM/NOSTRUCTASSIGN).
    python3 "$HERE/pure-drc/drcify.py" "$SRC" "$W" "$RUNS" watcom >/dev/null
    (
        cd "$W"
        owcc -bcpm86 -march=i186 -mcmodel=s -msoft-float "$opt" \
            -DREG=register \
            dhry_1.c dhry_2.c "$LIB" -o "DHRY.CMD" >build.log 2>&1 \
            || { echo "error: owcc build failed for '$label'" >&2; cat build.log >&2; exit 1; }
        [ -f DHRY.CMD ] || { echo "error: no DHRY.CMD for '$label'" >&2; exit 1; }
        cp DHRY.CMD "$SRC/../DHRY-$label.CMD"
    )
    rm -rf "$W"
}

echo "Building owcc variants from identical drcified Dhrystone ($RUNS runs)..."
build_variant O0 -O0
build_variant O2 -O2
echo

ORACLE="$HERE/pure-drc/dhry.cmd"
if [ -f "$ORACLE" ]; then
    for v in O0 O2; do
        echo "### DR C (oracle) vs owcc $v ###"
        python3 "$HERE/bench.py" compare "$ORACLE" "$HERE/owc-drc/DHRY-$v.CMD" \
            --mhz "$MHZ" --label-candidate "owcc $v"
        echo
    done
else
    echo "(DR C oracle pure-drc/dhry.cmd not present -- checking against baseline.json)"
    echo
    for v in O0 O2; do
        echo "### baseline dhry (DR C) vs owcc $v ###"
        python3 "$HERE/bench.py" baseline check dhry "$HERE/owc-drc/DHRY-$v.CMD" --mhz "$MHZ" \
            || true
        echo
    done
fi
