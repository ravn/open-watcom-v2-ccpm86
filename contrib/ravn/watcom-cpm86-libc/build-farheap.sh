#!/bin/bash
# Reproducible proof: Watcom's OWN far heap (_fmalloc/_ffree, unchanged)
# running on CP/M-86 Stage A "compact model", resolved only by the thin
# port/farheap.c seam (__AllocSeg/__GrowSeg/__FreeSeg -- multi-slab carving
# out of the wlink-emitted Extra group, zero DOS trap). Overlap-detecting
# stress test (test/farheaptest.c): allocate 48 pseudo-random-sized blocks
# via _fmalloc, fill each with a distinct pattern, THEN verify all of them --
# any allocator bug that overlaps two blocks shows up as a byte mismatch.
#
# tasks/plan-cpm86-big-model-2026-08-18.md Phase A4.
set -e
# NOTE: do NOT name the override vars WCC/WASM/WLINK -- Open Watcom's own
# tools read an env var NAMED AFTER THEMSELVES for implicit default
# switches (confirmed 2026-08-18: an exported WCC=<path to wcc.exe> makes
# wcc.exe parse that path string as bogus extra command-line content ->
# "E1139: Command line contains more than one file to compile"). unset
# here defensively in case the CALLER's shell exported one of these.
unset WCC WASM WLIB WLINK
cd "$(dirname "$0")"
OW="${OW:-$(cd "$(dirname "$0")/../../.." && pwd)}"; B="$OW/bld"
WCC="${OWCC_BIN:-$B/cc/i86/osxa64/binbuild/wcc.exe}"
WASM="${OWASM_BIN:-$B/wasm/osxa64/wasm.exe}"
WLINK="${OWLINK_BIN:-$B/wl/osxa64/wlink.exe}"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"

# FARHEAP size: a generous CEILING (G_Max), not a fixed target -- wlink now
# emits G_Min=1 paragraph for the Extra group (loadcpm86.c), so the CP/M-86
# loader grants whatever is ACTUALLY free between 1 paragraph and this
# ceiling, never refusing to load outright the way it did (rejected outright
# under real Concurrent CP/M-86, "Concurrent Fejl: For lidt lager") when
# G_Min==G_Max==300K exceeded a real machine's free memory. farheaptest.c
# reads the real grant off the base page at runtime and allocates until
# _fmalloc fails -- so it always uses "as much far heap as this machine
# actually has", not a number picked in advance. 0xF0000 (983040 B, just
# under the 16-bit-paragraph-field's ~1 MB ceiling) is "ask for basically
# everything"; RC759 hardware will grant far less (its own free-memory
# figure, once running, is well under this).
FARHEAP_SIZE=0xF0000

OUTDIR="${OUTDIR:-build-farheap}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/string/h -i=$B/clib/time/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"       # compile Watcom clib source
USER="-bt=dos -0 -ms -zl -zastd=c99"          # compile our port + test

cw() { "$WCC" $CLIB $INC "$1" -fo="$2"; }     # compile a Watcom clib source

# --- Watcom clib: printf formatter (for cprintf) ---
cw "$B/clib/streamio/c/prtf.c"     prtf.obj
cw "$B/clib/streamio/c/noefgfmt.c" noefgfmt.obj
cw "$B/clib/string/c/strupr.c"     strupr.obj
cw "$B/clib/string/c/strlen.c"     strlen.obj
cw "$B/clib/convert/c/itoa.c"      itoa.obj
cw "$B/clib/convert/c/ltoa.c"      ltoa.obj
cw "$B/clib/convert/c/lltoa.c"     lltoa.obj
cw "$B/clib/convert/c/alphabet.c"  alphabet.obj
cw "$B/clib/mbyte/c/wctomb.c"      wctomb.obj

# --- Watcom clib: the GENUINE far heap manager (unchanged) ---
# NOTE: this test only calls _fmalloc, so only ITS transitive deps are built
# here -- deliberately NOT ffree/fcalloc/frealloc/fmsize/fheapset/fheapchk/
# fheapmin/fheapwal. Those pull in heapmin.c's __HeapMin, which -- entirely
# independently of the __AllocSeg/__GrowSeg/__FreeSeg seam -- has its OWN
# direct DOS-only shrink path (TinySetBlock) with no non-DOS branch at all;
# retargeting THAT is separate follow-up work, not needed for the Phase A4
# allocate/fill/verify proof (see farheap.c's __FreeSeg comment for the
# related heapshrink() caveat). Keep this test's object set minimal and
# extend only as further _f* entry points get their own tests.
cw "$B/clib/heap/c/fmalloc.c"   fmalloc.obj
cw "$B/clib/heap/c/fmemneed.c"  fmemneed.obj   # __fmemneed (fmalloc)
cw "$B/clib/heap/c/heapen.c"    heapen.obj     # __heap_enabled
cw "$B/clib/heap/c/mem.c"       mem.obj        # __MemAllocator (generic alloc engine, near+far)

