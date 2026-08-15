#!/bin/bash
# Reproducible proof: Open Watcom's OWN, UNCHANGED stdio FILE* layer -- the full
# read AND write path (fopen/fclose/fwrite/fputs/fprintf/fread/fgets/fgetc/
# fseek/ftell/remove) -- running against REAL CP/M-86 disk files, resolved only
# by our thin Layer-2 seam:
#   port/diskio.c   : _sopen/__qread/__qwrite/__lseek/__close + isatty + remove
#                     backed by CP/M-86 FCB BDOS random-record calls (INT 0E0h).
#                     SUPERSEDES stdioshim.c (owns the same console __qwrite too).
#   port/lowlevel.c : arena heap (FILE buffers come from malloc)
#   port/stubs.c    : residual closure symbols; __lseek excluded (-DDISKIO_LSEEK)
#                     because diskio.c owns the real one.
# Run-verified under emu2 against the self-checking oracle test/disktest.c.
# This is the disk-FILE* milestone (rc7xx-work#7).
set -e
cd "$(dirname "$0")"
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build-diskio}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"       # compile Watcom clib source
USER="-bt=dos -0 -ms -zl -zastd=c99"          # compile our port + test

cw() { "$WCC" $CLIB $INC "$B/clib/$1" -fo="$2"; }   # compile a Watcom clib source

# --- Watcom clib: the __prtf formatter (shared) ---
cw streamio/c/prtf.c     prtf.obj
cw streamio/c/noefgfmt.c noefgfmt.obj
cw string/c/strupr.c     strupr.obj
cw string/c/strlen.c     strlen.obj
cw string/c/strcmp.c     strcmp.obj
cw convert/c/itoa.c      itoa.obj
cw convert/c/ltoa.c      ltoa.obj
cw convert/c/lltoa.c     lltoa.obj
cw convert/c/alphabet.c  alphabet.obj
cw mbyte/c/wctomb.c      wctomb.obj

# --- Watcom clib: the GENUINE FILE* stdio write path (UNCHANGED) ---
cw streamio/c/printf.c   printf.obj
cw streamio/c/fprintf.c  fprintf.obj
cw streamio/c/fprtf.c    fprtf.obj
cw streamio/c/fputc.c    fputc.obj
cw streamio/c/fputs.c    fputs.obj
cw streamio/c/puts.c     puts.obj
cw streamio/c/fwrite.c   fwrite.obj
cw streamio/c/flush.c    flush.obj
cw streamio/c/fflush.c   fflush.obj

# --- Watcom clib: the GENUINE FILE* stdio read/open path (UNCHANGED) ---
cw streamio/c/fopen.c    fopen.obj      # fopen/_fsopen/__doopen/__open_flags -> _sopen
cw streamio/c/fclose.c   fclose.obj     # fclose/__doclose -> __flush + __close
cw streamio/c/allocfp.c  allocfp.obj    # __allocfp/__freefp
cw streamio/c/fgetc.c    fgetc.obj      # fgetc/__filbuf/__fill_buffer -> __qread
cw streamio/c/fgets.c    fgets.obj      # fgets -> fgetc
cw streamio/c/fread.c    fread.obj      # fread -> __filbuf
cw streamio/c/fseek.c    fseek.obj      # fseek -> __flush/__lseek
cw streamio/c/ftell.c    ftell.obj      # ftell -> __lseek
cw streamio/c/feof.c     feof.obj
cw streamio/c/ferror.c   ferror.obj
cw streamio/c/ioalloc.c  ioalloc.obj    # __ioalloc (buffer via malloc)
cw streamio/c/chktty.c   chktty.obj     # __chktty -> isatty (our seam)
cw streamio/c/iob.c      iob.obj        # __iob table (std FILE static init)
cw streamio/c/initfile.c initfile.obj   # __InitFiles (DOS-free)
cw streamio/c/comtflag.c comtflag.obj   # _commode (commit-mode default)
cw streamio/c/freefp.c   freefp.obj     # __freefp (release FILE on fclose/fail)
cw handleio/c/textmode.c textmode.obj   # _fmode (default text translation)

