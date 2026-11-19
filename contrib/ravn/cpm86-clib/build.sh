#!/bin/bash
# ===========================================================================
# DEPRECATED (2026-08): superseded by
#   contrib/ravn/watcom-cpm86-libc/build-lib.sh
# which builds the FULL CP/M-86 C library (183 modules incl. stdio, malloc,
# string, and a real time()/__getctime seam) and installs it as the canonical
# lib286/cpm86/{clibs.lib,cstartcpm.obj}. This script only ever produced a
# 4-module proof-of-concept stub (putchar, i4m, i4d, strlen) and is kept for
# reference/history only. Use build-lib.sh instead.
# ===========================================================================
# build.sh -- regenerate the CP/M-86 C-runtime artifacts that the Open Watcom
# "owcc -bcpm86" one-command build depends on, entirely FROM SOURCE IN GIT.
#
# Produces (into $WATCOM/lib286/cpm86/, a .gitignored install dir):
#     cstartcpm.obj   CP/M-86 startup   (from ./cstartcpm.asm)
#     clibs.lib       CP/M-86 C library (from ./putchar.c + Watcom clib source)
#
# These two files are the pieces wlink's `system begin cpm86` block references
# (`libfile cstartcpm.obj` + auto-fetch of `clibs.lib` from `%WATCOM%/lib286/
# cpm86`).  They are build OUTPUT, git-ignored by .gitignore's `/*/` rule, so
# they vanish on `clean.sh`.  Everything needed to rebuild them, however, is in
# git: this script, cstartcpm.asm, putchar.c, and the stock Watcom clib sources
# under bld/clib/.  Re-run this after any clean.
#
# Once built, a single command links a CP/M-86 .CMD:
#     WLINK_LNK=$OW/bld/wl/lnk/osxa64/wlink.lnk \
#     owcc -bcpm86 -march=i186 -mcmodel=s prog.c -o PROG.CMD
# (WLINK_LNK points wlink at the config that defines `system cpm86`; source
#  env.sh from this directory to set WATCOM/WLINK_LNK/PATH in one step.)
# ===========================================================================
set -e
cd "$(dirname "$0")"
HERE="$(pwd)"
OW="${OW:-$(cd ../../.. && pwd)}"
B="$OW/bld"

# --- native tools (owcc drives children by bare name; expose unsuffixed) ---
BIN="$(mktemp -d)"
ln -sf "$B/cc/i86/osxa64/binbuild/wcc.exe" "$BIN/wcc"
ln -sf "$B/wasm/osxa64/wasm.exe"           "$BIN/wasm"
ln -sf "$B/wl/osxa64/wlink.exe"            "$BIN/wlink"
ln -sf "$B/nwlib/osxa64/wlib.exe"          "$BIN/wlib"
ln -sf "$B/wcl/owcc/osxa64/owcc.exe"       "$BIN/owcc"
cp     "$B/wcl/owcc/osxa64/specs.owc"      "$BIN/specs.owc"
export PATH="$BIN:$PATH" WATCOM="$OW" OWROOT="$OW"
export WLINK_LNK="$B/wl/lnk/osxa64/wlink.lnk"
trap 'rm -rf "$BIN"' EXIT

DEST="$OW/lib286/cpm86"
mkdir -p "$DEST"
WORK="$(mktemp -d)"

# Include path for building stock Watcom clib source (mirrors the port build).
INC="-i=$B/lib_misc/h -i=$B/clib/h -i=$B/clib/intel/h -i=$B/watcom/h -i=$B/hdr/dos/h"
# Compile Watcom clib modules the same way the DOS 16-bit small-model library
# does: -bt=dos picks the 16-bit host, -ms small model, -1 80186 (the RC759
# Piccoline CPU -- unlocks shl/shr r,imm8, imul r,imm, push imm, enter/leave),
# -zl suppress the auto-lib record inside the members, -x no default includes.
CLIB="-bt=dos -1 -ms -zl -zastd=c99 -x"

echo "==> startup: cstartcpm.obj"
wasm -ms -1 "$HERE/cstartcpm.asm" -fo="$WORK/cstartcpm.obj"

echo "==> clib members"
# console seam (our CP/M-86 BDOS putchar); -march=i186 targets the RC759 CPU
owcc -bcpm86 -march=i186 -mcmodel=s -O2 -c "$HERE/putchar.c" -o "$WORK/putchar.obj"
# 32-bit long multiply / divide helpers (stock Watcom cgsupp; 8086 mnemonics
# only, so -1 just keeps the assembler CPU level uniform with the rest)
wasm -ms -1 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo="$WORK/i4m.obj"
wasm -ms -1 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo="$WORK/i4d.obj"
# a genuine stock Watcom clib module, to prove auto-fetch of real clib code
wcc $CLIB $INC "$B/clib/string/c/strlen.c" -fo="$WORK/strlen.obj"

echo "==> archive clibs.lib"
rm -f "$WORK/clibs.lib"
wlib -q -b "$WORK/clibs.lib" \
    +"$WORK/putchar.obj" +"$WORK/i4m.obj" +"$WORK/i4d.obj" +"$WORK/strlen.obj"

echo "==> install into $DEST"
cp "$WORK/cstartcpm.obj" "$DEST/cstartcpm.obj"
cp "$WORK/clibs.lib"     "$DEST/clibs.lib"
rm -rf "$WORK"
ls -l "$DEST"

# --- self-test: single-command owcc -bcpm86 build + emu2 oracle -------------
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
MSRC="$OW/contrib/ravn/owc-drc/mandel.c"
DRC="$OW/contrib/ravn/owc-drc/MANDEL-DRC.CMD"     # independent DR C oracle
if [ -f "$MSRC" ]; then
    T="$(mktemp -d)"
    echo "==> self-test: owcc -bcpm86 mandel.c -o MANDEL.CMD  (single command)"
    # RC759 target: -march=i186 (80186).  The former wcc ICE 97 at -O1+ on
    # mandel.c's ternary/string-index line was a stale incremental binary, not a
    # stock bug -- gone after a clean rebuild (2026-08-16) -- so this builds -O2.
    ( cd "$T" && owcc -bcpm86 -march=i186 -mcmodel=s -O2 "$MSRC" -o MANDEL.CMD )
    ls -l "$T/MANDEL.CMD"
    if [ -x "$EMU2" ] && [ -f "$DRC" ]; then
        ( cd "$T" && EMU2_DRIVE_A=. EMU2_DEFAULT_DRIVE=A "$EMU2" MANDEL.CMD 2>/dev/null | tr -d '\r' >ours.txt
          cp "$DRC" MANDEL-DRC.CMD
          EMU2_DRIVE_A=. EMU2_DEFAULT_DRIVE=A "$EMU2" MANDEL-DRC.CMD 2>/dev/null | tr -d '\r' >drc.txt )
        if diff -q "$T/ours.txt" "$T/drc.txt" >/dev/null; then
            echo "PASS: owcc build byte-identical to independent DR C oracle"
        else
            echo "FAIL: mandel output differs from DR C oracle"; diff "$T/ours.txt" "$T/drc.txt" | head; rm -rf "$T"; exit 1
        fi
    else
        echo "(skipped emu2 oracle: emu2 or DR C reference not found)"
    fi
    rm -rf "$T"
fi
echo "DONE."
