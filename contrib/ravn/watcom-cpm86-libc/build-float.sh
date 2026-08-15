#!/bin/bash
# Reproducible proof: Open Watcom's OWN, UNCHANGED double SOFT-FLOAT running on
# CP/M-86 with NO 8087 -- the rc7xx-work #8 milestone.
#
# The target (RC759) has no 8087 coprocessor, so we compile float with -fpc
# ("floating-point calls"): every double op becomes a CALL to Watcom's __FDx
# runtime (__FDA/__FDS/__FDM/__FDD arithmetic, __FDI4 double->long), NOT inline
# 8087 ESC opcodes. Those __FDx routines dispatch at runtime on __real87 (a
# cstart byte that is 0 => "no 8087") and take their PURE-SOFTWARE __FDxemu path.
# Result: no 8087 instructions are ever executed, and -- unlike the -fpi
# emulator route -- there is NO interrupt-vector install, NO INT 0x34-0x3D traps,
# and NO INT 21h anywhere. The purity gate is green by construction.
#
# 8087 HARDWARE support (the -fpi87 inline path and the -fpi trap-emulator with
# its segment-0 IVT install, drafted in port/emu87cpm.asm + docs/
# FLOAT_8087_EMULATOR.md) is deliberately NOT built here: we have no 8087 machine
# to verify it on. It is left for someone with such hardware -- see
# docs/8087_HARDWARE_SUPPORT_DEFERRED.md.
#
# Resolved ONLY by our thin Layer-2 seams (the Watcom clib proper is unchanged):
#   port/stdioshim.c : __qwrite (BDOS C_WRITE) + isatty
#   port/lowlevel.c  : arena heap (the FILE buffer + %f scratch come from malloc)
#   port/stubs.c     : __InitFiles/__full_io_exit stubs
set -e
cd "$(dirname "$0")"
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
WDIS="$B/ndisasm/osxa64/wdis.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build-float}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
AINC="-i=$B/watcom/h -i=$B/comp_cfg/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"       # compile Watcom clib source
USER="-bt=dos -0 -ms -fpc -zl"                # our port + test (float => -fpc)

cw() { "$WCC" $CLIB $INC "$1" -fo="$2"; }      # compile a Watcom clib source
aw() { "$WASM" -ms -0 $AINC "$1" -fo="$2"; }   # assemble a Watcom clib asm source

# --- Watcom clib: the __prtf formatter + %ld converters (proven stdio path) ---
cw "$B/clib/streamio/c/prtf.c"     prtf.obj
cw "$B/clib/streamio/c/noefgfmt.c" noefgfmt.obj   # %f falls back to stub for now
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

# --- Watcom clib: the OWN double SOFT-FLOAT runtime (-fpc __FDxemu path) ---
aw "$B/clib/cgsupp/a/fdmth086.asm" fdmth086.obj   # __FDA/__FDS/__FDM/__FDD (+ emu)
aw "$B/clib/cgsupp/a/fdi4086.asm"  fdi4086.obj     # __FDI4  double -> long
aw "$B/clib/fpu/a/chipd16.asm"     chipd16.obj      # __fdiv_m64r software divider
aw "$B/fpuemu/i86/asm/emustub.asm" emustub.obj       # FIxRQQ stubs + no-op emu init
"$WASM" -ms -0 $AINC "$SRC/port/fpsupport.asm" -fo=fpsupport.obj  # F8Over/Under/DivZero
"$WASM" -ms -0 $AINC "$SRC/port/fpsoftstub.asm" -fo=fpsoftstub.obj  # __real87=0 etc.

# --- our thin CP/M-86 seam (Layer 2) + the float test ---
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
"$WCC" $USER $INC "$SRC/port/stdioshim.c" -fo=stdioshim.obj
"$WCC" $USER $INC "$SRC/port/lowlevel.c"  -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/stubs.c"     -fo=stubs.obj
"$WCC" $USER $INC "$SRC/test/floattest.c" -fo=floattest.obj

