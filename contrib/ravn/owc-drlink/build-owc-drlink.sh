#!/bin/sh
# ---------------------------------------------------------------------------
# build-owc-drlink.sh - build a CP/M-86 .CMD from Open Watcom C by linking the
# Watcom OMF output with Digital Research's native CP/M-86 linker (LINK86).
#
# Unlike wl's own `format cpm86` output (the native route in this tree), this
# path proves that Open Watcom's OMF object output is directly consumable by a
# *real CP/M-86 linker*. DR LINK86 reads the Watcom .obj files and emits a
# proper .CMD with correct group descriptors.
#
# KEY FACTS (verified, see README.md):
#   * Open Watcom emits Intel/MS OMF; DR LINK-86 v1.4 (1984) accepts it as-is.
#   * Compile C with -ecc so Watcom uses cdecl (stack args, leading underscore),
#     matching a classic C-runtime ABI.
#   * Do NOT name the C entry `main` (that pulls Watcom's _cstart_ and exports
#     main_). Use a plain name (here: cmain) so it links against crt.asm.
#
# Prerequisites:
#   1. Built Open Watcom cross-tools (bwcc/bwasm or wcc/wasm) - run ./build.sh.
#   2. The cpm86-crossdev submodule populated with DR tools + emu2:
#        cd contrib/ravn/cpm86-crossdev
#        ./fetch_tools           # or the individual src/fetch/* + buildemu2
#      This provides bin/emu2, bin/pcdev_linkcmd and (for running) bin/cpm86.
#
# Usage: ./build-owc-drlink.sh [hello.c]
# ---------------------------------------------------------------------------
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-"$HERE/hello.c"}
STEM=$(basename "$SRC" | sed 's/\.[^.]*$//')
CMD=$(echo "$STEM" | tr '[:lower:]' '[:upper:]').CMD

CPU=${CPU:-0}

# Tools: prefer released names, fall back to bootstrap b-prefixed cross-tools.
REPO=$(cd "$HERE/../../.." && pwd)
BINB="$REPO/build/binbuild"
pick() { for t in "$@"; do command -v "$t" >/dev/null 2>&1 && { echo "$t"; return; }; done; echo "$2"; }
WCC=$(pick wcc "$BINB/bwcc")
WASM=$(pick wasm "$BINB/bwasm")

XDEV="${XDEV:-$REPO/../cpm86-crossdev}"   # top-level ~/z80/cpm86-crossdev (nested dup removed; overridable)
# CANONICAL TOOLCHAIN (see wlink-cpm86-plan.md): authentic DR LINK-86 v1.4
# (19 March 1984), the same native CP/M-86 linker DR C 1.11 uses, run under the
# emu2-cpm86 fork (executes a CP/M-86 .CMD natively).  NOT linkcmd.exe (LINK-86
# v2.02, 1987).  Both overridable.
WS=$(cd "$HERE/../../../.." && pwd)       # workspace root (/Users/ravn/z80)
EMU2="${EMU2:-$WS/scratch/cpm86-tools/emu2-cpm86/emu2}"
LINK86="${LINK86:-$WS/scratch/rc759-cmd-toolchain/drc86111/LINK86.CMD}"

[ -x "$WCC" ]  || { echo "ERR: Watcom C compiler not found ($WCC). Run ./build.sh." >&2; exit 1; }
[ -x "$EMU2" ] || { echo "ERR: emu2-cpm86 not found ($EMU2)." >&2; exit 1; }
[ -f "$LINK86" ] || { echo "ERR: DR LINK-86 v1.4 missing ($LINK86)." >&2; exit 1; }

WORK=$(mktemp -d /tmp/owcdr.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

# DR LINK86 has a short limit on the OMF THEADR (module-name) string, which the
# Watcom compiler fills with the *absolute* source path. Compile from a
# short-pathed work dir with bare filenames so THEADR stays within the limit.
cp "$SRC" "$WORK/HELLO.C"
cp "$HERE/crt.asm" "$WORK/CRT.ASM"
cp "$LINK86" "$WORK/LINK86.CMD"

# 1. Compile C to OMF: 8086 (-$CPU), small model (-ms), no stack checks (-s),
#    no default library refs (-zl), force cdecl ABI (-ecc).
( cd "$WORK" && "$WCC" "-$CPU" -ms -s -zl -ecc HELLO.C -fo=HELLO.OBJ )

# 2. Assemble the tiny OMF runtime.
( cd "$WORK" && "$WASM" "-$CPU" CRT.ASM -fo=CRT.OBJ )

# 3. Link both OMF objects with DR LINK-86 v1.4 -> .CMD (runtime first = entry).
#    Run natively under emu2-cpm86 (drive A = the work dir).
#    LINK86 syntax: OUTPUT=INPUT1,INPUT2  (drops .OBJ / .CMD extensions).
( cd "$WORK" && EMU2_DRIVE_A=. EMU2_DEFAULT_DRIVE=A \
    "$EMU2" LINK86.CMD "HELLO=CRT,HELLO" ) \
    2>&1 | grep -iE 'undefined|no file|error|CODE|DATA' || true

cp "$WORK/HELLO.CMD" "$HERE/$CMD"
echo "OK: $HERE/$CMD"

# 4. Run it directly under the emu2-cpm86 fork (executes a CP/M-86 .CMD natively).
echo "--- run on CP/M-86 emulator ---"
( cd "$HERE" && EMU2_DRIVE_A=. EMU2_DEFAULT_DRIVE=A "$EMU2" "$CMD" ) 2>&1 \
    | grep -v 'Copyright\|emulator for DOS' || true
