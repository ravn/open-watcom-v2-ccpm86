#!/bin/sh
# Stage B (medium model, -mm -zm) far-code load-time-relocation regression test.
#
# End-to-end PRODUCER + CONSUMER oracles: they drive the real wcc/wasm/wlink to
# build forced-split .CMD files whose cross-segment far references can only
# resolve if wlink emits correct CP/M-86 P_LOAD fixup records (header byte 127
# bit 7 + ch_fixrec + 4-byte records), then run them under cpm86run_unicorn.py
# and check the exact console output.  This complements test_cpm86_reloc.py,
# which tests only the runner's _apply_fixups() consumer with hand-fed records
# and so cannot catch a wrong paragraph emitted by the linker (the 2026-08-19
# image-layout bug: a far target's group-relative paragraph must come from the
# packed .CMD image layout, not from wlink's grp_addr.seg frame numbers).
#
# Oracles:
#   1. CALL   (test_stageb_farcall.c): CODE->CODE far calls, value oracle via
#             execution.  Output must be exactly "OK!\r\n".
#   2. POINTER(test_stageb_farptr.c):  DATA->CODE far pointers, MEMORY oracle --
#             follows each relocated pointer and checks the bytes there are the
#             stub's expected code, independent of execution flow.  This also
#             exercises fixups LOCATED in DATA (record nibble 0x2X), which the
#             call oracle never does.  Output must be exactly "OK!\r\n".
#   3. SMALL  model (single CODE segment): header byte 127 bit 7 must be CLEAR
#             (no spurious fixup table -- small-model regression guard).
#
# Requires a built native wcc + wasm + wlink and cpm86run_unicorn.py.  Source
# the toolchain env first (contrib/ravn/cpm86-clib/env.sh) so the tools are on
# PATH, or point $WCC/$WASM/$WLINK at them.  Exits non-zero on any failure.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RUNNER="$HERE/cpm86run_unicorn.py"

WCC=${WCC:-wcc}
WASM=${WASM:-wasm}
WLINK=${WLINK:-wlink}
PY=${PYTHON:-python3}

for t in "$WCC" "$WASM" "$WLINK"; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not found (source cpm86-clib/env.sh)" >&2; exit 77; }
done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

fail() { echo "FAIL: $1" >&2; exit 1; }
byte_at() { od -An -j "$2" -N1 -tu1 "$1" | tr -d ' \n'; }   # decimal byte at offset $2

EXP=$(printf 'OK!\r\n')

# Base-page reservation shared by both freestanding medium-model programs.
"$WASM" -bt=dos "$HERE/test_stageb_begdata.asm" -fo=begdata.obj >/dev/null 2>&1 \
    || fail "wasm base-page stub"

# link+run one medium-model oracle; assert fixups present and output == "OK!\r\n"
run_medium_oracle() {
    name=$1 src=$2
    "$WCC" -bt=dos -mm -zm -s "$src" -fo="$name.obj" >/dev/null 2>&1 || fail "$name: wcc -mm -zm"
    cat > "$name.lnk" <<EOF
format cpm86
option packcode=8
option start=cpmmain_
option undefsok
name $name.CMD
file begdata.obj
file $name.obj
EOF
    "$WLINK" @"$name.lnk" >/dev/null 2>&1 || fail "$name: wlink"

    LBYTE=$(byte_at "$name.CMD" 127)
    FIXLO=$(byte_at "$name.CMD" 125)
    FIXHI=$(byte_at "$name.CMD" 126)
    [ $(( LBYTE & 0x80 )) -ne 0 ] || fail "$name: header byte 127 bit 7 not set (no fixups emitted)"
    [ $(( FIXLO | FIXHI )) -ne 0 ] || fail "$name: ch_fixrec (word 0x7D) is zero"

    OUT=$("$PY" "$RUNNER" "$name.CMD" 2>/dev/null | tr -d '\000')
    [ "$OUT" = "$EXP" ] || fail "$name: output $(printf %s "$OUT" | od -An -tx1) != 'OK' CR LF (mislocated far reference)"
    echo "ok  $name: far references relocate + verify, output = OK CRLF, ch_fixrec set"
}

run_medium_oracle farcall "$HERE/test_stageb_farcall.c"   # CODE->CODE far calls
run_medium_oracle farptr  "$HERE/test_stageb_farptr.c"    # DATA->CODE far pointers

# small model: single CODE segment must NOT emit any fixup table
"$WCC" -bt=dos -ms -s "$HERE/test_stageb_farcall.c" -fo=small.obj >/dev/null 2>&1 || fail "small: wcc -ms"
cat > small.lnk <<EOF
format cpm86
option start=cpmmain_
option undefsok
name SMALL.CMD
file small.obj
EOF
"$WLINK" @small.lnk >/dev/null 2>&1 || fail "small: wlink"
SLBYTE=$(byte_at SMALL.CMD 127)
[ $(( SLBYTE & 0x80 )) -eq 0 ] || fail "small model wrongly set header byte 127 bit 7 (spurious relocation)"
echo "ok  small model emits no fixup table (header byte 127 bit 7 clear)"

echo
echo "3 passed"
