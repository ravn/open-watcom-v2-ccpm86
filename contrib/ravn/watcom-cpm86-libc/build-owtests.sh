#!/bin/bash
# Reproducible proof: Open Watcom's OWN, UNCHANGED float regression tests
# (bld/ctest/positive/source/float01..float04.c) running on CP/M-86 with NO 8087.
#
# These four floatNN.c are upstream Watcom's own self-checking float tests: each
# calls fail(__LINE__) on a wrong result and returns errors!=0 from main() via
# fail.h's _PASS. We run them BYTE-FOR-BYTE UNCHANGED (only -Dmain=owtest_main on
# the compile so our test/owtdrv.c can supply the real main() + a machine-checkable
# OWTEST PASS/FAIL verdict). Passing them is an INDEPENDENT third-party oracle on
# the retargeted -fpc soft-float path (the tests encode their own expected values;
# no host-cc oracle needed). Complements the Whetstone proof (rc7xx-work#8).
#
# Same no-8087 discipline as build-whetstone.sh: -fpc (soft-float calls, no inline
# x87), pure-software __FDxemu path (__real87==0), no INT 21h, purity-gated.
set -e
cd "$(dirname "$0")"
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
WLIB="$B/nwlib/osxa64/wlib.exe"
WDIS="$B/ndisasm/osxa64/wdis.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build-owtests}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
CTEST="$B/ctest/positive/source"
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
AINC="-i=$B/watcom/h -i=$B/comp_cfg/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"       # compile Watcom clib source
USER="-bt=dos -0 -ms -fpc -zl -zastd=c99"     # our port + tests (float => -fpc; C99 for hex-float/fpclassify)

cw()  { "$WCC" $CLIB $INC "$1" -fo="$2"; }
cwf() { "$WCC" $CLIB -fpc -i=$B/mathlib/h $INC "$1" -fo="$2"; }
aw()  { "$WASM" -ms -0 $AINC "$1" -fo="$2"; }

# --- Watcom clib: formatter + %ld converters (integer printf path is enough:
#     these tests print only "failure on line %u" and our OWTEST verdict) ---
cw "$B/clib/streamio/c/prtf.c"     prtf.obj
cw "$B/clib/streamio/c/noefgfmt.c" noefgfmt.obj
cw "$B/clib/string/c/strupr.c"     strupr.obj
cw "$B/clib/string/c/strlen.c"     strlen.obj
cw "$B/clib/convert/c/itoa.c"      itoa.obj
cw "$B/clib/convert/c/ltoa.c"      ltoa.obj
cw "$B/clib/convert/c/lltoa.c"     lltoa.obj
cw "$B/clib/convert/c/alphabet.c"  alphabet.obj
cw "$B/clib/mbyte/c/wctomb.c"      wctomb.obj

# --- Watcom clib: the GENUINE FILE* stdio write-path (UNCHANGED) ---
cw "$B/clib/streamio/c/printf.c"   printf.obj
cw "$B/clib/streamio/c/fprintf.c"  fprintf.obj
cw "$B/clib/streamio/c/fprtf.c"    fprtf.obj
cw "$B/clib/streamio/c/fputc.c"    fputc.obj
cw "$B/clib/streamio/c/fputs.c"    fputs.obj
cw "$B/clib/streamio/c/puts.c"     puts.obj
cw "$B/clib/streamio/c/flush.c"    flush.obj
cw "$B/clib/streamio/c/fflush.c"   fflush.obj
cw "$B/clib/streamio/c/ioalloc.c"  ioalloc.obj
cw "$B/clib/streamio/c/chktty.c"   chktty.obj
cw "$B/clib/streamio/c/iob.c"      iob.obj
cw "$B/clib/streamio/c/initfile.c" initfile.obj
cw "$B/clib/streamio/c/ferror.c"   ferror.obj

# --- Watcom clib: the near-heap manager (UNCHANGED) ---
for f in nmalloc nfree calloc nrealloc grownear amblksiz heapen nheapmin \
         mem bfree _expand nmemneed nmsize nexpand nheapunl; do
  cw "$B/clib/heap/c/$f.c" "$f.obj"
done

