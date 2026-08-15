#!/bin/bash
# Reproducible proof: Open Watcom's OWN, UNCHANGED stream-I/O regression test
# (clibtest/streamio/c/iotest.c) running against REAL CP/M-86 disk + console
# through our thin Layer-2 seam. Success = the test runs to completion and prints
# "Tests completed (...)"; every VERIFY *and* EXPECT is fatal (exit(-1)), so a
# pass means the whole streamio surface behaves.
#
# This exercises far more than the disk oracle: fopen("CON")/freopen onto the std
# streams, fcloseall/flushall, dup+fdopen, setbuf/setvbuf, ungetc, perror, and
# the scanf family -- which (like fscanf) pulls scan_float() UNCONDITIONALLY, so
# we reuse the SAME 8087-free -fpc soft-float + msdos.086/286 stack proven in
# build-fscanf.sh / build-whetstone.sh. Purity gate: INT21h(DOS)=0.
# rc7xx-work GAP #3 (clibtest oracle) -- streamio component.
set -e
cd "$(dirname "$0")"
OW="${OW:-/Users/ravn/z80/scratch/open-watcom-v2}"; B="$OW/bld"
WCC="$B/cc/i86/osxa64/binbuild/wcc.exe"
WASM="$B/wasm/osxa64/wasm.exe"
WLINK="$B/wl/osxa64/wlink.exe"
WLIB="$B/nwlib/osxa64/wlib.exe"
WDIS="$B/ndisasm/osxa64/wdis.exe"
EMU2="${EMU2:-/Users/ravn/z80/scratch/cpm86-tools/emu2-cpm86/emu2}"
OUTDIR="${OUTDIR:-build-streamio}"; mkdir -p "$OUTDIR"; cd "$OUTDIR"
SRC=".."
TESTSRC="$B/clibtest/streamio/c/iotest.c"     # Watcom's UNCHANGED stream test
INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/handleio/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
AINC="-i=$B/watcom/h -i=$B/comp_cfg/h"
CLIB="-bt=dos -0 -ms -zastd=c99 -zl -x"        # compile Watcom clib source
USER="-bt=dos -0 -ms -zl -zastd=c99"           # compile our port + test

cw()  { "$WCC" $CLIB $INC "$B/clib/$1" -fo="$2"; }               # clib source
cwf() { "$WCC" $CLIB -fpc -i=$B/mathlib/h $INC "$B/clib/$1" -fo="$2"; } # float-bearing
aw()  { "$WASM" -ms -0 $AINC "$1" -fo="$2"; }                    # asm source

# --- Watcom clib: shared __prtf formatter + %ld converters (stdio write path) --
cw streamio/c/prtf.c     prtf.obj
cw streamio/c/noefgfmt.c noefgfmt.obj      # also defines __EFG_scanf stub
cw string/c/strupr.c     strupr.obj
cw string/c/strlen.c     strlen.obj
cw string/c/strcmp.c     strcmp.obj
cw string/c/strcpy.c     strcpy.obj
cw convert/c/itoa.c      itoa.obj
cw convert/c/ltoa.c      ltoa.obj
cw convert/c/lltoa.c     lltoa.obj
cw convert/c/alphabet.c  alphabet.obj
cw mbyte/c/wctomb.c      wctomb.obj

# --- Watcom clib: the GENUINE FILE* stdio read+write path (UNCHANGED) ---------
cw streamio/c/printf.c   printf.obj
cw streamio/c/fprintf.c  fprintf.obj
cw streamio/c/fprtf.c    fprtf.obj
cw streamio/c/fputc.c    fputc.obj
cw streamio/c/fputs.c    fputs.obj
cw streamio/c/puts.c     puts.obj
cw streamio/c/fwrite.c   fwrite.obj
cw streamio/c/flush.c    flush.obj
cw streamio/c/fflush.c   fflush.obj
cw streamio/c/fopen.c    fopen.obj
cw streamio/c/fclose.c   fclose.obj
cw streamio/c/allocfp.c  allocfp.obj
cw streamio/c/fgetc.c    fgetc.obj
cw streamio/c/fgets.c    fgets.obj
cw streamio/c/fread.c    fread.obj
cw streamio/c/fseek.c    fseek.obj
cw streamio/c/ftell.c    ftell.obj
cw streamio/c/rewind.c   rewind.obj
cw streamio/c/feof.c     feof.obj
cw streamio/c/ferror.c   ferror.obj
cw streamio/c/ungetc.c   ungetc.obj        # fscanf push-back
cw streamio/c/ioalloc.c  ioalloc.obj
cw streamio/c/chktty.c   chktty.obj
cw streamio/c/iob.c      iob.obj
cw streamio/c/initfile.c initfile.obj
cw streamio/c/comtflag.c comtflag.obj
cw streamio/c/freefp.c   freefp.obj
cw handleio/c/textmode.c textmode.obj

