#!/bin/bash
# build-stdcbench.sh -- the wc-stdcbench milestone (rc7xx-work#6 milestone 4).
#
# Rebuild the SUBSTANTIAL multi-module benchmark stdcbench 0.8 (integer suite:
# c90base + c90lib) against Open Watcom's OWN, UNCHANGED C library plus our thin
# CP/M-86 Layer-2 seams -- instead of the Digital Research C run-time
# (clears.l86) that owc-drc/stdcbench used.  This is the end-to-end proof that
# the retargeted clib carries a real program that leans on the whole library
# surface: printf/sprintf, the string + ctype families, malloc/free/realloc,
# qsort, and the 32-bit long helpers -- all resolved ONLY by:
#   port/stdioshim.c : __qwrite (BDOS C_WRITE) + isatty (no DOS IOCTL)
#   port/lowlevel.c  : arena near-heap (malloc/FILE buffers/stdcbench buffers)
#   port/stubs.c     : closure symbols; NO DOS trap
#   port/crt0sm.asm  : CP/M-86 startup (wc_heap_init -> main -> BDOS exit)
#   test/scbport.c   : stdcbench glue (BDOS T_GET clock, __InitFiles, main)
#
# The UPSTREAM stdcbench sources are byte-for-byte the same ones owc-drc built;
# only the runtime beneath them changes.  Run-verified under emu2 (functional:
# proves every module executes and scores compute correctly through the Watcom
# clib).  The RC759-comparable score vs the DR C reference (13) comes from
# running the SAME SCB.CMD on MAME rc759 -- see scb-mame harness.
#
# NEVER search outside /Users/ravn/z80/.
set -e
cd "$(dirname "$0")"
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build-stdcbench}"; mkdir -p "$OUTDIR"
SRC=".."                                        # port/ + test/ live one up from OUTDIR
# Upstream stdcbench 0.8 sources (the SAME tree owc-drc built), reused verbatim.
SCB="$SRC/../owc-drc/stdcbench/src/stdcbench-0.8"
cd "$OUTDIR"

INC="-i=$SRC/test -i=$SCB -i=$B/hdr/dos/h -i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h"
# -otexan = Watcom full optimisation (time, relax-alias, expand-calls, max,
# numeric): applied to both the hot clib objects and the benchmark modules so
# the score is a fair like-for-like comparison against the DR C reference.
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -otexan"    # compile Watcom clib source
USER="-bt=dos -0 -ms -zl -zastd=c99"                       # compile our port + scbport
SCBC="-bt=dos -0 -ms -zl -zastd=c99 -otexan"    # compile upstream stdcbench C99

cw() { "$WCC" $CLIB $INC "$1" -fo="$2"; }        # a Watcom clib source

# ---------------------------------------------------------------------------
# Layer 1: Open Watcom's OWN, UNCHANGED C library objects
# ---------------------------------------------------------------------------
# --- __prtf formatter (shared by printf/fprintf/sprintf) ---
cw "$B/clib/streamio/c/prtf.c"     prtf.obj
cw "$B/clib/streamio/c/noefgfmt.c" noefgfmt.obj   # %e/%f/%g slot (no float used)
cw "$B/clib/convert/c/itoa.c"      itoa.obj
cw "$B/clib/convert/c/ltoa.c"      ltoa.obj
cw "$B/clib/convert/c/lltoa.c"     lltoa.obj
cw "$B/clib/convert/c/alphabet.c"  alphabet.obj
cw "$B/clib/mbyte/c/wctomb.c"      wctomb.obj

# --- genuine FILE* stdio write-path (printf path) ---
cw "$B/clib/streamio/c/printf.c"   printf.obj
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

# --- sprintf (stdcbench formats into buffers 10x); shares __prtf, no FILE ---
cw "$B/clib/string/c/sprintf.c"    sprintf.obj
cw "$B/clib/string/c/vsprintf.c"   vsprintf.obj

# --- string family used by stdcbench ---
cw "$B/clib/string/c/strlen.c"     strlen.obj
cw "$B/clib/string/c/strcmp.c"     strcmp.obj
cw "$B/clib/string/c/strncmp.c"    strncmp.obj
cw "$B/clib/string/c/strchr.c"     strchr.obj
cw "$B/clib/string/c/strcpy.c"     strcpy.obj
cw "$B/clib/string/c/strncpy.c"    strncpy.obj
cw "$B/clib/string/c/strstr.c"     strstr.obj
cw "$B/clib/string/c/strrchr.c"    strrchr.obj
cw "$B/clib/convert/c/strtol.c"    strtol.obj
cw "$B/clib/string/c/strupr.c"     strupr.obj   # pulled by convert/prtf helpers

# --- ctype: isdigit/isspace are macros over _IsTable; tolower is a function ---
cw "$B/clib/char/c/istable.c"      istable.obj
cw "$B/clib/char/c/tolower.c"      tolower.obj