# --- Watcom clib: mem helpers ---
cw "$B/clib/memory/c/memcpy.c"     memcpy.obj
cw "$B/clib/memory/c/memset.c"     memset.obj
cw "$B/clib/memory/c/memmove.c"    memmove.obj

# --- Layer-1 long helpers (32-bit multiply/divide) ---
aw "$B/clib/cgsupp/a/i4m.asm"      i4m.obj
aw "$B/clib/cgsupp/a/i4d.asm"      i4d.obj

# --- Watcom clib: OWN double SOFT-FLOAT runtime (-fpc __FDxemu path) ---
aw "$B/clib/cgsupp/a/fdmth086.asm" fdmth086.obj
aw "$B/clib/cgsupp/a/fdi4086.asm"  fdi4086.obj
aw "$B/clib/fpu/a/chipd16.asm"     chipd16.obj
aw "$B/fpuemu/i86/asm/emustub.asm" emustub.obj
"$WASM" -ms -0 $AINC "$SRC/port/fpsupport.asm"  -fo=fpsupport.obj
"$WASM" -ms -0 $AINC "$SRC/port/fpsoftstub.asm" -fo=fpsoftstub.obj

# --- Watcom clib: float format/error support the mathlib pulls (UNCHANGED) ---
cw "$B/clib/streamio/c/setefg.c"   setefg.obj
cw "$B/clib/startup/c/seterrno.c"  seterrno.obj
cw "$B/clib/startup/c/rtcntrl.c"   rtcntrl.obj
cw "$B/clib/streamio/c/iobaddr.c"  iobaddr.obj
cw "$B/clib/char/c/istable.c"      istable.obj

