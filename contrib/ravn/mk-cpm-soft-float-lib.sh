#!/bin/bash
# Build `cpm-soft-float.lib` -- the ONE reusable Open-Watcom soft-float closure
# for `owcc -bcpm86` programs that print/scan floating point.
#
# DESIGN (standing decision @ravn 2026-08-19, see
# tasks/memory/reference_owcc_cpm86_no_seams_softfloat_lib.md):
#   * benchmarks build against owcc's STANDARD CP/M-86 runtime -- cstartcpm.obj
#     + lib286/cpm86/clibs.lib -- with NO hand-written port seams in either the
#     library or the individual test sources;
#   * a float-using benchmark adds exactly this one library:
#       owcc -bcpm86 -mcmodel=s -msoft-float prog.c cpm-soft-float.lib -o P.CMD
#     (-msoft-float == wcc -fpc: pure __FDxemu soft-float, no 8087 -- the RC759
#     has none, so never -fpi/-fpi87);
#   * a NON-float benchmark links nothing extra: no bloat, no dependency on this
#     library at all.
#
# Why a library is needed at all: owcc's stock cpm86 clibs.lib carries the
# __FDx soft-float ARITHMETIC but not the %e/%f/%g FORMATTER, the transcendental
# libm, the `_fltused_` marker, or the "no-8087" chip markers -- and its startup
# (cstartcpm -> __CommonInit, built WITHOUT -DCOMMONINIT_EFG) never installs the
# real formatter, so a bare float printf emits nothing.  This library supplies
# all of that AND overrides __CommonInit with the -DCOMMONINIT_EFG build so the
# genuine _EFG_Format is installed -- and it is pulled in ONLY when the program
# links this library, keeping non-float programs lean.  It contains only genuine
# Watcom clib objects (no port seams).

set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/cpm86-clib/env.sh"           # owcc/wcc/wasm/wlib on PATH, $WATCOM set
B="$WATCOM/bld"
OUT="${1:-$HERE/cpm-soft-float.lib}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h \
     -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"     # exactly how the stock clib is built
cw() { wcc $CLIB $INC "$1" -fo="$2" >/dev/null; }

# --- (1) startup override: __CommonInit that DOES install the real EFG fmt -----
# Same canonical source as the stock runtime, but compiled with -DCOMMONINIT_EFG
# so __CommonInit() also calls __setEFGfmt().  Because this object is only linked
# when a program pulls this library, non-float programs keep the lean no-EFG
# __CommonInit from clibs.lib.
wcc $CLIB -DCOMMONINIT_EFG $INC "$B/clib/_cpm/c/cominit.c" -fo=cominit.obj >/dev/null

# --- (2) the genuine %e/%f/%g formatter installer + its support seams ----------
cw "$B/clib/streamio/c/setefg.c"   setefg.obj    # __setEFGfmt(): install real fmt
cw "$B/clib/startup/c/seterrno.c"  seterrno.obj  # __set_EDOM_/__set_ERANGE_
cw "$B/clib/startup/c/rtcntrl.c"   rtcntrl.obj   # __get_rt_control_ptr_
cw "$B/clib/streamio/c/iobaddr.c"  iobaddr.obj   # __get_std_stream_ (_matherr)
cw "$B/clib/char/c/istable.c"      istable.obj   # __IsTable for strtod/__cnvs2d

# --- (3) the "no 8087" chip markers + the _fltused_ marker --------------------
# fpsoftstub.asm sets __8087/__real87/... = 0 (statically "no coprocessor"); it
# is the ONE allowed port asm -- a pure constant table, not a runtime seam.
wasm -q -ms -0 -i="$B/watcom/h" -i="$B/comp_cfg/h" \
     "$HERE/watcom-cpm86-libc/port/fpsoftstub.asm" -fo=fpsoftstub.obj >/dev/null
cp "$B/clib/startup/library/msdos.086/ms/fltused.obj" ./fltused.obj  # _fltused_

# --- assemble the library (fresh archive: member order must let setefg's
# _EFG_Format_/__cnvs2d_ refs resolve within the single library) --------------
wlib -q -b -n "$OUT" \
     cominit.obj setefg.obj fltused.obj fpsoftstub.obj \
     seterrno.obj rtcntrl.obj iobaddr.obj istable.obj >/dev/null

# (4) merge the pure-8086 (msdos.086) clib components that carry _EFG_Format_,
# __cnvs2d_ and the FP-conversion/support code -- consistent with our -0 build.
for L in cgsupp fpu math convert; do
    wlib -q -b "$OUT" +"$B/clib/$L/library/msdos.086/ms/clibs.lib" >/dev/null
done

# (5) merge the transcendental + 80-bit-cvt libm.  Watcom shipped these only in
# the msdos.286 mathlib; they are software-float, INT-21h-free and carry no
# 286-only opcodes, so they run on the RC759's 80186 (kept honest by the
# assert_no_286 gate in the whetstone build).
MATH286="$WORK/math286.lib"
wlib -q -b -n "$MATH286" "$B/mathlib/library/msdos.286/ms"/*.obj >/dev/null
wlib -q -b "$OUT" +"$MATH286" >/dev/null

echo "built $OUT ($(wc -c < "$OUT") bytes)"
