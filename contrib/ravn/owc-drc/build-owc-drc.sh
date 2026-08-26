#!/usr/bin/env bash
#
# build-owc-drc.sh -- build CP/M-86 programs with Open Watcom C linked against
# the Digital Research C run-time library (clears.l86), then run them on the
# Unicorn-based CP/M-86 harness.
#
# Targets:
#   ./build-owc-drc.sh            build+run the hello smoke test
#   ./build-owc-drc.sh mandel     build+run the fixed-point Mandelbrot kernel
#   ./build-owc-drc.sh mandel-ow  build+run the OW-specific IMUL Mandelbrot
#   ./build-owc-drc.sh dhry       fetch, build+run unmodified Dhrystone 2.1
#
# Prerequisites:
#   * ./drc/clears.l86 + headers  -> run ./fetch-drc.sh first
#   * Open Watcom native tools built in ../../../build/binbuild (bwcc, bwasm)
#   * Canonical CP/M-86 linker+emulator (overridable via LINK86=/EMU2=): the
#     authentic DR LINK-86 v1.4 (scratch/rc759-cmd-toolchain/drc86111/LINK86.CMD)
#     run under the emu2-cpm86 fork (scratch/cpm86-tools/emu2-cpm86/emu2)
#   * python3 with the 'unicorn' package (for cpm86run_unicorn.py)
#
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
XDEV="${XDEV:-$REPO/../cpm86-crossdev}"   # top-level ~/z80/cpm86-crossdev (nested dup removed; overridable)
BIN="$REPO/build/binbuild"
# CANONICAL TOOLCHAIN (see wlink-cpm86-plan.md): the AUTHENTIC DR LINK-86 v1.4
# (19 March 1984) -- the same native CP/M-86 linker DR C v1.11 uses -- run under
# the emu2-cpm86 fork (executes a CP/M-86 .CMD natively).  NOT linkcmd.exe (LINK-86
# v2.02, 1987), which is anachronistic; the older XDEV emu2 cannot run a .CMD
# directly.  Both paths overridable.
WS="$(cd "$HERE/../../../.." && pwd)"      # workspace root (/Users/ravn/z80)
EMU2="${EMU2:-$WS/scratch/cpm86-tools/emu2-cpm86/emu2}"
LINK86="${LINK86:-$WS/scratch/rc759-cmd-toolchain/drc86111/LINK86.CMD}"
RUNNER="$HERE/../cpm86run_unicorn.py"

# Shared Open Watcom flags:
#   -0 -ms   8086, small memory model (CP/M-86 small/8080 CMD)
#   -s -zl   no stack checks, no default library references
#   -ecc     cdecl calling convention (matches DR C)
#   -fpi87   inline 8087 floating point (avoids Watcom float helper calls
#            such as FIDRQQ/FIWRQQ, which DR C's library does not provide)
#   -nt=CODE put code in segment CODE so it merges with DR C's CODE segment
#   -fi=compat.h  force-include the no-underscore naming shim
CFLAGS="-0 -ms -s -zl -ecc -fpi87 -nt=CODE -fi=compat.h"

for t in "$BIN/bwcc" "$BIN/bwasm" "$EMU2" "$LINK86" "$RUNNER"; do
    [ -e "$t" ] || { echo "error: missing prerequisite: $t" >&2; exit 1; }
done
[ -f "$HERE/drc/clears.l86" ] || { echo "error: drc/clears.l86 missing; run ./fetch-drc.sh first" >&2; exit 1; }

TARGET="${1:-hello}"