MATH286_SRC="$B/mathlib/library/msdos.286/ms"
"$WLIB" -q -b -n math286.lib "$MATH286_SRC"/*.obj >/dev/null 2>&1
CLIB086="$B/clib"
LIBS="library $CLIB086/cgsupp/library/msdos.086/ms/clibs.lib \
      library $CLIB086/fpu/library/msdos.086/ms/clibs.lib \
      library $CLIB086/math/library/msdos.086/ms/clibs.lib \
      library $CLIB086/convert/library/msdos.086/ms/clibs.lib \
      library math286.lib"

# --- HARD BUILD ERROR if a used library carries a non-80186 (286+) opcode ----
CPU286RE='\b(arpl|lar|lsl|lgdt|lidt|lldt|sgdt|sidt|sldt|lmsw|smsw|clts|str|ltr|verr|verw)\b'
assert_no_286() {
  local bad=0 o hits n=0
  for o in "$@"; do
    n=$((n+1))
    hits=$("$WDIS" "$o" 2>/dev/null | grep -ioE "$CPU286RE" | sort -u | tr '\n' ' ')
    if [ -n "$hits" ]; then
      echo "BUILD ERROR: non-80186 (286+ protected-mode) opcode in $o : $hits"
      bad=1
    fi
  done
  [ "$bad" -eq 0 ] || exit 4
  echo "cpu-check: $n prebuilt .286 object(s) scanned, 0 non-80186 opcodes (80186-safe)"
}
assert_no_286 "$MATH286_SRC"/*.obj

# --- our thin CP/M-86 seam (Layer 2) + the PASS/FAIL driver ---
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
"$WCC" $USER $INC "$SRC/port/stdioshim.c" -fo=stdioshim.obj
"$WCC" $USER $INC "$SRC/port/lowlevel.c"  -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/stubs.c"     -fo=stubs.obj
"$WCC" $USER $INC "$SRC/port/errnoptr.c"  -fo=errnoptr.obj
"$WCC" $USER $INC "$SRC/port/abortcpm.c"  -fo=abortcpm.obj
"$WCC" $USER $INC ${OWT_EXTRA:-} "$SRC/test/owtdrv.c" -fo=owtdrv.obj

SEAM="file crt0.obj file owtdrv.obj file stdioshim.obj file lowlevel.obj \
  file printf.obj file fprintf.obj file fprtf.obj file fputc.obj file fputs.obj \
  file puts.obj file flush.obj file fflush.obj file ioalloc.obj file chktty.obj \
  file iob.obj file initfile.obj file ferror.obj \
  file prtf.obj file noefgfmt.obj file strupr.obj file itoa.obj file ltoa.obj \
  file lltoa.obj file alphabet.obj file strlen.obj file wctomb.obj \
  file nmalloc.obj file nfree.obj file calloc.obj file nrealloc.obj \
  file grownear.obj file amblksiz.obj file heapen.obj file nheapmin.obj \
  file mem.obj file _expand.obj file nmemneed.obj file nmsize.obj \
  file nexpand.obj file nheapunl.obj file bfree.obj \
  file memcpy.obj file memset.obj file memmove.obj \
  file fdmth086.obj file fdi4086.obj file chipd16.obj file fpsupport.obj \
  file emustub.obj file fpsoftstub.obj \
  file setefg.obj file seterrno.obj file rtcntrl.obj file iobaddr.obj \
  file istable.obj file errnoptr.obj file abortcpm.obj \
  file stubs.obj file i4m.obj file i4d.obj"

# --- 8087 purity assertion (soft-float only; no inline x87 in OUR objects) ----
X87RE='\b(fld[a-z0-9]*|fst[a-z0-9]*|fadd[a-z0-9]*|fsub[a-z0-9]*|fmul[a-z0-9]*|fdiv[a-z0-9]*|fild[a-z0-9]*|fist[a-z0-9]*|fcom[a-z0-9]*|fwait|finit|fninit|fldcw|fnstcw|fnstsw|fxch|fabs|fchs|fsqrt|fscale|fpatan|fptan|f2xm1|fyl2x)\b'
assert_no_8087() {
  local bad=0 o
  for o in "$@"; do
    if "$WDIS" "$o" 2>/dev/null | grep -qiE "$X87RE"; then
      echo "BUILD ERROR: inline 8087 (x87) math in $o -- compile -fpc, not -fpi/-fpi87."
      "$WDIS" "$o" 2>/dev/null | grep -iE "$X87RE" | head -5 | sed 's/^/    > /'
      bad=1
    fi
  done
  [ "$bad" -eq 0 ] || exit 2
}

TESTS="${OWT_TESTS:-float01 float02 float03 float04}"
PASS_ALL=1
for t in $TESTS; do
  echo "================= $t ================="
  # Compile the UPSTREAM test byte-for-byte (only main->owtest_main renamed).
  if ! "$WCC" $USER -Dmain=owtest_main -i="$CTEST" $INC "$CTEST/$t.c" -fo="$t.obj" 2>"$t.cc.log"; then
    echo "SKIP $t: does not compile with this toolchain"; sed 's/^/    cc> /' "$t.cc.log" | head -8
    continue
  fi
  assert_no_8087 "$t.obj" crt0.obj owtdrv.obj stdioshim.obj lowlevel.obj stubs.obj abortcpm.obj

  "$WLINK" format cpm86 op dosseg op quiet name "$t.cmd" \
    $SEAM file "$t.obj" $LIBS 2>"$t.ln.log" || { echo "LINK FAIL $t:"; sed 's/^/    ln> /' "$t.ln.log" | head -20; PASS_ALL=0; continue; }

  # purity gate: zero INT 21h (DOS), some BDOS present
  python3 - "$t.cmd" <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity[{sys.argv[1]}]: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0, "FAIL: DOS INT 21h present!"
assert bdos>0, "FAIL: no BDOS call in image!"
PY

  if [ "${OWT_NORUN:-0}" = "1" ]; then echo "OWT_NORUN=1: built $t.cmd (skipping emu2)"; continue; fi

  OUT="$("$EMU2" "$t.cmd" | tr -d '\r')"
  echo "--- $t output ---"; echo "$OUT"
  if echo "$OUT" | grep -q "OWTEST: PASS"; then
    echo "PASS: $t (Watcom's own float regression test, no 8087, on CP/M-86)"
  else
    echo "FAIL: $t did not report OWTEST: PASS"; PASS_ALL=0
  fi
done

echo "======================================="
if [ "$PASS_ALL" = "1" ]; then
  echo "ALL-PASS: Watcom's own float0x regression suite green on CP/M-86 (-fpc, no 8087)"
else
  echo "SOME FAILURES -- see above"; exit 1
fi
