#!/bin/bash
# build-cpp.sh -- native CP/M-86 C++ .CMD (iostreams + exceptions + setjmp),
# built on this contrib port's OW-clib foundation (the SAME real Watcom stdio /
# near-heap / disk-FILE* seam that build-diskio.sh proves) with Open Watcom's
# OWN unchanged C++ iostream + base-runtime libraries layered on top.
#
# This is the rc7xx-work#9 C++ layer, REBASED off the abandoned scratch mini-clib
# onto the canonical OW-clib port (rc7xx-work#12). The empirically-found gap
# (SEAMS=0 trial-link, 2026): everything the C++ runtime needs is either the REAL
# Watcom clib we already link (stdio __iob, near heap, ltoa/ultoa, strupr via a
# link alias, __get_std_stream_ from iobaddr.obj) OR one of exactly six OS hooks
# in port/cpprt.c (__clib_malloc/free) + port/ehsupp.c (__longjmp_handler,
# __get/restore_ovl_stack, __clib_exit, __clib_fatal). NO raw fd write/read shim
# is needed -- cout/cin/cerr route through the __iob FILE* layer.
#
#   ./build-cpp.sh test/cppfeat.cpp cppfeat.cmd --eh   # C++ feature demo (8/0)
#   ./build-cpp.sh test/mame_cpptest.cpp mc.cmd  --eh   # setjmp+EH+ios demo (6/0)
#   TRIAL=1 ./build-cpp.sh test/cppfeat.cpp x.cmd --eh  # link-only, dump undefineds
#   SEAMS=0 ...                                          # omit C++ seams (raw-gap probe)
#   NORUN=1 ...                                          # build only, skip emu2 run
# NEVER search outside /Users/ravn/z80/.
set -e
cd "$(dirname "$0")"
OW="${OW:-$(cd "$(dirname "$0")/../../.." && pwd)}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WPP="$B/plusplus/i86/osxa64/wpp.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
WLIB="$B/nwlib/osxa64/wlib.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"

