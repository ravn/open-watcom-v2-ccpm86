#!/bin/bash
# Reproducible proof that the CP/M-86 BDOS T_SECONDS timer (fn 155) works and is
# a DETERMINISTIC, work-proportional clock under both emulator oracles:
#   * emu2 (ravn/emu2-cpm86)          -- instruction-count clock, env EMU2_CPM86_CLOCK_HZ
#   * cpm86run_unicorn.py             -- code-byte clock (needs --count), env CPM86_CLOCK_HZ
#
# The test (test/tsecs_test.c) reads T_SECONDS, spins a fixed loop, reads it
# again, and prints ELAPSED whole seconds -- the same pattern stdcbench uses.
# Gate: ELAPSED > 0 (PASS), the value is identical across two runs (deterministic),
# and halving CLOCK_HZ doubles it (the clock scales as seconds = work / CLOCK_HZ).
set -e
cd "$(dirname "$0")"
OW="${OW:-$(cd "$(dirname "$0")/../../.." && pwd)}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
EMU2="${EMU2:-/Users/ravn/z80/emu2-cpm86/emu2}"
RUNNER="$OW/contrib/ravn/cpm86run_unicorn.py"
SPINS="${SPINS:-4000000}"
OUTDIR="${OUTDIR:-build-tsecs}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"
USER="-bt=dos -0 -ms -zl -zastd=c99"

cw() { "$WCC" $CLIB $INC "$1" -fo="$2"; }

# printf formatter + integer converters (reused Watcom clib, unchanged)
cw "$B/clib/streamio/c/prtf.c"     prtf.obj
cw "$B/clib/streamio/c/noefgfmt.c" noefgfmt.obj
cw "$B/clib/string/c/strupr.c"     strupr.obj
cw "$B/clib/string/c/strlen.c"     strlen.obj
cw "$B/clib/convert/c/itoa.c"      itoa.obj
cw "$B/clib/convert/c/ltoa.c"      ltoa.obj
cw "$B/clib/convert/c/lltoa.c"     lltoa.obj
cw "$B/clib/convert/c/alphabet.c"  alphabet.obj
cw "$B/clib/mbyte/c/wctomb.c"      wctomb.obj
cw "$B/clib/heap/c/amblksiz.c"     amblksiz.obj

# 32-bit multiply/divide helpers (the fold() math needs __U4M)
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo=i4m.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo=i4d.obj

# our thin CP/M-86 seam + the test
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
"$WCC" $USER $INC -DCOMMONINIT_NOSTDIO "$SRC/port/cominit.c" -fo=cominit.obj
cw "$SRC/port/cprintf.c"           cprintf.obj
"$WCC" $USER $INC "$SRC/port/lowlevel.c" -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/stubs.c"    -fo=stubs.obj
"$WCC" $USER $INC -DSPINS=${SPINS}ul "$SRC/test/tsecs_test.c" -fo=tsecs.obj

"$WLINK" format cpm86 op dosseg op quiet name tsecs.cmd \
  file crt0.obj file tsecs.obj file cprintf.obj file lowlevel.obj \
  file cominit.obj \
  file prtf.obj file noefgfmt.obj file strupr.obj file itoa.obj file ltoa.obj \
  file lltoa.obj file alphabet.obj file strlen.obj file wctomb.obj \
  file amblksiz.obj \
  file stubs.obj file i4m.obj file i4d.obj

# purity gate: zero INT 21h (DOS), at least one BDOS call
python3 - tsecs.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0, "FAIL: DOS INT 21h present in image!"
assert bdos>0, "FAIL: no BDOS call in image!"
PY

emu2_elapsed() { "$EMU2" -P 255 tsecs.cmd 2>/dev/null | tr -d '\r' | awk '/^ELAPSED/{print $2}'; }
uni_elapsed()  { python3 "$RUNNER" --count tsecs.cmd 2>/dev/null | tr -d '\r\000' | awk '/^ELAPSED/{print $2}'; }

echo "== emu2 =="
E1=$(EMU2_CPM86_CLOCK_HZ=300000 emu2_elapsed)
E2=$(EMU2_CPM86_CLOCK_HZ=300000 emu2_elapsed)
E3=$(EMU2_CPM86_CLOCK_HZ=150000 emu2_elapsed)
echo "  ELAPSED @300000 = $E1 (run2 $E2)   @150000 = $E3"

echo "== unicorn (--count) =="
U1=$(CPM86_CLOCK_HZ=300000 uni_elapsed)
U2=$(CPM86_CLOCK_HZ=300000 uni_elapsed)
U3=$(CPM86_CLOCK_HZ=150000 uni_elapsed)
echo "  ELAPSED @300000 = $U1 (run2 $U2)   @150000 = $U3"

fail=0
for pair in "emu2:$E1:$E2:$E3" "unicorn:$U1:$U2:$U3"; do
  IFS=: read name a b c <<<"$pair"
  [ -n "$a" ] && [ "$a" -gt 0 ] 2>/dev/null || { echo "FAIL($name): ELAPSED not > 0"; fail=1; continue; }
  [ "$a" = "$b" ] || { echo "FAIL($name): not deterministic ($a != $b)"; fail=1; }
  # halving CLOCK_HZ should roughly double the elapsed count (allow +/-1 s of quantisation)
  want=$((a*2)); lo=$((want-2)); hi=$((want+2))
  [ "$c" -ge "$lo" ] && [ "$c" -le "$hi" ] || { echo "FAIL($name): halved HZ did not ~double ($c vs ~$want)"; fail=1; }
  [ $fail -eq 0 ] && echo "PASS($name): deterministic + scales with CLOCK_HZ"
done
exit $fail
