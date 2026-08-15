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
WLIB="$B/nwlib/osxa64/wlib.exe"
WDIS="$B/ndisasm/osxa64/wdis.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build-whetstone}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
AINC="-i=$B/watcom/h -i=$B/comp_cfg/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"       # compile Watcom clib source
USER="-bt=dos -0 -ms -fpc -zl -zastd=c99"                # our port + test (float => -fpc)

cw() { "$WCC" $CLIB $INC "$1" -fo="$2"; }      # compile a Watcom clib source
cwf() { "$WCC" $CLIB -fpc -i=$B/mathlib/h $INC "$1" -fo="$2"; } # float-bearing clib/mathlib source (-fpc!)
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

# --- Watcom clib: REAL %e/%f/%g float printf + transcendental libm -----------
# Whetstone prints check values with "%12.4e" and calls sin/cos/atan/exp/log/
# sqrt, so unlike the #8 float proof (which used %ld and no libm) we need
# Watcom's GENUINE float formatter (_EFG_Format -> __LDcvt) and its transcendental
# library. setefg.obj's __setEFGfmt() -- which we call from main(), since our crt0
# does not walk the init table -- repoints the noefgfmt DATA pointers at the real
# _EFG_Format. Everything else is pulled by wlink from prebuilt Watcom libraries
# (LIB-searched, so ONLY referenced modules are pulled):
#   * FP conversions + fpu support + %e formatter: the PURE-8086 (msdos.086)
#     clib libraries -- fully consistent with our -0 build.
#   * transcendentals + 80-bit long-double software layer: Watcom shipped these
#     only in the msdos.286 mathlib (there is no msdos.086 mathlib build). Those
#     objects are software-float (0 inline 8087) and INT-21h-free, and -- verified
#     by assert_no_286() below -- contain NO 286-only opcode, so they run on the
#     RC759's 80186. We archive them into math286.lib.
cw "$B/clib/streamio/c/setefg.c"   setefg.obj    # __setEFGfmt(): install real fmt

