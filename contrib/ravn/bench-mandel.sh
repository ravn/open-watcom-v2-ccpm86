#!/bin/sh
# bench-mandel.sh -- build the fixed-point Mandelbrot kernel at several
# optimisation levels with owcc -bcpm86 (Docker) and run each under emu2
# (Docker).  No DR C oracle, no Unicorn, no host toolchain needed.
#
# Variants:
#   O0     mandel.c    -O0   optimiser disabled
#   O2     mandel.c    -O2   full optimisation
#   OWIMUL mandel-ow.c -O2   FP_MUL via #pragma aux IMUL (OW-specific, ~8x faster)
#
# Usage: ./bench-mandel.sh
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/owc-drc"
OW_IMAGE="${OW_IMAGE:-open-watcom-cpm86:latest}"
EMU2_IMAGE="${EMU2_IMAGE:-emu2-cpm86:latest}"

owcc() {
    docker run --rm --platform linux/amd64 -v "$SRC":/work "$OW_IMAGE" owcc "$@"
}
emu2() {
    docker run --rm --platform linux/amd64 -v "$SRC":/work "$EMU2_IMAGE" emu2 "$@"
}

echo "Building Mandelbrot variants (owcc -bcpm86) ..."
owcc -bcpm86 -O0 -o mandel-O0.cmd    mandel.c
owcc -bcpm86 -O2 -o mandel-O2.cmd    mandel.c
owcc -bcpm86 -O2 -o mandel-OWIMUL.cmd mandel-ow.c
echo

for v in O0 O2 OWIMUL; do
    cmd="mandel-$v.cmd"
    size=$(wc -c < "$SRC/$cmd")
    printf "=== %-8s  %5d B ===\n" "$v" "$size"
    emu2 "$cmd"
    echo
done
