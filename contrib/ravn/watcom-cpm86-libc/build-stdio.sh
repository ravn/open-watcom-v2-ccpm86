#!/bin/bash
# Reproducible proof: Open Watcom's OWN, UNCHANGED stdio FILE* write-path --
# printf / fprintf / puts / fputs -> __fprtf -> fputc (buffer) -> __flush ->
# __qwrite -- running on CP/M-86, resolved ONLY by our thin Layer-2 seams:
#   port/stdioshim.c  : __qwrite (BDOS C_WRITE) + isatty (no DOS IOCTL)
#   port/lowlevel.c   : arena heap (the FILE buffer comes from malloc)
#   port/stubs.c      : __InitFiles/__full_io_exit stubs (their XI/YI records are
#                       never walked -- our crt0 doesn't run __InitRtns -- but the
#                       linker needs the symbols) + read-path stubs never hit.
# Run-verified under emu2 against a hand-computed oracle (see stdiotest.c).
# This is the wc-printf-stdio milestone (rc7xx-work#7).
set -e
cd "$(dirname "$0")"
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build-stdio}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"       # compile Watcom clib source
USER="-bt=dos -0 -ms -zl -zastd=c99"                     # compile our port + test

cw() { "$WCC" $CLIB $INC "$1" -fo="$2"; }     # compile a Watcom clib source

# --- Watcom clib: the __prtf formatter (shared with cprintf/heap proofs) ---
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
cw "$B/clib/streamio/c/printf.c"   printf.obj    # printf -> __fprtf(stdout,...)
cw "$B/clib/streamio/c/fprintf.c"  fprintf.obj   # fprintf -> __fprtf(fp,...)
cw "$B/clib/streamio/c/fprtf.c"    fprtf.obj     # __fprtf -> __prtf(file_putc) + __flush
cw "$B/clib/streamio/c/fputc.c"    fputc.obj     # fputc buffers into FILE
cw "$B/clib/streamio/c/fputs.c"    fputs.obj     # fputs -> fputc
cw "$B/clib/streamio/c/puts.c"     puts.obj      # puts -> fputc
cw "$B/clib/streamio/c/flush.c"    flush.obj     # __flush -> __qwrite (our shim)
cw "$B/clib/streamio/c/fflush.c"   fflush.obj    # fflush
cw "$B/clib/streamio/c/ioalloc.c"  ioalloc.obj   # __ioalloc (buffer via malloc)
cw "$B/clib/streamio/c/chktty.c"   chktty.obj    # __chktty -> isatty (our shim)
cw "$B/clib/streamio/c/iob.c"      iob.obj       # __iob table (stdout static init)
cw "$B/clib/streamio/c/initfile.c" initfile.obj  # __InitFiles (DOS-free, attaches _link)
cw "$B/clib/streamio/c/ferror.c"   ferror.obj    # ferror (used by __fprtf)

# --- Watcom clib: the near-heap manager (FILE buffer allocator, UNCHANGED) ---
cw "$B/clib/heap/c/nmalloc.c"      nmalloc.obj
cw "$B/clib/heap/c/nfree.c"        nfree.obj
cw "$B/clib/heap/c/calloc.c"       calloc.obj
cw "$B/clib/heap/c/nrealloc.c"     nrealloc.obj
cw "$B/clib/heap/c/grownear.c"     grownear.obj
cw "$B/clib/heap/c/amblksiz.c"     amblksiz.obj
cw "$B/clib/heap/c/heapen.c"       heapen.obj
cw "$B/clib/heap/c/nheapmin.c"     nheapmin.obj
cw "$B/clib/heap/c/mem.c"          mem.obj
cw "$B/clib/heap/c/bfree.c"        bfree.obj
cw "$B/clib/heap/c/_expand.c"      _expand.obj
cw "$B/clib/heap/c/nmemneed.c"     nmemneed.obj
cw "$B/clib/heap/c/nmsize.c"       nmsize.obj
cw "$B/clib/heap/c/nexpand.c"      nexpand.obj
cw "$B/clib/heap/c/nheapunl.c"     nheapunl.obj

# --- Watcom clib: mem helpers ---
cw "$B/clib/memory/c/memcpy.c"     memcpy.obj
cw "$B/clib/memory/c/memset.c"     memset.obj
cw "$B/clib/memory/c/memmove.c"    memmove.obj

# --- Layer-1 long helpers (32-bit multiply/divide for %ld and 123456*789) ---
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo=i4m.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo=i4d.obj

# --- our thin CP/M-86 seam (Layer 2) + test ---
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
"$WCC" $USER $INC "$SRC/port/stdioshim.c" -fo=stdioshim.obj  # __qwrite + isatty
"$WCC" $USER $INC "$SRC/port/lowlevel.c"  -fo=lowlevel.obj   # arena __brk/sbrk
"$WCC" $USER $INC "$SRC/port/stubs.c"     -fo=stubs.obj
"$WCC" $USER $INC "$SRC/test/stdiotest.c" -fo=stdiotest.obj

# --- link a CP/M-86 .CMD ---
"$WLINK" format cpm86 op dosseg op quiet name stdiotest.cmd \
  file crt0.obj file stdiotest.obj file stdioshim.obj file lowlevel.obj \
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
  file stubs.obj file i4m.obj file i4d.obj

# --- purity gate: zero INT 21h (DOS) ---
python3 - stdiotest.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0, "FAIL: DOS INT 21h present in image!"
assert bdos>0, "FAIL: no BDOS call in image!"
PY

# --- run under emu2 + hand-computed oracle gate ---
OUT="$("$EMU2" stdiotest.cmd | tr -d '\r')"; echo "--- output ---"; echo "$OUT"
EXP=$'printf 42 ok\nputs line\nfputs line\nfprintf 97406784'
if [ "$OUT" = "$EXP" ]; then
  echo "PASS: Watcom GENUINE stdio FILE* write-path (printf/fprintf/puts/fputs) on CP/M-86"
else
  echo "FAIL: expected:"; echo "$EXP"; exit 1
fi