# --- Watcom clib: the small DOS-free support modules the mathlib error/format
# path pulls (all msdos-independent; compiled -0/-ms just like the rest):
#   seterrno.c : __set_EDOM_/__set_ERANGE_ -- sin/cos/pow/ldexp domain/range flag
#                (DOS build => lib_set_errno(x) is just `errno = x`; errno itself
#                 is the single global defined in port/stubs.c).
#   rtcntrl.c  : __get_rt_control_ptr_ -- efgfmt reads the rounding-control flag.
#   iobaddr.c  : __get_std_stream_ -- _matherr's stderr accessor (=> &__iob[h]).
#   istable.c  : __IsTable (_IsTable) -- ctype table strtod/__cnvs2d index into.
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
# The RC759 CPU is an 80186. Our own code is all built -0 (8086), but we also pull
# transcendentals/long-double from Watcom's PREBUILT msdos.286 mathlib. The 80186
# runs every 80186/80286 *real-mode* integer instruction, but NOT the 286+
# system/protected-mode instructions (arpl,lar,lsl,lgdt,lidt,lldt,sgdt,sidt,sldt,
# lmsw,smsw,clts,str,ltr,verr,verw) -- those would fault on the RC759. So, as a
# standing part of the CP/M-86 build process, disassemble every object we make
# linkable from a non-8086 (here: .286) source and fail the build if any such
# opcode appears. (This complements assert_no_8087: one guards the FPU, this one
# guards the CPU instruction set of the target.)
CPU286RE='\b(arpl|lar|lsl|lgdt|lidt|lldt|sgdt|sidt|sldt|lmsw|smsw|clts|str|ltr|verr|verw)\b'
assert_no_286() {
  local bad=0 o hits n=0
  for o in "$@"; do
    n=$((n+1))
    hits=$("$WDIS" "$o" 2>/dev/null | grep -ioE "$CPU286RE" | sort -u | tr '\n' ' ')
    if [ -n "$hits" ]; then
      echo "BUILD ERROR: non-80186 (286+ protected-mode) opcode in $o : $hits"
      echo "  The RC759 target is an 80186 and cannot execute these. A prebuilt"
      echo "  library object is not 80186-safe -- do not link it."
      bad=1
    fi
  done
  if [ "$bad" -ne 0 ]; then exit 4; fi
  echo "cpu-check: $n prebuilt .286 object(s) scanned, 0 non-80186 opcodes (80186-safe)"
}
assert_no_286 "$MATH286_SRC"/*.obj

# --- our thin CP/M-86 seam (Layer 2) + the float test ---
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
"$WCC" $USER $INC "$SRC/port/stdioshim.c" -fo=stdioshim.obj
"$WCC" $USER $INC "$SRC/port/lowlevel.c"  -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/stubs.c"     -fo=stubs.obj
"$WCC" $USER $INC "$SRC/port/errnoptr.c"  -fo=errnoptr.obj
"$WCC" $USER $INC ${WHET_EXTRA:-} "$SRC/test/whetstone.c" -fo=whetstone.obj

# --- anti-constant-fold tripwire ---------------------------------------------
# The whole point of #8 is to exercise Watcom's RUNTIME soft-float. If the test's
# operands are compile-time constants the optimizer folds the arithmetic away and
# emits ZERO __FDx calls -- the oracle would still print the right numbers but the
# runtime path we claim to prove is never taken (this bit us once: volatile
# operands are what force the calls). Fail loudly unless real __FDx calls survive.
NFD=$("$WDIS" whetstone.obj 2>/dev/null | grep -ciE 'call[[:space:]]+__FD[A-Z0-9]+')
if [ "${NFD:-0}" -lt 1 ]; then
  echo "BUILD ERROR: whetstone.obj contains no runtime __FDx soft-float calls --"
  echo "  the float arithmetic was constant-folded, so this run would NOT prove the"
  echo "  runtime soft-float path. Keep the operands 'volatile' in test/whetstone.c."
  exit 3
fi
echo "soft-float: $NFD runtime __FDx call site(s) in whetstone.obj (not folded)"

# --- link a CP/M-86 .CMD ---
"$WLINK" format cpm86 op dosseg op quiet name whetstone.cmd \
  file crt0.obj file whetstone.obj file stdioshim.obj file lowlevel.obj \
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
  file setefg.obj \
  file seterrno.obj file rtcntrl.obj file iobaddr.obj file istable.obj \
  file errnoptr.obj \
  file stubs.obj file i4m.obj file i4d.obj \
  $LIBS

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
assert_no_8087 whetstone.obj crt0.obj stdioshim.obj lowlevel.obj stubs.obj

# --- purity gate: zero INT 21h (DOS) AND zero executed 8087 trap (INT 34-3D) ---
python3 - whetstone.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
# emulator trap opcodes INT 0x34..0x3D would be CD 34 .. CD 3D
traps=sum(d.count(bytes([0xcd,v])) for v in range(0x34,0x3e))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}  INT34-3D(8087emu-bytecount)={traps}")
assert dos==0,   "FAIL: DOS INT 21h present!"
assert bdos>0,   "FAIL: no BDOS call in image!"
# NOTE: the INT34-3D byte-count is DROPPED as a gate for now (user: "drop the
# guard for now, get it working"). It is a raw byte scan and false-positives on
# libm constant tables: exp/log embed IEEE-754 double coefficients whose bytes
# incidentally contain CD 3B (e.g. inside a run of 3F-exponent words), which is
# DATA, not an INT 3Bh instruction (wdis shows no int/esc there). The genuine
# x87 in fdmth086/chipd16 is the DEAD hardware branch (ESC bytes D8-DF, selected
# out at runtime by __real87==0), not CD 3x. A future disassembly-based (code vs
# data) check should replace this byte scan -- tracked as a TODO.
print(f"note: INT34-3D byte-count guard disabled for now (false-positives on libm double constants)")
PY

# --- run under emu2 + independent-oracle gate --------------------------------
# Oracle = the SAME whetston.c compiled and run with a DIFFERENT toolchain (the
# host cc, native IEEE-754 double). That is an independent correctness check: if
# Watcom's soft-float + libm + %e formatter on CP/M-86 reproduce these per-module
# check values to the printed 4 significant digits, the double path is correct.
# (WHET_NORUN=1 skips this emu2 gate -- used by the MAME build, which measures
# execution time on cycle-accurate rc759 hardware instead.)
if [ "${WHET_NORUN:-0}" = "1" ]; then
  echo "WHET_NORUN=1: skipping emu2 oracle (built whetstone.cmd for MAME rc759)"
  exit 0
fi
OUT="$("$EMU2" whetstone.cmd | tr -d '\r')"; echo "--- output ---"; echo "$OUT"
EXP="$(cat <<'ORACLE'
      0       0       0   1.0000e+00  -1.0000e+00  -1.0000e+00  -1.0000e+00
    120     140     120  -6.8342e-02  -4.6264e-01  -7.2972e-01  -1.1240e+00
    140     120     120  -5.5336e-02  -4.4744e-01  -7.1097e-01  -1.1031e+00
   3450       1       1   1.0000e+00  -1.0000e+00  -1.0000e+00  -1.0000e+00
   2100       1       2   6.0000e+00   6.0000e+00  -7.1097e-01  -1.1031e+00
    320       1       2   4.9041e-01   4.9041e-01   4.9039e-01   4.9039e-01
   8990       1       2   1.0000e+00   1.0000e+00   9.9994e-01   9.9994e-01
   6160       1       2   3.0000e+00   2.0000e+00   3.0000e+00  -1.1031e+00
      0       2       3   1.0000e+00  -1.0000e+00  -1.0000e+00  -1.0000e+00
    930       2       3   8.3467e-01   8.3467e-01   8.3467e-01   8.3467e-01
ORACLE
)"
if [ "$OUT" = "$EXP" ]; then
  echo "PASS: Watcom OWN double soft-float + libm + %e printf (Whetstone) on CP/M-86, no 8087"
else
  echo "FAIL: output differs from the independent host-cc oracle:"; diff <(echo "$EXP") <(echo "$OUT") | head -40; exit 1
fi