# --- streamio surface UNIQUE to iotest.c (all UNCHANGED Watcom clib source) ----
cw handleio/c/iomode.c   iomode.obj        # __GetIOMode/__SetIOMode + __io_mode
cw handleio/c/stiomode.c stiomode.obj      # __SetIOMode_grow/__grow_iomode
cw handleio/c/fileno.c   fileno.obj        # fileno(stdout)
cw streamio/c/fdopen.c   fdopen.obj        # fdopen(handle,mode)
cw streamio/c/flushall.c flushall.obj      # fcloseall()/flushall()
cw streamio/c/setbuf.c   setbuf.obj
cw streamio/c/setvbuf.c  setvbuf.obj
cw streamio/c/clearerr.c clearerr.obj
cw streamio/c/fsetpos.c  fsetpos.obj
cw streamio/c/fgetpos.c  fgetpos.obj
cw streamio/c/getc.c     getc.obj
cw streamio/c/putc.c     putc.obj
cw streamio/c/putchar.c  putchar.obj
cw streamio/c/fputchar.c fputchar.obj
cw streamio/c/getchar.c  getchar.obj
cw streamio/c/fgetchar.c fgetchar.obj
cw streamio/c/gets.c     gets.obj
cw streamio/c/perror.c   perror.obj
cw streamio/c/scanf.c    scanf.obj         # scanf() + vscanf()
cw streamio/c/vfprintf.c vfprintf.obj      # Test_vfprintf
cw streamio/c/vprintf.c  vprintf.obj       # Test_vprintf
cw startup/c/ioexit.c    ioexit.obj        # fcloseall() + real __full_io_exit
cw handleio/c/iomodtty.c iomodtty.obj      # __ChkTTYIOMode (pure: __io_mode+isatty)
cw string/c/strlwr.c     strlwr.obj        # defines _strlwr; wlink aliases strlwr->_strlwr

# --- Watcom clib: the UNCHANGED read-path formatter (fscanf -> __scnf) ---------
cw  streamio/c/fscanf.c  fscanf.obj
cwf streamio/c/scnf.c    scnf.obj          # -fpc: scan_float() is compiled in

# --- Watcom clib: the near-heap manager (FILE buffer allocator, UNCHANGED) -----
for f in nmalloc nfree calloc nrealloc grownear amblksiz heapen nheapmin \
         mem bfree _expand nmemneed nmsize nexpand nheapunl; do
  cw heap/c/$f.c "$f.obj"
done

# --- Watcom clib: mem helpers ---
cw memory/c/memcpy.c  memcpy.obj
cw memory/c/memset.c  memset.obj
cw memory/c/memmove.c memmove.obj
cw memory/c/memcmp.c  memcmp.obj

# --- Layer-1 long helpers (32-bit mul/div for %ld and lseek arithmetic) --------
aw "$B/clib/cgsupp/a/i4m.asm" i4m.obj
aw "$B/clib/cgsupp/a/i4d.asm" i4d.obj

# --- double SOFT-FLOAT runtime (-fpc __FDxemu path), copied from build-whetstone
#     -- resolves the FIDRQQ/FIERQQ/FIWRQQ + __FDx fixups that scan_float pulls,
#     with __real87==0 forcing the pure-software branch (no 8087, no INT traps). -
aw "$B/clib/cgsupp/a/fdmth086.asm" fdmth086.obj   # __FDA/__FDS/__FDM/__FDD (+emu)
aw "$B/clib/cgsupp/a/fdi4086.asm"  fdi4086.obj     # __FDI4 double -> long
aw "$B/clib/fpu/a/chipd16.asm"     chipd16.obj      # __fdiv_m64r software divider
aw "$B/fpuemu/i86/asm/emustub.asm" emustub.obj       # FIxRQQ stubs + no-op emu init
"$WASM" -ms -0 $AINC "$SRC/port/fpsupport.asm"  -fo=fpsupport.obj
"$WASM" -ms -0 $AINC "$SRC/port/fpsoftstub.asm" -fo=fpsoftstub.obj  # __real87=0

# --- DOS-free mathlib/strtod support modules pulled by scan_float --------------
cw startup/c/seterrno.c  seterrno.obj      # __set_EDOM_/__set_ERANGE_
cw startup/c/rtcntrl.c   rtcntrl.obj       # __get_rt_control_ptr_
cw streamio/c/iobaddr.c  iobaddr.obj       # __get_std_stream_ (stderr for matherr)
cw char/c/istable.c      istable.obj       # __IsTable ctype table (strtod indexes)