# --- near-heap manager (malloc/free/realloc/calloc + FILE + stdcbench buffers) ---
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

# --- mem helpers ---
cw "$B/clib/memory/c/memcpy.c"     memcpy.obj
cw "$B/clib/memory/c/memset.c"     memset.obj
cw "$B/clib/memory/c/memmove.c"    memmove.obj
cw "$B/clib/memory/c/memcmp.c"     memcmp.obj
cw "$B/clib/memory/c/memchr.c"     memchr.obj

# --- qsort (stdcbench sorts once) ---
cw "$B/clib/search/c/qsort.c"      qsort.obj

# --- Layer-1 long helpers (32-bit multiply/divide) ---
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo=i4m.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo=i4d.obj

# ---------------------------------------------------------------------------
# Layer 2: our thin CP/M-86 seams + stdcbench glue
# ---------------------------------------------------------------------------
"$WASM" -ms -0 "$SRC/port/crt0sm.asm"  -fo=crt0.obj
"$WCC" $USER $INC "$SRC/port/cominit.c"   -fo=cominit.obj   # crt0 runtime init (__InitFiles)
"$WCC" $USER $INC "$SRC/port/stdioshim.c" -fo=stdioshim.obj
"$WCC" $USER $INC "$SRC/port/lowlevel.c"  -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/stubs.c"     -fo=stubs.obj
"$WCC" $USER $INC $SCB_EXTRA "$SRC/test/scbport.c" -fo=scbport.obj

# ---------------------------------------------------------------------------
# UPSTREAM stdcbench 0.8 modules (byte-identical to the owc-drc build set)
# ---------------------------------------------------------------------------
MODS="stdcbench c90base c90base-compression c90base-data \
      c90base-huffman-iterative c90base-huffman-recursive c90base-huffman_tree \
      c90base-immul c90base-isort c90lib c90lib-htab c90lib-lnlc c90lib-peep \
      c90lib-peep-stm8"
SCBOBJS=""
for m in $MODS; do
    o="scb_$(echo "$m" | tr -c 'A-Za-z0-9' '_').obj"
    "$WCC" $SCBC $INC "$SCB/$m.c" -fo="$o"
    SCBOBJS="$SCBOBJS file $o"
done

# ---------------------------------------------------------------------------
# Link a CP/M-86 .CMD
# ---------------------------------------------------------------------------
"$WLINK" format cpm86 op dosseg op quiet name scb.cmd \
  file crt0.obj file scbport.obj file cominit.obj $SCBOBJS \
  file stdioshim.obj file lowlevel.obj file stubs.obj \
  file printf.obj file fprtf.obj file fputc.obj file fputs.obj file puts.obj \
  file flush.obj file fflush.obj file ioalloc.obj file chktty.obj \
  file iob.obj file initfile.obj file ferror.obj \
  file sprintf.obj file vsprintf.obj \
  file prtf.obj file noefgfmt.obj file itoa.obj file ltoa.obj file lltoa.obj \
  file alphabet.obj file wctomb.obj \
  file strlen.obj file strcmp.obj file strncmp.obj file strchr.obj \
  file strcpy.obj file strncpy.obj file strstr.obj file strupr.obj \
  file strrchr.obj file strtol.obj \
  file istable.obj file tolower.obj \
  file nmalloc.obj file nfree.obj file calloc.obj file nrealloc.obj \
  file grownear.obj file amblksiz.obj file heapen.obj file nheapmin.obj \
  file mem.obj file _expand.obj file nmemneed.obj file nmsize.obj \
  file nexpand.obj file nheapunl.obj file bfree.obj \
  file memcpy.obj file memset.obj file memmove.obj file memcmp.obj file memchr.obj \
  file qsort.obj file i4m.obj file i4d.obj

# --- purity gate: zero INT 21h (DOS) ---
python3 - scb.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}  size={len(d)}")
assert dos==0, "FAIL: DOS INT 21h present in image!"
assert bdos>0, "FAIL: no BDOS call in image!"
PY

cp scb.cmd "../SCB-WC.CMD" 2>/dev/null || true
echo "built scb.cmd ($(stat -f%z scb.cmd) bytes)"

# --- optional emu2 functional run (host-speed score; proves execution) ---
if [ -n "$SCB_NORUN" ]; then echo "SCB_NORUN set -- skipping emu2 run"; exit 0; fi
echo "=== running stdcbench (emu2 functional run; score reflects HOST speed) ==="
OUT="$("$EMU2" scb.cmd 2>&1 | tr -d '\r')"; echo "--- output ---"; echo "$OUT"
if echo "$OUT" | grep -q "final score:"; then
  echo "PASS: stdcbench (c90base+c90lib) runs end-to-end on Watcom clib + our shim"
else
  echo "FAIL: no final score printed"; exit 1
fi