echo "==> Layer 1: near heap (crt0's wc_heap_init_ needs it, even though this test never calls near malloc)"
cw "$B/clib/heap/c/nmalloc.c"   nmalloc.obj
cw "$B/clib/heap/c/nfree.c"     nfree.obj
cw "$B/clib/heap/c/grownear.c"  grownear.obj
cw "$B/clib/heap/c/amblksiz.c"  amblksiz.obj
cw "$B/clib/heap/c/bfree.c"     bfree.obj
cw "$B/clib/heap/c/_expand.c"   _expand.obj
cw "$B/clib/heap/c/nmemneed.c"  nmemneed.obj
# WC_ARENA_BYTES=32: this test never calls near-malloc for real -- the full
# 36352-byte default (shared by other builds that DO use the near heap,
# e.g. build-heap.sh) would (a) burn ~36K of DGROUP that could otherwise
# come off the Extra-group budget, and (b) let Watcom's OWN _fmalloc()
# near-heap fallback (bld/clib/heap/c/fmalloc.c, see farheaptest.c's
# _getds comment) silently keep "succeeding" past real Extra-group
# exhaustion, contaminating the measurement. 32 is just enough to avoid a
# zero-length array; every real allocation here is >=512 bytes so the
# fallback can never actually satisfy one, and farheaptest.c also detects
# it directly (belt and suspenders).
"$WCC" $USER $INC -DWC_ARENA_BYTES=32u "$SRC/port/lowlevel.c" -fo=lowlevel.obj

# --- Layer-1 long helpers (32-bit multiply/divide, for %lu) ---
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo=i4m.obj
"$WASM" -ms -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo=i4d.obj

# --- our thin CP/M-86 seam (Layer 2) + test ---
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
"$WCC" $USER $INC -DCOMMONINIT_NOSTDIO "$SRC/port/cominit.c" -fo=cominit.obj
cw "$SRC/port/cprintf.c"             cprintf.obj
"$WCC" $USER $INC "$SRC/port/farheap.c"     -fo=farheap.obj   # Stage A seam under test
"$WCC" $USER $INC "$SRC/port/stubs.c"       -fo=stubs.obj
"$WCC" $USER $INC "$SRC/test/farheaptest.c" -fo=farheaptest.obj

# --- link a CP/M-86 .CMD, WITH the Stage A Extra group ---
"$WLINK" format cpm86 op dosseg op quiet op "farheap=$FARHEAP_SIZE" name farheaptest.cmd \
  file crt0.obj file farheaptest.obj file cprintf.obj file farheap.obj \
  file cominit.obj \
  file prtf.obj file noefgfmt.obj file strupr.obj file itoa.obj file ltoa.obj \
  file lltoa.obj file alphabet.obj file strlen.obj file wctomb.obj \
  file fmalloc.obj file fmemneed.obj file heapen.obj file mem.obj \
  file nmalloc.obj file nfree.obj file grownear.obj file amblksiz.obj \
  file bfree.obj file _expand.obj file nmemneed.obj \
  file lowlevel.obj \
  file stubs.obj file i4m.obj file i4d.obj

echo "--- .CMD header (group descriptors) ---"
python3 -c "
import sys
d = open('farheaptest.cmd','rb').read(128)
for i in range(0, 128, 9):
    t = d[i]
    if t == 0:
        break
    length, base, gmin, gmax = [int.from_bytes(d[i+1+2*k:i+3+2*k], 'little') for k in range(4)]
    print(f'  type={t} length={length:#06x} base={base:#06x} min={gmin:#06x} max={gmax:#06x}')
"

echo "--- purity gate: zero INT 21h (DOS) ---"
python3 - farheaptest.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0, "FAIL: DOS INT 21h present in image!"
assert bdos>0, "FAIL: no BDOS call in image!"
PY

echo "--- run under emu2 ---"
OUT="$("$EMU2" -P 255 farheaptest.cmd | tr -d '\r')"; echo "$OUT"
case "$OUT" in
  *"PASS (0 blocks corrupted)"*)
    echo "PASS: Stage A far heap (_fmalloc/_ffree, multi-slab) on CP/M-86" ;;
  *)
    echo "FAIL: unexpected output"; exit 1 ;;
esac
