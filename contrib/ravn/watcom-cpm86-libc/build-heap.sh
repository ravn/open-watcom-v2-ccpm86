#!/bin/bash
# Reproducible proof: Open Watcom's OWN near-heap (malloc/_nmalloc, free/_nfree,
# calloc, realloc, grownear's __ExpandDGROUP) + qsort running on CP/M-86,
# resolved ONLY by the thin lowlevel.c seam (arena __brk/sbrk, zero DOS trap).
# Run-verified under emu2 against a hand-computed oracle (see heaptest.c).
#
# This is the wc-lowlevel-shim milestone: it proves the ONE OS-coupled heap
# primitive (DOS sbrk.c __brk -> INT 21h AH=4Ah) can be swapped for a BDOS/arena
# equivalent while every heap manager above it stays Watcom's unmodified clib.
set -e
cd "$(dirname "$0")"
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build-heap}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"       # compile Watcom clib source
USER="-bt=dos -0 -ms -zl -zastd=c99"                     # compile our port + test

cw() { "$WCC" $CLIB $INC "$1" -fo="$2"; }     # compile a Watcom clib source

# --- Watcom clib: printf formatter (reused from the wc-sprintf-proof) ---
cw "$B/clib/streamio/c/prtf.c"     prtf.obj
cw "$B/clib/streamio/c/noefgfmt.c" noefgfmt.obj
cw "$B/clib/string/c/strupr.c"     strupr.obj
cw "$B/clib/string/c/strlen.c"     strlen.obj
cw "$B/clib/convert/c/itoa.c"      itoa.obj
cw "$B/clib/convert/c/ltoa.c"      ltoa.obj
cw "$B/clib/convert/c/lltoa.c"     lltoa.obj
cw "$B/clib/convert/c/alphabet.c"  alphabet.obj
cw "$B/clib/mbyte/c/wctomb.c"      wctomb.obj

# --- Watcom clib: the GENUINE near-heap manager (unchanged) ---
cw "$B/clib/heap/c/nmalloc.c"      nmalloc.obj    # malloc/_nmalloc/__nheapbeg
cw "$B/clib/heap/c/nfree.c"        nfree.obj      # free/_nfree
cw "$B/clib/heap/c/calloc.c"       calloc.obj     # calloc
cw "$B/clib/heap/c/nrealloc.c"     nrealloc.obj   # realloc/_nrealloc
cw "$B/clib/heap/c/grownear.c"     grownear.obj   # __ExpandDGROUP (calls our __brk)
cw "$B/clib/heap/c/amblksiz.c"     amblksiz.obj   # _amblksiz (grow chunk)
cw "$B/clib/heap/c/heapen.c"       heapen.obj     # __heap_enabled
cw "$B/clib/heap/c/nheapmin.c"     nheapmin.obj   # __nheapshrink (grownear needs it)
cw "$B/clib/heap/c/mem.c"          mem.obj        # __MemAllocator/__MemFree (near core)
cw "$B/clib/heap/c/bfree.c"        bfree.obj      # _bfree (based free helper, no DOS)
cw "$B/clib/heap/c/_expand.c"      _expand.obj    # __HeapManager_expand
cw "$B/clib/heap/c/nmemneed.c"     nmemneed.obj   # __nmemneed
cw "$B/clib/heap/c/nmsize.c"       nmsize.obj     # _nmsize (realloc)
cw "$B/clib/heap/c/nexpand.c"      nexpand.obj    # _nexpand (realloc)
cw "$B/clib/heap/c/nheapunl.c"     nheapunl.obj   # __UnlinkNHeap

# --- Watcom clib: qsort + the mem/str helpers the test/heap use ---
cw "$B/clib/search/c/qsort.c"      qsort.obj
cw "$B/clib/memory/c/memcpy.c"     memcpy.obj
cw "$B/clib/memory/c/memset.c"     memset.obj
cw "$B/clib/memory/c/memmove.c"    memmove.obj

# --- Layer-1 long helpers (32-bit multiply/divide) ---
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo=i4m.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo=i4d.obj

# --- our thin CP/M-86 seam (Layer 2) + test ---
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
cw "$SRC/port/cprintf.c"           cprintf.obj
"$WCC" $USER $INC "$SRC/port/lowlevel.c" -fo=lowlevel.obj   # arena __brk/sbrk + _curbrk
"$WCC" $USER "$SRC/port/stubs.c"    -fo=stubs.obj
"$WCC" $USER $INC "$SRC/test/heaptest.c" -fo=heaptest.obj

# --- link a CP/M-86 .CMD ---
"$WLINK" format cpm86 op dosseg op quiet name heaptest.cmd \
  file crt0.obj file heaptest.obj file cprintf.obj file lowlevel.obj \
  file prtf.obj file noefgfmt.obj file strupr.obj file itoa.obj file ltoa.obj \
  file lltoa.obj file alphabet.obj file strlen.obj file wctomb.obj \
  file nmalloc.obj file nfree.obj file calloc.obj file nrealloc.obj \
  file grownear.obj file amblksiz.obj file heapen.obj file nheapmin.obj \
  file mem.obj file _expand.obj file nmemneed.obj file nmsize.obj \
  file nexpand.obj file nheapunl.obj file bfree.obj \
  file qsort.obj file memcpy.obj file memset.obj file memmove.obj \
  file stubs.obj file i4m.obj file i4d.obj

# --- purity gate: zero INT 21h (DOS) ---
python3 - heaptest.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0, "FAIL: DOS INT 21h present in image!"
assert bdos>0, "FAIL: no BDOS call in image!"
PY

# --- run under emu2 + hand-computed oracle gate ---
OUT="$("$EMU2" heaptest.cmd | tr -d '\r')"; echo "--- output ---"; echo "$OUT"
EXP=$'sorted : 0 1 2 3 4 5 6 7 8 9\ncalloc : 0\nrealloc: 0 40\nreuse  : ok'
if [ "$OUT" = "$EXP" ]; then
  echo "PASS: Watcom near-heap (malloc/free/calloc/realloc) + qsort on CP/M-86"
else
  echo "FAIL: expected:"; echo "$EXP"; exit 1
fi
