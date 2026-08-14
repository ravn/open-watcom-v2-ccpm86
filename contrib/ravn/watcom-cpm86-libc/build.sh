#!/bin/bash
# Reproducible proof: Open Watcom's OWN clib printf formatter (__prtf) running on
# CP/M-86 through a thin BDOS callback. Zero DOS (INT 21h), zero Aztec code.
# Run-verified under emu2 against an independent (hand-computed / host) oracle.
#
# Architecture proven here (see README.md for the full three-layer picture):
#   Layer 1 (kernel, reused as-is)  : Watcom 32-bit long helpers __I4M/__U4M... ,
#                                     the __prtf formatter, itoa/ltoa/lltoa, str*.
#   Layer 2 (thin CP/M-86 seam)     : cprintf.c  -> __prtf + a BDOS C_WRITE
#                                     callback (INT E0h, CL=2); crt0sm.asm start.
#   The only OS dependency is the BDOS console call in the callback. Everything
#   above it is Watcom's unmodified clib source.
set -e
cd "$(dirname "$0")"
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/intel/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"       # compile Watcom clib source
USER="-bt=dos -0 -ms -zl"                     # compile our plain port + demo

# --- Watcom clib objects (reused UNCHANGED from the scratch build tree) ---
"$WCC" $CLIB $INC "$B/clib/streamio/c/prtf.c"     -fo=prtf.obj      # __prtf core
"$WCC" $CLIB $INC "$B/clib/streamio/c/noefgfmt.c" -fo=noefgfmt.obj  # no-float stub
"$WCC" $CLIB $INC "$B/clib/string/c/strupr.c"     -fo=strupr.obj    # %X upper-case
"$WCC" $CLIB $INC "$B/clib/string/c/strlen.c"     -fo=strlen.obj
"$WCC" $CLIB $INC "$B/clib/convert/c/itoa.c"      -fo=itoa.obj      # itoa_/utoa_
"$WCC" $CLIB $INC "$B/clib/convert/c/ltoa.c"      -fo=ltoa.obj      # ultoa_/ltoa_ (narrow)
"$WCC" $CLIB $INC "$B/clib/convert/c/lltoa.c"     -fo=lltoa.obj     # ulltoa_ (narrow)
"$WCC" $CLIB $INC "$B/clib/convert/c/alphabet.c"  -fo=alphabet.obj  # ___Alphabet table
"$WCC" $CLIB $INC "$B/clib/mbyte/c/wctomb.c"      -fo=wctomb.obj
# long-arithmetic helpers (Layer 1 "kernel"): 32-bit multiply / divide
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo=i4m.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo=i4d.obj

# --- our thin CP/M-86 port (Layer 2 seam) + demo ---
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj   # CP/M-86 startup + BDOS exit
"$WCC" $CLIB $INC "$SRC/port/cprintf.c" -fo=cprintf.obj  # __prtf + BDOS C_WRITE callback
"$WCC" $USER "$SRC/port/stubs.c"        -fo=stubs.obj    # never-reached closure stubs
"$WCC" $USER "$SRC/test/main.c"         -fo=main.obj

# --- link a CP/M-86 .CMD ---
"$WLINK" format cpm86 op dosseg op quiet name demo.cmd \
  file crt0.obj file main.obj file cprintf.obj file prtf.obj file noefgfmt.obj \
  file strupr.obj file itoa.obj file ltoa.obj file lltoa.obj file alphabet.obj \
  file strlen.obj file wctomb.obj file stubs.obj file i4m.obj file i4d.obj

# --- purity gate: zero INT 21h (DOS), at least one INT E0h (BDOS) ---
python3 - demo.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0,  "FAIL: DOS INT 21h present in image!"
assert bdos>0,  "FAIL: no BDOS call in image!"
PY

# --- run under emu2 + independent oracle gate ---
OUT="$("$EMU2" demo.cmd | tr -d '\r')"; echo "--- output ---"; echo "$OUT"
EXP=$'hello world: 42 and 97406784\nhex=beef width=[   42][42   ] char=Z'
if [ "$OUT" = "$EXP" ]; then
  echo "PASS: matches independent oracle (123456*789=97406784 via __I4M)"
else
  echo "FAIL: expected:"; echo "$EXP"; exit 1
fi
