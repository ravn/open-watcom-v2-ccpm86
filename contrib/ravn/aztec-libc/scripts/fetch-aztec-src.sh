#!/usr/bin/env bash
# fetch-aztec-src.sh -- Phase 0 of ravn/open-watcom-v2#13.
#
# Fetches the Aztec C 3.4 distribution (az8634b.zip) and extracts the CP/M-86
# library SOURCE archives (*.ARC) into ./src/.  The Aztec sources are
# proprietary (Manx/Harry Suckow copyright; aztecmuseum.ca redistributes for
# Fair-Use educational/enthusiast purposes only) -- they are fetched locally
# and NEVER committed (see ../.gitignore).
#
# Requires: curl, unzip, and an emu2 DOS emulator (to run Aztec's own ARCV.COM,
# the only tool that understands the old .ARC format).  Point EMU2 at a binary
# or leave it to auto-discover one under the sibling cpm86-crossdev tree or PATH.
#
# Usage:  ./fetch-aztec-src.sh
# Result: ./src/<ARCHIVE>/*.c and *.asm for CPM86 STDIO MISC MCH86 MATH (+more).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"        # contrib/ravn/aztec-libc
src="$here/src"
work="$here/work"
url="https://www.aztecmuseum.ca/az8634b.zip"

# --- locate emu2 (DOS) --------------------------------------------------------
find_emu2() {
    if [ -n "${EMU2:-}" ] && [ -x "$EMU2" ]; then echo "$EMU2"; return; fi
    local c
    for c in \
        "$here/../cpm86-crossdev/bin/emu2" \
        "$here/../cpm86-crossdev/emu2/emu2" \
        "$(command -v emu2 2>/dev/null || true)"; do
        [ -n "$c" ] && [ -x "$c" ] && { echo "$c"; return; }
    done
    echo ""   # not found
}
EMU2_BIN="$(find_emu2)"
if [ -z "$EMU2_BIN" ]; then
    echo "ERR: no emu2 found. Set EMU2=/path/to/emu2 (a DOS emulator)." >&2
    exit 1
fi
echo "INF: using emu2 = $EMU2_BIN"

mkdir -p "$src" "$work"
cd "$work"

# --- download (cache locally) -------------------------------------------------
if [ ! -f az8634b.zip ]; then
    echo "INF: downloading $url"
    curl -fsSL -o az8634b.zip "$url"
fi
unzip -oq az8634b.zip 'AZ8634B/SRC/*.ARC' 'AZ8634B/BIN/ARCV.COM'
cp -f AZ8634B/BIN/ARCV.COM .

# Archives that make up (or feed) a CP/M-86 C library.  DOS11/DOS20 are the
# DOS OS layer (not CP/M-86); S/TERM/G are screen/graphics (out of scope for a
# base libc) -- extracted too so enumeration is complete, but not the target.
ARCHIVES="CPM86 STDIO MISC MCH86 MATH S TERM G DOS11 DOS20"

for a in $ARCHIVES; do
    arc="AZ8634B/SRC/$a.ARC"
    [ -f "$arc" ] || { echo "WARN: $arc missing, skipping"; continue; }
    d="$src/$a"
    rm -rf "$d"; mkdir -p "$d"
    cp "$arc" "$d/$a.ARC"; cp ARCV.COM "$d/"
    ( cd "$d"; "$EMU2_BIN" ARCV.COM "$a.ARC" >/dev/null 2>&1; rm -f ARCV.COM "$a.ARC" )
    printf "INF: %-8s -> %3d files\n" "$a" "$(ls "$d" | wc -l | tr -d ' ')"
done

echo "OK: Aztec CP/M-86 sources extracted under $src (uncommitted)."