# --- prebuilt Watcom libraries, LIB-searched (only referenced modules pulled) --
MATH286_SRC="$B/mathlib/library/msdos.286/ms"
"$WLIB" -q -b -n math286.lib "$MATH286_SRC"/*.obj >/dev/null 2>&1
CLIB086="$B/clib"
LIBS="library $CLIB086/cgsupp/library/msdos.086/ms/clibs.lib \
      library $CLIB086/fpu/library/msdos.086/ms/clibs.lib \
      library $CLIB086/math/library/msdos.086/ms/clibs.lib \
      library $CLIB086/convert/library/msdos.086/ms/clibs.lib \
      library $CLIB086/char/library/msdos.086/ms/clibs.lib \
      library $CLIB086/mbyte/library/msdos.086/ms/clibs.lib \
      library $CLIB086/string/library/msdos.086/ms/clibs.lib \
      library math286.lib"

# --- our thin CP/M-86 seam (Layer 2) + oracle test (with the fscanf block) -----
"$WASM" -ms -0 "$SRC/port/crt0sm.asm" -fo=crt0.obj
"$WCC" $USER $INC "$SRC/port/cominit.c"  -fo=cominit.obj
"$WCC" $USER $INC -DDISKIO_IOMODE "$SRC/port/diskio.c"   -fo=diskio.obj
"$WCC" $USER $INC "$SRC/port/lowlevel.c" -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/errnoptr.c" -fo=errnoptr.obj
"$WCC" $USER $INC -DDISKIO_LSEEK -DHAVE_IOEXIT -DHAVE_FLUSHALL "$SRC/port/stubs.c" -fo=stubs.obj
"$WCC" $CLIB $INC "$TESTSRC" -fo=iotest.obj      # Watcom's UNCHANGED stream test

# --- link a CP/M-86 .CMD ---
"$WLINK" format cpm86 op dosseg op quiet name iotest.cmd \
  file crt0.obj file iotest.obj file diskio.obj file lowlevel.obj \
  file cominit.obj \
  file printf.obj file fprintf.obj file fprtf.obj file fputc.obj file fputs.obj \
  file puts.obj file fwrite.obj file flush.obj file fflush.obj \
  file fopen.obj file fclose.obj file allocfp.obj file fgetc.obj file fgets.obj \
  file fread.obj file fseek.obj file ftell.obj file rewind.obj file feof.obj file ferror.obj \
  file ungetc.obj file fscanf.obj file scnf.obj \
  file ioalloc.obj file chktty.obj file iob.obj file initfile.obj \
  file comtflag.obj file freefp.obj file textmode.obj \
  file iomode.obj file stiomode.obj file fileno.obj file fdopen.obj file flushall.obj \
  file setbuf.obj file setvbuf.obj file clearerr.obj file fsetpos.obj file fgetpos.obj \
  file getc.obj file putc.obj file putchar.obj file fputchar.obj file getchar.obj \
  file fgetchar.obj file gets.obj file perror.obj \
  file scanf.obj file vfprintf.obj file vprintf.obj file ioexit.obj file iomodtty.obj \
  file strlwr.obj alias strlwr_=_strlwr_ \
  file prtf.obj file noefgfmt.obj file strupr.obj file strlen.obj file strcmp.obj file strcpy.obj \
  file itoa.obj file ltoa.obj file lltoa.obj file alphabet.obj file wctomb.obj \
  file nmalloc.obj file nfree.obj file calloc.obj file nrealloc.obj \
  file grownear.obj file amblksiz.obj file heapen.obj file nheapmin.obj \
  file mem.obj file _expand.obj file nmemneed.obj file nmsize.obj \
  file nexpand.obj file nheapunl.obj file bfree.obj \
  file memcpy.obj file memset.obj file memmove.obj file memcmp.obj \
  file fdmth086.obj file fdi4086.obj file chipd16.obj file fpsupport.obj \
  file emustub.obj file fpsoftstub.obj \
  file seterrno.obj file rtcntrl.obj file iobaddr.obj file istable.obj \
  file stubs.obj file errnoptr.obj file i4m.obj file i4d.obj \
  $LIBS

# --- purity gate: zero INT 21h (DOS) ---
python3 - iotest.cmd <<'PY'
import sys; d=open(sys.argv[1],'rb').read()
dos=d.count(b'\xcd\x21'); bdos=d.count(bytes([0xcd,0xe0]))
print(f"purity: INT21h(DOS)={dos}  INTE0h(BDOS)={bdos}")
assert dos==0, "FAIL: DOS INT 21h present in image!"
assert bdos>0, "FAIL: no BDOS call in image!"
PY

# --- run under emu2 + self-checking oracle gate ---
if [ "${STREAMIO_NORUN:-0}" = "1" ]; then
  echo "STREAMIO_NORUN=1: built $OUTDIR/iotest.cmd, skipping emu2 run"
  exit 0
fi
rm -f TMP?????.\$\$\$ *.TMP
OUT="$("$EMU2" iotest.cmd | tr -d '\r')"; echo "--- output ---"; echo "$OUT"
if echo "$OUT" | grep -q "Tests completed"; then
  echo "PASS: Watcom UNCHANGED streamio/iotest.c on CP/M-86 (disk + console)"
else
  echo "FAIL: streamio iotest did not report 'Tests completed'"; exit 1
fi
