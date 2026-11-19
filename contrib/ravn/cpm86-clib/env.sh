# Source this to set up the environment for the owcc -bcpm86 one-command build:
#     . contrib/ravn/cpm86-clib/env.sh
# then:
#     owcc -bcpm86 -march=i186 -mcmodel=s prog.c -o PROG.CMD
#
# It exposes the native osxa64 tools under the bare names owcc drives them by
# (wcc/wasm/wlink/wlib), points WATCOM at the tree (so wlink resolves
# %WATCOM%/lib286/cpm86) and WLINK_LNK at the config that defines `system
# cpm86` (without it wlink reports "undefined system name: cpm86"). These are
# the standard "setvars" bits every Watcom target needs in an UNINSTALLED tree
# (INCLUDE, tool PATH, WLINK_LNK) — none are cpm86-specific.
#
# NOTE: the CP/M-86 C runtime lib286/cpm86/{clibs.lib,cstartcpm.obj} is now a
# FIRST-CLASS standard-build target (bld/clib/_cpm + bld/clib/library/cpm86.086;
# builder.ctl installs it). Build it with the standard driver, e.g. from
# bld/clib:  `pmake -d cpm86`  (component _cpm then the merge). The old
# contrib build-lib.sh / build.sh scripts are NO LONGER required.
_OW="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../../.." && pwd)"
_B="$_OW/bld"
# Host platform build-dir token (osxa64 on the Apple-Silicon macbook, linuxx64
# on sonnyboy, etc.). Derived from uname so this script is host-agnostic — the
# NATIVE host build, not a cross-hosted tree dir like dosi86 (which also holds
# an owcc.exe but is a DOS-hosted binary that can't run here).
_uS="$(uname -s)"; _uM="$(uname -m)"
case "$_uS/$_uM" in
    Darwin/arm64)          _PLAT=osxa64 ;;
    Darwin/x86_64)         _PLAT=osxx64 ;;
    Linux/x86_64)          _PLAT=linuxx64 ;;
    Linux/aarch64|Linux/arm64) _PLAT=linuxa64 ;;
    Linux/i?86)            _PLAT=linux386 ;;
    *)                     _PLAT="" ;;
esac
if [ -z "$_PLAT" ] || [ ! -f "$_B/wcl/owcc/$_PLAT/owcc.exe" ]; then
    echo "env.sh: no native owcc.exe at $_B/wcl/owcc/${_PLAT:-<unknown>}/ for $_uS/$_uM — build the toolchain first (sh build.sh)" >&2
    return 1 2>/dev/null || exit 1
fi
_BIN="${TMPDIR:-/tmp}/owcc-cpm86-bin"
mkdir -p "$_BIN"
ln -sf "$_B/cc/i86/$_PLAT/binbuild/wcc.exe" "$_BIN/wcc"
ln -sf "$_B/wasm/$_PLAT/wasm.exe"           "$_BIN/wasm"
ln -sf "$_B/wl/$_PLAT/wlink.exe"            "$_BIN/wlink"
ln -sf "$_B/nwlib/$_PLAT/wlib.exe"          "$_BIN/wlib"
ln -sf "$_B/wcl/owcc/$_PLAT/owcc.exe"       "$_BIN/owcc"
cp     "$_B/wcl/owcc/$_PLAT/specs.owc"      "$_BIN/specs.owc"
export PATH="$_BIN:$PATH"
export WATCOM="$_OW"
export OWROOT="$_OW"
export WLINK_LNK="$_B/wl/lnk/$_PLAT/wlink.lnk"
# Header search for a bare `owcc -bcpm86 prog.c` (owcc does not set these for
# the cpm86 target itself). Mirrors the -I list used by the UnZip build.
export INCLUDE="$_B/clib/h:$_B/clib/intel/h:$_B/watcom/h:$_B/lib_misc/h:$_B/hdr/dos/h"
echo "owcc -bcpm86 environment ready (WATCOM=$WATCOM, plat=$_PLAT)"