# Everything below runs INSIDE a short work directory with bare filenames.  The
# Watcom compiler stamps the absolute source path into the OMF THEADR record and
# DR LINK-86 rejects long ones (OBJECT FILE ERROR 10); a short /tmp path plus
# bare names keeps it small, and also makes -i. resolve to the DR C headers.
WORK="$(mktemp -d /tmp/owcdrc.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
cp "$HERE/compat.h" "$HERE/owcrt.asm" "$HERE/drc/clears.l86" "$WORK/"
cp "$HERE"/drc/*.h "$WORK/" 2>/dev/null || true
cp "$LINK86" "$WORK/LINK86.CMD"
cp "$HERE/drc/clears.l86" "$WORK/CLEARS.L86"
cd "$WORK"

# owmath_objs -- assemble Open Watcom's OWN 32-bit long helpers (__U4M/__I4M/
# __U4D/__I4D) from its cgsupp sources into I4M.OBJ + I4D.OBJ in the work dir,
# ready to add to a link as "I4M,I4D".  These replace the former hand-written
# owmath.asm: runtime helpers must come from the compiler, never from us.  The
# small model's near CALL needs the helper _TEXT folded into CODE, so we pass
# --merge-text-into-code (see omf-delocal.py).
owmath_objs() {
    "$BIN/bwasm" -0 -ms -q -i="$REPO/bld/watcom/h" "$REPO/bld/clib/cgsupp/a/i4m.asm" -fo=I4M0.OBJ >/dev/null
    "$BIN/bwasm" -0 -ms -q -i="$REPO/bld/watcom/h" "$REPO/bld/clib/cgsupp/a/i4d.asm" -fo=I4D0.OBJ >/dev/null
    python3 "$HERE/stdcbench/omf-delocal.py" --merge-text-into-code I4M0.OBJ I4M.OBJ
    python3 "$HERE/stdcbench/omf-delocal.py" --merge-text-into-code I4D0.OBJ I4D.OBJ
}

link() {   # link <OUT> <obj1,obj2,...>
    local out="$1" objs="$2"
    # DR LINK-86 v1.4 run natively under emu2-cpm86 (drive A = the work dir).
    EMU2_DRIVE_A=. EMU2_DEFAULT_DRIVE=A \
        "$EMU2" LINK86.CMD "$out=$objs,CLEARS.L86[S]" >link.log 2>&1 || true
    if grep -qiE 'Undefined|OBJECT FILE ERROR|TARGET OUT OF RANGE|NO FILE' link.log; then
        echo "!! link problems:" >&2; grep -iE 'Undefined|OBJECT|ERROR|RANGE|NO FILE|Symbol' link.log >&2
    fi
    [ -f "$out.CMD" ] || { echo "error: link produced no $out.CMD" >&2; cat link.log >&2; exit 1; }
}

"$BIN/bwasm" -0 -ms owcrt.asm -fo=OWCRT.OBJ >/dev/null

case "$TARGET" in
hello)
    cp "$HERE/hello.c" HELLO.C
    "$BIN/bwcc" $CFLAGS -Dmain=cmain HELLO.C -fo=HELLO.OBJ >/dev/null
    link HELLO "OWCRT,HELLO"
    cp HELLO.CMD "$HERE/HELLO.CMD"
    echo "== running HELLO.CMD =="
    python3 "$RUNNER" "$HERE/HELLO.CMD"
    ;;
dhry)
    # Fetch unmodified Dhrystone 2.1 (Keith Thompson's faithful mirror).
    DHRY="$HERE/dhry21"
    if [ ! -f "$DHRY/dhry_1.c" ]; then
        mkdir -p "$DHRY"
        base="https://raw.githubusercontent.com/Keith-S-Thompson/dhrystone/master/v2.1"
        for f in dhry_1.c dhry_2.c dhry.h; do curl -fsSL -o "$DHRY/$f" "$base/$f"; done
    fi
    cp "$DHRY/dhry_1.c" DHRY_1.C
    cp "$DHRY/dhry_2.c" DHRY_2.C
    cp "$DHRY/dhry.h"   dhry.h
    cp "$HERE/glue.c" GLUE.C
    # dhry-time.h is a tiny <time.h> shim (CLK_TCK, clock_t) so the unmodified
    # Dhrystone builds with its MSC_CLOCK "hi-res clock" path; -i. then resolves
    # <time.h> to it in this work dir.
    cp "$HERE/dhry-time.h" time.h
    # -DMSC_CLOCK selects the clock()-based timing path (HZ == CLK_TCK == 1000);
    # glue.c's clock() reads the RC759 XIOS "16 ms counter" (Int 28h function 19)
    # and returns milliseconds (16 ms resolution);
    # -Dmain=cmain avoids Open Watcom's special-casing of the name "main";
    # -i. finds the DR C headers (and the time.h shim) copied into this work dir.
    for u in DHRY_1 DHRY_2; do
        "$BIN/bwcc" $CFLAGS -DMSC_CLOCK -Dmain=cmain -i. "$u.C" -fo="$u.OBJ" >/dev/null 2>&1
    done
    "$BIN/bwcc" $CFLAGS GLUE.C -fo=GLUE.OBJ >/dev/null
    # Open Watcom's OWN 32-bit long helpers (__U4M/__I4M/__U4D/__I4D) that DR C
    # lacks; glue.c's clock()/time() do 32-bit multiplies, so the dhry link needs
    # them too (as the stdcbench link does).
    owmath_objs
    link DHRY "OWCRT,DHRY_1,DHRY_2,GLUE,I4M,I4D"
    cp DHRY.CMD "$HERE/DHRY.CMD"
    echo "== running DHRY.CMD (50 runs) =="
    printf '50\n' | python3 "$RUNNER" "$HERE/DHRY.CMD"
    ;;
mandel)
    # 80x25 ASCII Mandelbrot, fixed-point 8.8 (owc-drc/mandel.c), ported from
    # the llvm-z80 test-gen example.  Output is deterministic (no timing, no
    # input); the only I/O primitive is the shared putchar.asm (BDOS C_WRITE),
    # so it links WITHOUT DR C's buffered stdio.  Open Watcom's OWN cgsupp
    # helpers supply the 32-bit long helpers FP_MUL's (long)a*b>>8 needs.
    cp "$HERE/mandel.c" MANDEL.C
    cp "$HERE/putchar.asm" putchar.asm
    "$BIN/bwasm" -0 -ms putchar.asm -fo=PUTCHAR.OBJ >/dev/null
    owmath_objs
    "$BIN/bwcc" $CFLAGS -Dmain=cmain MANDEL.C -fo=MANDEL.OBJ >/dev/null
    link MANDEL "OWCRT,MANDEL,PUTCHAR,I4M,I4D"
    cp MANDEL.CMD "$HERE/MANDEL.CMD"
    echo "== running MANDEL.CMD =="
    python3 "$RUNNER" "$HERE/MANDEL.CMD"
    ;;
mandel-ow)
    # Open-Watcom-SPECIFIC Mandelbrot (owc-drc/mandel-ow.c): identical output to
    # the mandel target, but FP_MUL is a #pragma aux routine lowering to a single
    # 16x16 IMUL + byte-extract instead of the portable (long)a*b>>8 idiom, which
    # Open Watcom compiles to a 32x32 __I4M call + carry-chained shift loop.  ~4.6x
    # fewer clocks; needs NO owmath (no __I4M).  Its output stays byte-identical to
    # the DR C oracle (the transform is exact), but it deliberately breaks the
    # "one source, both compilers" design -- DR C v1.11 cannot express inline IMUL
    # -- so it is NOT built by the pure-drc pipeline.
    cp "$HERE/mandel-ow.c" MOW.C
    cp "$HERE/putchar.asm" putchar.asm
    "$BIN/bwasm" -0 -ms putchar.asm -fo=PUTCHAR.OBJ >/dev/null
    "$BIN/bwcc" $CFLAGS -Dmain=cmain MOW.C -fo=MOW.OBJ >/dev/null
    link MOW "OWCRT,MOW,PUTCHAR"
    cp MOW.CMD "$HERE/MANDEL-OWIMUL.CMD"
    echo "== running MANDEL-OWIMUL.CMD =="
    python3 "$RUNNER" "$HERE/MANDEL-OWIMUL.CMD"
    ;;
stdcbench)
    # Build stdcbench 0.8 (Philipp Klaus Krause) -- the c90base + c90lib
    # integer benchmark modules -- on Open Watcom C + DR C.  Float/double
    # modules are excluded (portme.h #undefs C90FLOAT/C90DOUBLE).
    #
    # Three things beyond the hello/dhry pipeline are needed:
    #   * inc/           -- neutral ANSI C90 headers (DR C predates ANSI, so
    #                       it lacks mem*, strstr, strtol, linkable ctype);
    #                       cpmlibc.c supplies those routines.
    #   * omf-delocal.py -- rewrites Open Watcom's LEXTDEF/LPUBDEF (0xB4/0xB6,
    #                       emitted for file-scope statics) to plain EXTDEF/
    #                       PUBDEF, which 1987 DR LINK-86 accepts.
    #   * cgsupp helpers -- Open Watcom's OWN 32-bit long helpers __U4M/__I4M/
    #                       __U4D/__I4D, pulled in by the unsigned-long score
    #                       arithmetic (assembled from bld/clib/cgsupp/a).
    #
    # portme.c also repairs DR C's heap base (see its comments): malloc()
    # would otherwise hand out memory on top of the program's static data.
    SCB="$HERE/stdcbench"
    SRC="$SCB/src/stdcbench-0.8"
    if [ ! -d "$SRC" ]; then
        mkdir -p "$SCB/src"
        curl -fsSL -o "$SCB/src/stdcbench-0.8.tar.gz" \
            "https://downloads.sourceforge.net/project/stdcbench/stdcbench-0.8.tar.gz"
        tar xzf "$SCB/src/stdcbench-0.8.tar.gz" -C "$SCB/src"
    fi
    # Neutral libc headers, our glue and the OMF filter.
    cp -R "$SCB/inc" inc
    cp "$SCB/portme.h" "$SCB/portme.c" "$SCB/cpmlibc.c" \
       "$SCB/omf-delocal.py" .
    # Upstream headers (portme.h above deliberately shadows the upstream one).
    cp "$SRC/stdcbench.h" "$SRC/c90base-huffman.h" \
       "$SRC/c90lib-htab.h" "$SRC/c90lib-peep.h" .
    # Upstream translation units, compiled to short 8.3 names N00..N13 so the
    # THEADR that Open Watcom stamps stays short enough for DR LINK-86.
    upstream="c90base.c c90base-data.c c90base-compression.c c90base-isort.c \
c90base-immul.c c90base-huffman-recursive.c c90base-huffman-iterative.c \
c90base-huffman_tree.c c90lib.c c90lib-lnlc.c c90lib-peep.c \
c90lib-peep-stm8.c c90lib-htab.c stdcbench.c"
    SCFLAGS="$CFLAGS -Iinc"
    objs="OWCRT"; i=0
    for f in $upstream; do
        nn="$(printf 'N%02d' "$i")"
        cp "$SRC/$f" "$nn.C"
        "$BIN/bwcc" $SCFLAGS "$nn.C" -fo="$nn.OBJ" >/dev/null 2>&1
        python3 omf-delocal.py "$nn.OBJ" "$nn.OBJ"
        objs="$objs,$nn"; i=$((i + 1))
    done
    # Our glue: cpmlibc (missing ANSI routines) and portme (entry/clock/heap).
    for g in cpmlibc portme; do
        nn="$(printf 'N%02d' "$i")"
        cp "$g.c" "$nn.C"
        "$BIN/bwcc" $SCFLAGS "$nn.C" -fo="$nn.OBJ" >/dev/null 2>&1
        python3 omf-delocal.py "$nn.OBJ" "$nn.OBJ"
        objs="$objs,$nn"; i=$((i + 1))
    done
    owmath_objs
    objs="$objs,I4M,I4D"
    link SCB "$objs"
    cp SCB.CMD "$HERE/SCB.CMD"
    echo "== running SCB.CMD =="
    # stdcbench times itself with the Concurrent CP/M-86 BDOS T_SECONDS call
    # (fn 155), read at 1-second resolution -- see stdcbench/portme.c.  Under the
    # Unicorn runner that clock is the deterministic code-byte counter, which
    # only advances when the block hook is installed, i.e. in --count mode; a
    # plain run leaves elapsed time at 0 and the score normalises to 0.  So run
    # with --count.  One benchmark iteration is heavy, so the emulated CPU rate
    # (CPM86_CLOCK_HZ) must be high enough that an iteration fits inside the
    # module's timing window (8 s / 40 s); 700000 keeps each module at one
    # (cheap) iteration while giving reproducible non-zero scores.
    CPM86_CLOCK_HZ="${CPM86_CLOCK_HZ:-700000}" python3 "$RUNNER" --count "$HERE/SCB.CMD"
    ;;
*)
    echo "usage: $0 [hello|mandel|mandel-ow|dhry|stdcbench]" >&2; exit 2;;
esac