SRC_CPP="${1:?usage: build-cpp.sh SRC.cpp OUT.cmd [--eh]}"
OUT="${2:?usage: build-cpp.sh SRC.cpp OUT.cmd [--eh]}"; OUTBASE="$(basename "$OUT")"
shift 2 || true
EH=0; for a in "$@"; do case "$a" in --eh) EH=1;; esac; done
# resolve the source to an absolute path (we build inside OUTDIR, so a path
# relative to the port root would otherwise be lost after the pushd).
case "$SRC_CPP" in /*) ;; *) SRC_CPP="$(pwd)/$SRC_CPP";; esac

PORTROOT="$(pwd)"
# test-harness include dir (mamedone.h). The C++ tests + harness header now live
# in this port's own test/ dir (rc7xx-work#12 consolidation), so the port is
# self-contained; override TESTINC only for out-of-tree sources.
TESTINC="${TESTINC:-$PORTROOT/test}"
EXTRA="${CPP_EXTRA:-}"                          # e.g. -DMAME_DONE

INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/handleio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"         # compile Watcom clib source
USER="-bt=dos -0 -ms -zl -zastd=c99"            # compile our port seams

OUTDIR="${OUTDIR:-build-cpp}"; mkdir -p "$OUTDIR"
SRC=".."
pushd "$OUTDIR" >/dev/null
cw() { "$WCC" $CLIB $INC "$B/clib/$1" -fo="$2"; }

# ---- Watcom clib: shared __prtf formatter + %ld converters -------------------
cw streamio/c/prtf.c     prtf.obj
cw streamio/c/noefgfmt.c noefgfmt.obj
cw string/c/strupr.c     strupr.obj      # exports _strupr_; aliased below
cw string/c/strlen.c     strlen.obj
cw string/c/strcmp.c     strcmp.obj
cw string/c/strcpy.c     strcpy.obj
cw convert/c/itoa.c      itoa.obj
cw convert/c/ltoa.c      ltoa.obj
cw convert/c/ultoa.c     ultoa.obj       # ostream << unsigned long
cw convert/c/lltoa.c     lltoa.obj
cw convert/c/alphabet.c  alphabet.obj
cw mbyte/c/wctomb.c      wctomb.obj

# ---- Watcom clib: GENUINE FILE* stdio write+read+open path (UNCHANGED) -------
cw streamio/c/printf.c   printf.obj
cw streamio/c/fprintf.c  fprintf.obj
cw streamio/c/fprtf.c    fprtf.obj
cw streamio/c/fputc.c    fputc.obj
cw streamio/c/fputs.c    fputs.obj
cw streamio/c/puts.c     puts.obj
cw streamio/c/fwrite.c   fwrite.obj
cw streamio/c/flush.c    flush.obj
cw streamio/c/fflush.c   fflush.obj
cw streamio/c/fopen.c    fopen.obj
cw streamio/c/fclose.c   fclose.obj
cw streamio/c/allocfp.c  allocfp.obj
cw streamio/c/fgetc.c    fgetc.obj
cw streamio/c/fgets.c    fgets.obj
cw streamio/c/fread.c    fread.obj
cw streamio/c/fseek.c    fseek.obj
cw streamio/c/ftell.c    ftell.obj
cw streamio/c/rewind.c   rewind.obj
cw streamio/c/feof.c     feof.obj
cw streamio/c/ferror.c   ferror.obj
cw streamio/c/ioalloc.c  ioalloc.obj
cw streamio/c/chktty.c   chktty.obj
cw streamio/c/iob.c      iob.obj
cw streamio/c/initfile.c initfile.obj
cw streamio/c/comtflag.c comtflag.obj
cw streamio/c/freefp.c   freefp.obj
cw streamio/c/iobaddr.c  iobaddr.obj     # __get_std_stream_ (iostream cout/cin/cerr binding)
cw handleio/c/textmode.c textmode.obj

# ---- Watcom clib: near-heap manager (FILE buffer + C++ streambuf/new) --------
for f in nmalloc nfree calloc nrealloc grownear amblksiz heapen nheapmin \
         mem bfree _expand nmemneed nmsize nexpand nheapunl; do
  cw heap/c/$f.c "$f.obj"
done

# ---- Watcom clib: mem helpers ------------------------------------------------
cw memory/c/memcpy.c  memcpy.obj
cw memory/c/memset.c  memset.obj
cw memory/c/memmove.c memmove.obj
cw memory/c/memcmp.c  memcmp.obj

# ---- Layer-1 long helpers + real 8086 setjmp/longjmp -------------------------
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm"       -fo=i4m.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm"       -fo=i4d.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/startup/a/stjmp086.asm" -fo=setjmp86.obj

# ---- CP/M-86 seams (Layer 2). diskio.c = console+disk __qwrite/__qread/isatty
#      superset (so NOT stdioshim.c); stubs.c -DDISKIO_LSEEK (diskio owns __lseek
#      and supplies getche/__flushall/tolower for fgetc's tty branch). ----------
"$WASM" -ms -0 "$SRC/port/crt0cpp.asm" -fo=crt0.obj
"$WCC" $USER $INC "$SRC/port/cominit.c"  -fo=cominit.obj
"$WCC" $USER $INC "$SRC/port/diskio.c"   -fo=diskio.obj
"$WCC" $USER $INC "$SRC/port/lowlevel.c" -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/errnoptr.c" -fo=errnoptr.obj
"$WCC" $USER $INC -DDISKIO_LSEEK "$SRC/port/stubs.c" -fo=stubs.obj
"$WCC" $USER $INC "$SRC/port/abortcpm.c" -fo=abortcpm.obj

# ---- C++ runtime OS seams (gated: SEAMS=0 probes the raw undefined gap) ------
CPPSEAM_OBJS=""
if [ "${SEAMS:-1}" = "1" ]; then
  "$WCC" $USER $INC "$SRC/port/cpprt.c"  -fo=cpprt.obj     # __clib_malloc/free
  "$WCC" $USER $INC "$SRC/port/ehsupp.c" -fo=ehsupp.obj    # longjmp/EH hooks
  CPPSEAM_OBJS="file cpprt.obj file ehsupp.obj"
fi

# ---- C++ iostream + base-runtime libraries (Watcom's OWN objects, archived) --
IOSDIR="$B/cpplib/iostream/generic.086/ms"
if [ "$EH" = 1 ]; then
  rm -f iosx_s.lib; set --; for o in "$IOSDIR/xobjs"/*.obj; do set -- "$@" "+$o"; done
  "$WLIB" -q -b -n iosx_s.lib "$@" >/dev/null
  IOSLIB=iosx_s.lib; BASERT="$B/cpplib/library/generic.086/ms/plbxs.lib"; XSFLAG="-xs"
else
  rm -f iost_s.lib; set --; for o in "$IOSDIR"/*.obj; do set -- "$@" "+$o"; done
  "$WLIB" -q -b -n iost_s.lib "$@" >/dev/null
  IOSLIB=iost_s.lib; BASERT="$B/cpplib/library/generic.086/ms/plibs.lib"; XSFLAG=""
fi

# ---- compile the C++ program -------------------------------------------------
"$WPP" -bt=dos -0 -ms $XSFLAG $INC -i="$TESTINC" $EXTRA "$SRC_CPP" -fo=user.obj

# ---- link a CP/M-86 .CMD -----------------------------------------------------
set +e
"$WLINK" format cpm86 op dosseg op quiet op nodefaultlibs name "$OUTBASE" \
  file crt0.obj file user.obj \
  file cominit.obj file diskio.obj file lowlevel.obj \
  file errnoptr.obj file stubs.obj file abortcpm.obj $CPPSEAM_OBJS \
  file setjmp86.obj \
  file prtf.obj file noefgfmt.obj \
  file strupr.obj alias strupr_=_strupr_ \
  file strlen.obj file strcmp.obj file strcpy.obj \
  file itoa.obj file ltoa.obj file ultoa.obj file lltoa.obj file alphabet.obj file wctomb.obj \
  file printf.obj file fprintf.obj file fprtf.obj file fputc.obj file fputs.obj \
  file puts.obj file fwrite.obj file flush.obj file fflush.obj \
  file fopen.obj file fclose.obj file allocfp.obj file fgetc.obj file fgets.obj \
  file fread.obj file fseek.obj file ftell.obj file rewind.obj file feof.obj file ferror.obj \
  file ioalloc.obj file chktty.obj file iob.obj file initfile.obj \
  file comtflag.obj file freefp.obj file iobaddr.obj file textmode.obj \
  file nmalloc.obj file nfree.obj file calloc.obj file nrealloc.obj \
  file grownear.obj file amblksiz.obj file heapen.obj file nheapmin.obj \
  file mem.obj file _expand.obj file nmemneed.obj file nmsize.obj \
  file nexpand.obj file nheapunl.obj file bfree.obj \
  file memcpy.obj file memset.obj file memmove.obj file memcmp.obj \
  file i4m.obj file i4d.obj \
  library "$IOSLIB" library "$BASERT" 2>&1 | tee wlink.out
set -e

echo "--- undefined references (if any) ---"
grep -iE 'E2028|undefined' wlink.out | sort -u || echo "(none)"

if [ "${TRIAL:-0}" = "1" ]; then echo "TRIAL=1: link-only."; popd >/dev/null; exit 0; fi
[ -f "$OUTBASE" ] || { echo "LINK FAILED"; popd >/dev/null; exit 1; }

# ---- purity gate: zero INT 21h (DOS) ----
python3 - "$OUTBASE" <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0, "FAIL: DOS INT 21h present!"
assert bdos>0, "FAIL: no BDOS call!"
PY

# leave the .CMD in build-cpp/ (gitignored); do not clutter the port root
echo "built $OUTBASE ($(wc -c < "$OUTBASE") bytes, EH=$EH)"

if [ "${NORUN:-0}" = "1" ]; then popd >/dev/null; exit 0; fi
echo "--- emu2 run ---"
"$EMU2" -P 255 "$OUTBASE" | tr -d '\r' || true
popd >/dev/null