# --- anti-constant-fold tripwire ---------------------------------------------
# The whole point of #8 is to exercise Watcom's RUNTIME soft-float. If the test's
# operands are compile-time constants the optimizer folds the arithmetic away and
# emits ZERO __FDx calls -- the oracle would still print the right numbers but the
# runtime path we claim to prove is never taken (this bit us once: volatile
# operands are what force the calls). Fail loudly unless real __FDx calls survive.
NFD=$("$WDIS" floattest.obj 2>/dev/null | grep -ciE 'call[[:space:]]+__FD[A-Z0-9]+')
if [ "${NFD:-0}" -lt 1 ]; then
  echo "BUILD ERROR: floattest.obj contains no runtime __FDx soft-float calls --"
  echo "  the float arithmetic was constant-folded, so this run would NOT prove the"
  echo "  runtime soft-float path. Keep the operands 'volatile' in test/floattest.c."
  exit 3
fi
echo "soft-float: $NFD runtime __FDx call site(s) in floattest.obj (not folded)"

# --- link a CP/M-86 .CMD ---
"$WLINK" format cpm86 op dosseg op quiet name floattest.cmd \
  file crt0.obj file floattest.obj file stdioshim.obj file lowlevel.obj \
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
  file stubs.obj file i4m.obj file i4d.obj

# --- HARD BUILD ERROR if any 8087 math ends up in code we generate -----------
# The target has no 8087, so this build must be pure soft-float. Any inline x87
# instruction (fld/fadd/fstp/fwait/...) means the compiler was (mis)invoked with
# -fpi/-fpi87 instead of -fpc -> fail the build. We scan the OBJECTS the compiler
# emits for us (test + our port seams). We deliberately do NOT scan the Watcom
# __FDx soft-float library (fdmth086/chipd16): those fuse a hardware __FDx87
# branch and a software __FDxemu branch into one file, so they legitimately carry
# dead x87 bytes that are never reached because __real87==0 selects the software
# path (verified in fdmth086.asm dispatch _chk8087). The separate image-level
# INT 0x34-0x3D check below proves the emulator-trap route is absent too.
X87RE='\b(fld[a-z0-9]*|fst[a-z0-9]*|fadd[a-z0-9]*|fsub[a-z0-9]*|fmul[a-z0-9]*|fdiv[a-z0-9]*|fild[a-z0-9]*|fist[a-z0-9]*|fcom[a-z0-9]*|fwait|finit|fninit|fldcw|fnstcw|fnstsw|fxch|fabs|fchs|fsqrt|fscale|fpatan|fptan|f2xm1|fyl2x)\b'
assert_no_8087() {
  local bad=0 o
  for o in "$@"; do
    if "$WDIS" "$o" 2>/dev/null | grep -qiE "$X87RE"; then
      echo "BUILD ERROR: inline 8087 (x87) math found in $o -- this target has no"
      echo "  8087 and this build must be pure soft-float. Compile with -fpc, not"
      echo "  -fpi/-fpi87. (8087 hardware support is deferred: see"
      echo "  docs/8087_HARDWARE_SUPPORT_DEFERRED.md.)"
      "$WDIS" "$o" 2>/dev/null | grep -iE "$X87RE" | head -5 | sed 's/^/    > /'
      bad=1
    fi
  done
  [ "$bad" -eq 0 ] || exit 2
}
assert_no_8087 floattest.obj crt0.obj stdioshim.obj lowlevel.obj stubs.obj

# --- purity gate: zero INT 21h (DOS) AND zero executed 8087 trap (INT 34-3D) ---
python3 - floattest.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
# emulator trap opcodes INT 0x34..0x3D would be CD 34 .. CD 3D
traps=sum(d.count(bytes([0xcd,v])) for v in range(0x34,0x3e))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}  INT34-3D(8087emu)={traps}")
assert dos==0,   "FAIL: DOS INT 21h present!"
assert bdos>0,   "FAIL: no BDOS call in image!"
assert traps==0, "FAIL: 8087 emulator trap present -- this must be pure soft-float!"
PY

# --- run under emu2 + hand-computed oracle gate ---
OUT="$("$EMU2" floattest.cmd | tr -d '\r')"; echo "--- output ---"; echo "$OUT"
EXP="pi6=3141592 mul=40115 add=468 sub=242"
if [ "$OUT" = "$EXP" ]; then
  echo "PASS: Watcom OWN double soft-float (-fpc, __FDxemu) on CP/M-86, no 8087"
else
  echo "FAIL: expected: $EXP"; exit 1
fi