# --- Watcom clib: the near-heap manager (FILE buffer allocator, UNCHANGED) ---
cw heap/c/nmalloc.c   nmalloc.obj
cw heap/c/nfree.c     nfree.obj
cw heap/c/calloc.c    calloc.obj
cw heap/c/nrealloc.c  nrealloc.obj
cw heap/c/grownear.c  grownear.obj
cw heap/c/amblksiz.c  amblksiz.obj
cw heap/c/heapen.c    heapen.obj
cw heap/c/nheapmin.c  nheapmin.obj
cw heap/c/mem.c       mem.obj
cw heap/c/bfree.c     bfree.obj
cw heap/c/_expand.c   _expand.obj
cw heap/c/nmemneed.c  nmemneed.obj
cw heap/c/nmsize.c    nmsize.obj
cw heap/c/nexpand.c   nexpand.obj
cw heap/c/nheapunl.c  nheapunl.obj

# --- Watcom clib: mem helpers ---
cw memory/c/memcpy.c  memcpy.obj
cw memory/c/memset.c  memset.obj
cw memory/c/memmove.c memmove.obj

# --- Layer-1 long helpers (32-bit mul/div for %ld and lseek arithmetic) ---
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo=i4m.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo=i4d.obj

# --- our thin CP/M-86 seam (Layer 2) + oracle test ---
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
"$WCC" $USER $INC "$SRC/port/cominit.c"  -fo=cominit.obj
"$WCC" $USER $INC "$SRC/port/diskio.c"   -fo=diskio.obj      # the disk seam
"$WCC" $USER $INC "$SRC/port/lowlevel.c" -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/errnoptr.c" -fo=errnoptr.obj    # __get_errno_ptr -> &errno
"$WCC" $USER $INC -DDISKIO_LSEEK "$SRC/port/stubs.c" -fo=stubs.obj  # exclude stub __lseek
"$WCC" $USER $INC "$SRC/test/disktest.c" -fo=disktest.obj

# --- link a CP/M-86 .CMD ---
"$WLINK" format cpm86 op dosseg op quiet name disktest.cmd \
  file crt0.obj file disktest.obj file diskio.obj file lowlevel.obj \
  file cominit.obj \
  file printf.obj file fprintf.obj file fprtf.obj file fputc.obj file fputs.obj \
  file puts.obj file fwrite.obj file flush.obj file fflush.obj \
  file fopen.obj file fclose.obj file allocfp.obj file fgetc.obj file fgets.obj \
  file fread.obj file fseek.obj file ftell.obj file feof.obj file ferror.obj \
  file ioalloc.obj file chktty.obj file iob.obj file initfile.obj \
  file comtflag.obj file freefp.obj file textmode.obj \
  file prtf.obj file noefgfmt.obj file strupr.obj file strlen.obj file strcmp.obj \
  file itoa.obj file ltoa.obj file lltoa.obj file alphabet.obj file wctomb.obj \
  file nmalloc.obj file nfree.obj file calloc.obj file nrealloc.obj \
  file grownear.obj file amblksiz.obj file heapen.obj file nheapmin.obj \
  file mem.obj file _expand.obj file nmemneed.obj file nmsize.obj \
  file nexpand.obj file nheapunl.obj file bfree.obj \
  file memcpy.obj file memset.obj file memmove.obj \
  file stubs.obj file errnoptr.obj file i4m.obj file i4d.obj

# --- purity gate: zero INT 21h (DOS) ---
python3 - disktest.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0, "FAIL: DOS INT 21h present in image!"
assert bdos>0, "FAIL: no BDOS call in image!"
PY

# --- run under emu2 + self-checking oracle gate ---
rm -f TEST.TXT
OUT="$("$EMU2" disktest.cmd | tr -d '\r')"; echo "--- output ---"; echo "$OUT"
if echo "$OUT" | grep -q "DISKIO: PASS"; then
  echo "PASS: Watcom GENUINE stdio FILE* disk read/write path on CP/M-86"
else
  echo "FAIL: disktest did not report PASS"; exit 1
fi
