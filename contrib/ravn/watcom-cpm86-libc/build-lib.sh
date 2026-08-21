#!/bin/bash
# ===========================================================================
# build-lib.sh -- assemble a REAL CP/M-86 C-library archive, clibcpm.lib.
#
# Rationale (rc7xx-work#6): the per-milestone scripts (build-diskio.sh, ...)
# compile only the exact object subset ONE demo/test happens to reference and
# link them loose.  That does not scale: every new program (e.g. Info-ZIP
# UnZip) then triggers a manual undefined-symbol hunt.  The proper solution --
# what Open Watcom itself ships for DOS as clibs.lib -- is a single ARCHIVE of
# the whole OS-agnostic clib layer plus our thin CP/M-86 (BDOS) seam.  wlib
# builds it once; wlink then pulls ONLY the modules a given program needs.
#
# Layers (see README):
#   Layer 1  Watcom clib source, REUSED UNCHANGED: string, ctype table, the
#            __prtf formatter + full stdio FILE* read/write path, near-heap,
#            mem*, convert (itoa/ltoa), gmtime.
#   Layer 2  our CP/M-86 seam WE own: diskio.c (FCB BDOS file I/O + console),
#            lowlevel.c (arena heap __brk/sbrk), cominit.c, errnoptr.c,
#            abortcpm.c (BDOS warm-boot abort), stubs.c (closure symbols),
#            portmisc.c (getenv/setmode/signal/localtime/tzset seams).
#   Layer 3  BDOS (INT E0h) -- reached only through Layer 2.
#
# The startup object crt0sm.asm / crt0mm.asm (public _cstart_) is BOTH archived
# as a member of clibs.lib/clibm.lib AND emitted separately as cstartcpm.obj/
# cstartmm.obj. Its entry sits in the front-sorted 'BEGTEXT' segment, so under
# `option dosseg` it lands at code-group offset 0 (the CP/M-86 fixed CS:0000
# entry) even when pulled from the library -- mirroring Open Watcom's 16-bit
# `system dos` target, whose linker block force-includes NO startup and instead
# auto-selects it from the model clib via the object's default-library record.
# So `owcc -bcpm86 -mcmodel={s,m} prog.c` links with NO explicit file/library:
#     wlink format cpm86 op dosseg op start=_cstart_ ...   (specs.sp system cpm86)
# auto-fetches clibs.lib (small) or clibm.lib (medium) which brings the matching
# startup first. The standalone cstartcpm.obj/cstartmm.obj remain for the
# explicit-`file` demo scripts (build.sh, build-medium.sh).
#
# NOTE: the scanf family (scnf/sscanf/fscanf/scanf) and the real %e/%f/%g printf
# formatter ARE archived now, but as ON-DEMAND members: nothing in the string/
# FILE* core references them, so a program pulls them only if it actually calls
# *scanf or __setEFGfmt().  Integer-only programs keep the small footprint that
# matters under the 64 KB single-code-segment small-model ceiling.  Float I/O
# (%f read or write) additionally needs libm + a one-time __setEFGfmt() call.
# ===========================================================================
set -e
cd "$(dirname "$0")"
# NOTE: do NOT name the override vars WCC/WASM/WLIB/WLINK -- Open Watcom's
# own tools read an env var NAMED AFTER THEMSELVES for implicit default
# switches (confirmed 2026-08-18: an exported WCC=<path to wcc.exe> makes
# wcc.exe itself parse that path string as bogus extra command-line
# content -> "E1139: Command line contains more than one file to compile").
# unset here defensively in case the CALLER's shell exported one of these.
unset WCC WASM WLIB WLINK
OW="${OW:-$(cd "$(dirname "$0")/../../.." && pwd)}"; B="$OW/bld"
WCC="${OWCC_BIN:-$B/cc/i86/osxa64/binbuild/wcc.exe}"
WASM="${OWASM_BIN:-$B/wasm/osxa64/wasm.exe}"
WLIB="${OWLIB_BIN:-$B/nwlib/osxa64/wlib.exe}"

OUTDIR="${OUTDIR:-build-lib}"; mkdir -p "$OUTDIR"
SRC="$(pwd)"
cd "$OUTDIR"

# MODEL selects the CP/M-86 memory model to build the library for:
#   s (default) = small  : near code + near data, ONE <=64 KB CODE segment.
#   m           = medium : FAR code (>64 KB, per-function *_TEXT segments via
#                          -zm, coalesced by wlink into one Code Group
#                          Descriptor -- Stage B), near data (DGROUP unchanged).
#   c           = compact : near code (ONE <=64 KB CODE segment, like small) +
#                          FAR data. -mc defines __BIG_DATA__, which flips
#                          Watcom's fmalloc.c so plain malloc()/calloc()/etc.
#                          redirect to the FAR heap (_fmalloc, port/farheap.c's
#                          __AllocSeg segments carved from the .CMD Extra group).
#                          Message strings / far CONST also leave DGROUP. This
#                          is the model UnZip's DEFLATE window needs -- link
#                          compact programs with `option farheap=<size>`.
# Everything shared with DOS 16-bit (the whole OS-agnostic Watcom clib source)
# is simply recompiled with -m$MODEL; only the model flag differs, no source
# changes. The CP/M-86 (BDOS) seam recompiles the same way. See README.
MODEL="${MODEL:-s}"
case "$MODEL" in
  s) ZMFLAG=""    ; CRT0SRC="crt0sm.asm" ; LIBNAME="clibs.lib"  ; CRT0NAME="cstartcpm.obj" ;;
  m) ZMFLAG="-zm" ; CRT0SRC="crt0mm.asm" ; LIBNAME="clibm.lib"  ; CRT0NAME="cstartmm.obj"  ;;
  # WORKS at runtime (was blocked before the wlink type-3 EXTRA fix `09c2eb3099`):
  # compact model makes clib globals (e.g. int __heap_enabled=1) FAR. wlink's
  # `format cpm86` used to emit that as a SECOND type=2 group the CP/M-86 loader
  # (groups keyed by TYPE 1-8) could not place -> globals read 0 -> malloc()==NULL.
  # Now program far data is coalesced into ONE type-3 EXTRA group the loader DOES
  # place, so __heap_enabled reads 1 and the transparent far malloc()/realloc()
  # path runs. Verified: run-all-models.sh compact heap/stdio/float all PASS under
  # Unicorn; link compact programs with `option farheap=<size>`. (The remaining
  # open item is far-HEAP-vs-far-DATA slab overlap for programs with LOTS of far
  # data -- see cmd_check.py [F1] + test/compact_farheap_test.c; heaptest's small
  # far data does not hit it.)
  c) ZMFLAG=""    ; CRT0SRC="crt0cm.asm" ; LIBNAME="clibc.lib"  ; CRT0NAME="cstartcm.obj"  ;;
  # LARGE = medium's FAR code (-zm per-function *_TEXT, `_big_code_` marker,
  # far calls/retf) + compact's FAR data (-ml also defines __BIG_DATA__, so the
  # SAME fmalloc.c far-heap path as compact is active). Only new artifact is
  # crt0lm.asm (== crt0mm's far-code startup; far-data needs nothing there).
  # Motivating deliverable: Info-ZIP `zip`, whose pristine source only type-
  # checks under a far-DATA model (flush_block char far* vs char*, E1129).
  l) ZMFLAG="-zm" ; CRT0SRC="crt0lm.asm" ; LIBNAME="clibl.lib"  ; CRT0NAME="cstartlm.obj"  ;;
  *) echo "MODEL must be s, m, c or l (got '$MODEL')" >&2; exit 1 ;;
esac
echo "==> building CP/M-86 clib for MODEL=$MODEL  (-m$MODEL $ZMFLAG -> $LIBNAME + $CRT0NAME)"

INC="-i=$B/lib_misc/h -i=$B/clib/streamio/h -i=$B/clib/string/h -i=$B/clib/time/h -i=$B/clib/h -i=$B/clib/heap/h -i=$B/clib/intel/h -i=$B/comp_cfg/h -i=$B/watcom/h -i=$B/hdr/dos/h"
CLIB="-bt=dos -0 -m$MODEL $ZMFLAG -zastd=c99 -zl -x"    # compile stock Watcom clib source
USER="-bt=dos -0 -m$MODEL $ZMFLAG -zl -zastd=c99"       # compile our port seam

cw() { "$WCC" $CLIB $INC "$B/clib/$1" -fo="$2"; }   # compile a Watcom clib source

echo "==> Layer 1: string"
cw string/c/strlen.c   strlen.obj
cw string/c/strcmp.c   strcmp.obj
cw string/c/strcpy.c   strcpy.obj
cw string/c/strncpy.c  strncpy.obj
cw string/c/strcat.c   strcat.obj
cw string/c/strncmp.c  strncmp.obj
cw string/c/strnicmp.c strnicmp.obj
cw string/c/strchr.c   strchr.obj
cw string/c/strrchr.c  strrchr.obj
cw string/c/strupr.c   strupr.obj
cw string/c/strerror.c strerror.obj
cw string/c/sprintf.c  sprintf.obj
cw string/c/vsprintf.c vsprintf.obj

echo "==> Layer 1: ctype table"
cw char/c/istable.c    istable.obj      # __IsTable / __Bits classification table

echo "==> Layer 1: convert + mbyte helpers used by __prtf"
cw convert/c/itoa.c      itoa.obj
cw convert/c/ltoa.c      ltoa.obj
cw convert/c/lltoa.c     lltoa.obj
cw convert/c/alphabet.c  alphabet.obj
cw mbyte/c/wctomb.c      wctomb.obj

echo "==> Layer 1: string-to-number + strtok + toupper + setvbuf (editor deps)"
cw convert/c/atoi.c      atoi.obj
cw convert/c/strtol.c    strtol.obj
cw string/c/strtok.c     strtok.obj
cw string/c/setbits.c    setbits.obj
cw string/c/bits.c       bits.obj
cw char/c/toupper.c      toupper.obj
cw streamio/c/setvbuf.c  setvbuf.obj

echo "==> Layer 1: __prtf formatter core"
cw streamio/c/prtf.c     prtf.obj
cw streamio/c/noefgfmt.c noefgfmt.obj

echo "==> Layer 1: stdio FILE* write path"
cw streamio/c/printf.c   printf.obj
cw streamio/c/fprintf.c  fprintf.obj
cw streamio/c/fprtf.c    fprtf.obj
cw streamio/c/fputc.c    fputc.obj
cw streamio/c/putchar.c  putchar.obj    # putchar -> fputc(c,stdout); fputc already in archive
cw streamio/c/fputs.c    fputs.obj
cw streamio/c/puts.c     puts.obj
cw streamio/c/fwrite.c   fwrite.obj
cw streamio/c/flush.c    flush.obj
cw streamio/c/fflush.c   fflush.obj
cw streamio/c/perror.c   perror.obj

echo "==> Layer 1: scanf family (sscanf/fscanf/scanf + %-parser core)"
# The scanf conversion core scnf.c + entries. Float reads (%e/%f/%g) go through
# __EFG_scanf, which __setEFGfmt() points at __cnvs2d (in libm) -- so a program
# reading floats calls __setEFGfmt() once and links libm, exactly like %f print.
cw streamio/c/scnf.c     scnf.obj       # __scnf: the %-conversion scanner core
cw string/c/sscanf.c     sscanf.obj     # sscanf (string source)
cw streamio/c/fscanf.c   fscanf.obj     # fscanf (FILE* source)
cw streamio/c/scanf.c    scanf.obj      # scanf (stdin)
"$WCC" $CLIB $INC -i="$B/clib/char/h" "$B/clib/char/c/isdigit.c" -fo=isdigit.obj  # scanner classification (functions)
"$WCC" $CLIB $INC -i="$B/clib/char/h" "$B/clib/char/c/isspace.c" -fo=isspace.obj
"$WCC" $CLIB $INC -i="$B/clib/mbyte/h" "$B/clib/mbyte/c/mbtowc.c" -fo=mbtowc.obj   # scanner multibyte step
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i8m086.asm" -fo=i8m086.obj  # __U8M 64-bit mul (cvt/scan)

echo "==> Layer 1: stdio FILE* read/open path"
cw streamio/c/fopen.c    fopen.obj
cw streamio/c/fclose.c   fclose.obj
cw streamio/c/allocfp.c  allocfp.obj
cw streamio/c/fgetc.c    fgetc.obj
cw streamio/c/getchar.c  getchar.obj    # getchar -> fgetc(stdin); fgetc already in archive
cw streamio/c/fgets.c    fgets.obj
cw streamio/c/gets.c     gets.obj       # gets -> fgets over stdin; read path already present
cw streamio/c/fread.c    fread.obj
cw streamio/c/fseek.c    fseek.obj
cw streamio/c/ftell.c    ftell.obj
cw streamio/c/rewind.c   rewind.obj
cw streamio/c/feof.c     feof.obj
cw streamio/c/ferror.c   ferror.obj
cw streamio/c/ungetc.c   ungetc.obj
cw streamio/c/ioalloc.c  ioalloc.obj
cw streamio/c/chktty.c   chktty.obj
cw streamio/c/iob.c      iob.obj
cw streamio/c/initfile.c initfile.obj
cw streamio/c/comtflag.c comtflag.obj
cw streamio/c/freefp.c   freefp.obj
cw handleio/c/textmode.c textmode.obj

echo "==> Layer 1: near-heap manager"
cw heap/c/nmalloc.c   nmalloc.obj
cw heap/c/nfree.c     nfree.obj
cw heap/c/calloc.c    calloc.obj
cw heap/c/nrealloc.c  nrealloc.obj
cw heap/c/grownear.c  grownear.obj
cw heap/c/amblksiz.c  amblksiz.obj
cw heap/c/heapen.c    heapen.obj
cw heap/c/nheapmin.c  nheapmin.obj
cw heap/c/mem.c       mem.obj
cw heap/c/bfree.c     bfree.obj
cw heap/c/bexpand.c   bexpand.obj    # _bexpand: based/far realloc grow-in-place core
cw heap/c/_expand.c   _expand.obj
cw heap/c/nmemneed.c  nmemneed.obj
cw heap/c/nmsize.c    nmsize.obj
cw heap/c/nexpand.c   nexpand.obj
cw heap/c/nheapunl.c  nheapunl.obj

echo "==> Layer 1: far heap manager (Stage A compact model)"
cw heap/c/fmalloc.c   fmalloc.obj
cw heap/c/ffree.c     ffree.obj
cw heap/c/fcalloc.c   fcalloc.obj
cw heap/c/frealloc.c  frealloc.obj
cw heap/c/fmsize.c    fmsize.obj
cw heap/c/fmemneed.c  fmemneed.obj
cw heap/c/fexpand.c   fexpand.obj    # _fexpand: far realloc grow-in-place
cw heap/c/fheapset.c  fheapset.obj
cw heap/c/fheapchk.c  fheapchk.obj
cw heap/c/fheapmin.c  fheapmin.obj
cw heap/c/fheapwal.c  fheapwal.obj

echo "==> Layer 1: mem helpers"
cw memory/c/memcpy.c  memcpy.obj
cw memory/c/fmemcpy.c fmemcpy.obj    # _fmemcpy: far realloc block copy
cw memory/c/memset.c  memset.obj
cw memory/c/memmove.c memmove.obj
cw memory/c/memcmp.c  memcmp.obj

echo "==> Layer 1: qsort (stdlib)"
cw search/c/qsort.c   qsort.obj

echo "==> Layer 1: time-conversion subsystem (pure computation, no OS trap)"
cw time/c/gmtime.c    gmtime.obj      # UTC broken-down time
cw time/c/localtim.c  localtim.obj    # local time (== UTC here; _timezone=0)
cw time/c/mktime.c    mktime.obj      # inverse (struct tm -> time_t)
cw time/c/locmktim.c  locmktim.obj    # __localtime/__mktime core
cw time/c/timeutil.c  timeutil.obj    # __diyr/__dilyr day tables + helpers
cw time/c/leapyear.c  leapyear.obj    # __isleap
cw time/c/tzset.c     tzset.obj       # tzset/_timezone (reads TZ via getenv->NULL)
cw time/c/time.c      time.obj        # time() -> __getctime()+mktime() (unchanged)

echo "==> Layer 1: environment (real getenv over our empty environ)"

echo "==> Layer 1: long mul/div helpers (%ld, lseek arithmetic)"
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4m.asm" -fo=i4m.obj
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4d.asm" -fo=i4d.obj

# --- Layer 1: double SOFT-FLOAT runtime (-fpc __FDxemu path, NO 8087) ---------
# The RC759 target has no 8087. Programs compiled -fpc emit __FDx double libcalls
# that dispatch at runtime on __real87 (=0 here) to a PURE-SOFTWARE path. These
# asm helpers make that path resolve; without them any program using `double`
# (printf %f, arithmetic) fails to link (FIDRQQ/FIWRQQ/__CHP undefined). Same set
# build-float.sh links standalone; archived here so the shipped lib is float-ready
# in every model. AINC gives the fp asm its config/register-name includes.
AINC="-i=$B/watcom/h -i=$B/comp_cfg/h"
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/fdmth086.asm" -fo=fdmth086.obj  # __FDA/__FDS/__FDM/__FDD (+emu)
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/fdi4086.asm" -fo=fdi4086.obj    # __FDI4: double -> long
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i4fd086.asm" -fo=i4fd086.obj    # __I4FD: long -> double
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/fdc086.asm"  -fo=fdc086.obj     # __FDC: double compare (x<y etc.)
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/fdn086.asm"  -fo=fdn086.obj     # __FDN: double negate
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/fdfs086.asm" -fo=fdfs086.obj    # __FDFS: double -> single
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/fsfd086.asm" -fo=fsfd086.obj    # __FSFD: single -> double
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/fsn086.asm"  -fo=fsn086.obj     # __FSN: single negate
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/fstat086.asm" -fo=fstat086.obj  # FPInvalidOp/FPOverFlow status
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/fpu/a/chipd16.asm"     -fo=chipd16.obj    # __fdiv_m64r software divider
"$WASM" -m$MODEL -0 -i="$B/watcom/h" -i="$B/clib/h" "$B/clib/fpu/a/chipw16.asm" -fo=chipw16.obj  # __fpatan_wrap (atan/atan2)
"$WASM" -m$MODEL -0 -i="$B/watcom/h" -i="$B/clib/h" "$B/clib/fpu/a/chipt16.asm" -fo=chipt16.obj  # __fptan_chk (tan)
"$WASM" -m$MODEL -0 -i="$B/watcom/h" -i="$B/clib/h" "$B/clib/fpu/a/chipa16.asm" -fo=chipa16.obj  # __fpatan_chk (atan)
"$WASM" -m$MODEL -0 $AINC "$B/fpuemu/i86/asm/emustub.asm"            -fo=emustub.obj     # FIxRQQ stubs + no-op emu init
"$WASM" -m$MODEL -0 $AINC "$SRC/port/fpsupport.asm"                  -fo=fpsupport.obj   # F8Over/Under/DivZero
"$WASM" -m$MODEL -0 $AINC "$SRC/port/fpsoftstub.asm"                 -fo=fpsoftstub.obj  # __real87=0 (force soft path)

# math.h support (transcendentals in libm$MODEL.lib need these clib-side seams)
"$WCC" $USER -fpc -i=$B/mathlib/h $INC "$B/clib/startup/c/seterrno.c" -fo=seterrno.obj  # __set_EDOM_/__set_ERANGE_
"$WCC" $USER -fpc -i=$B/mathlib/h $INC "$B/clib/startup/c/rtcntrl.c"   -fo=rtcntrl.obj   # __get_rt_control_ptr_
"$WCC" $USER -fpc -i=$B/mathlib/h $INC "$B/clib/streamio/c/iobaddr.c"  -fo=iobaddr.obj   # __get_std_stream_ (matherr)
"$WCC" $USER -fpc -i=$B/mathlib/h $INC "$B/mathlib/c/_matherr.c"       -fo=_matherr.obj  # _matherr
"$WCC" $USER -fpc -i=$B/mathlib/h $INC "$SRC/port/fesoft.c"           -fo=fesoft.obj    # soft feraiseexcept (no 8087)
"$WCC" $USER -fpc -i=$B/mathlib/h $INC "$B/clib/math/c/hugeval.c"      -fo=hugeval.obj   # __HugeValue (HUGE_VAL)

# Real %e/%f/%g printf formatting -- OPT-IN. The default noefgfmt.obj stub leaves
# __EFG_printf pointing at _no_support_loaded (float conversions print nothing).
# These objects supply the genuine double->decimal formatter (_EFG_Format) + the
# dtoa/cvt subsystem. They are only pulled when a program REFERENCES __setEFGfmt
# (our minimal crt0 does NOT walk Watcom's auto-init table, so the program must
# call __setEFGfmt() ONCE before its first %f) -- so non-float programs pay zero.
# See docs/FLOAT_PRINTF.md.
FPINC="$INC -i=$B/mathlib/h -i=$B/clib/startup/h"
"$WCC" $USER -fpc $FPINC "$B/clib/streamio/c/setefg.c" -fo=setefg.obj    # __setEFGfmt: install _EFG_Format
"$WCC" $USER -fpc $FPINC "$B/mathlib/c/efgfmt.c"       -fo=efgfmt.obj    # _EFG_Format (SOURCE build -> adds %a/%A; the prebuilt libm one lacks it. clib links first so this wins)
"$WCC" $USER -fpc $FPINC "$B/mathlib/c/cvt.c"          -fo=cvt.obj       # __cvt double formatter
"$WCC" $USER -fpc $FPINC "$B/mathlib/c/ldcvt.c"        -fo=ldcvt.obj     # __ldcvt long-double core
"$WCC" $USER -fpc $FPINC "$B/mathlib/c/efcvt.c"        -fo=efcvt.obj     # e/f cvt entry
"$WCC" $USER -fpc $FPINC "$B/mathlib/c/gcvt.c"         -fo=gcvt.obj      # g cvt entry
"$WCC" $USER -fpc $FPINC "$B/clib/startup/c/cvtbuf.c"  -fo=cvtbuf.obj    # __cvtbuf conversion buffer
"$WASM" -m$MODEL -0 -i="$B/watcom/h" "$B/clib/cgsupp/a/i8ls086.asm"    -fo=i8ls086.obj  # __U8LS 64-bit shift (cvt)

echo "==> Layer 2: CP/M-86 seam (BDOS) + closure stubs + port seams"
"$WCC" $USER $INC -DCOMMONINIT_REDIRECT "$SRC/port/cominit.c"  -fo=cominit.obj  # -DCOMMONINIT_REDIRECT: shell-style < > >> command-tail redirection (disk layer present)
"$WCC" $USER $INC "$SRC/port/cprintf.c"  -fo=cprintf.obj     # direct-to-console printf (stdio-free tests)
"$WCC" $USER $INC "$SRC/port/diskio.c"   -fo=diskio.obj      # FCB BDOS file I/O
"$WCC" $USER $INC ${WC_ARENA_BYTES:+-dWC_ARENA_BYTES=$WC_ARENA_BYTES} "$SRC/port/lowlevel.c" -fo=lowlevel.obj
"$WCC" $USER $INC "$SRC/port/farheap.c"  -fo=farheap.obj     # Stage A far heap __AllocSeg/__GrowSeg
"$WCC" $USER $INC "$SRC/port/errnoptr.c" -fo=errnoptr.obj
"$WCC" $USER $INC "$SRC/port/abortcpm.c" -fo=abortcpm.obj
"$WCC" $USER $INC "$SRC/port/portmisc.c" -fo=portmisc.obj    # setmode/signal/environ
"$WCC" $USER $INC "$SRC/port/gtctmcpm.c" -fo=gtctmcpm.obj    # __getctime() BDOS T_GET seam
"$WCC" $USER $INC "$SRC/port/dirent.c"   -fo=dirent.obj      # opendir/readdir BDOS F_SFIRST/F_SNEXT
# diskio.c owns the real __lseek/tolower/etc.; drop those overlapping stubs.
"$WCC" $USER $INC -DDISKIO_LSEEK "$SRC/port/stubs.c" -fo=stubs.obj

echo "==> startup (archived as a library member AND emitted as a standalone obj)"
"$WASM" -m$MODEL -0 ${WC_STACK_BYTES:+-DWC_STACK_BYTES=$WC_STACK_BYTES} ${WC_STACK_FILL:+-DWC_STACK_FILL=$WC_STACK_FILL} "$SRC/port/$CRT0SRC" -fo=crt0.obj

echo "==> archive clibcpm.lib"
rm -f clibcpm.lib
# crt0.obj is included: its entry is in the front-sorted BEGTEXT segment, so a
# library-member pull still lands _cstart_ at code-group offset 0 (16-bit `dos`
# convention). No other archive member defines _cstart_/__STK/__argc/__argv.
"$WLIB" -q -b clibcpm.lib \
    +crt0.obj \
    +strlen.obj +strcmp.obj +strcpy.obj +strncpy.obj +strcat.obj +strncmp.obj \
    +strnicmp.obj +strchr.obj +strrchr.obj +strupr.obj +strerror.obj \
    +sprintf.obj +vsprintf.obj +istable.obj \
    +itoa.obj +ltoa.obj +lltoa.obj +alphabet.obj +wctomb.obj \
    +atoi.obj +strtol.obj +strtok.obj +setbits.obj +bits.obj +toupper.obj +setvbuf.obj \
    +prtf.obj +noefgfmt.obj \
    +printf.obj +fprintf.obj +fprtf.obj +fputc.obj +fputs.obj +puts.obj \
    +putchar.obj +getchar.obj +gets.obj \
    +fwrite.obj +flush.obj +fflush.obj +perror.obj \
    +scnf.obj +sscanf.obj +fscanf.obj +scanf.obj +isdigit.obj +isspace.obj +mbtowc.obj +i8m086.obj \
    +fopen.obj +fclose.obj +allocfp.obj +fgetc.obj +fgets.obj +fread.obj \
    +fseek.obj +ftell.obj +rewind.obj +feof.obj +ferror.obj +ungetc.obj \
    +ioalloc.obj +chktty.obj +iob.obj +initfile.obj +comtflag.obj +freefp.obj \
    +textmode.obj \
    +nmalloc.obj +nfree.obj +calloc.obj +nrealloc.obj +grownear.obj \
    +amblksiz.obj +heapen.obj +nheapmin.obj +mem.obj +bfree.obj +bexpand.obj +_expand.obj \
    +nmemneed.obj +nmsize.obj +nexpand.obj +nheapunl.obj \
    +fmalloc.obj +ffree.obj +fcalloc.obj +frealloc.obj +fmsize.obj \
    +fmemneed.obj +fexpand.obj +fheapset.obj +fheapchk.obj +fheapmin.obj +fheapwal.obj \
    +farheap.obj \
    +qsort.obj \
    +fdmth086.obj +fdi4086.obj +i4fd086.obj +fdc086.obj +fdn086.obj +fdfs086.obj +fsfd086.obj +fsn086.obj +fstat086.obj \
    +chipd16.obj +chipw16.obj +chipt16.obj +chipa16.obj +emustub.obj +fpsupport.obj +fpsoftstub.obj \
    +seterrno.obj +rtcntrl.obj +iobaddr.obj +_matherr.obj +fesoft.obj +hugeval.obj \
    +setefg.obj +efgfmt.obj +cvt.obj +ldcvt.obj +efcvt.obj +gcvt.obj +cvtbuf.obj +i8ls086.obj \
    +memcpy.obj +fmemcpy.obj +memset.obj +memmove.obj +memcmp.obj +gmtime.obj \
    +i4m.obj +i4d.obj \
    +cominit.obj +cprintf.obj +diskio.obj +lowlevel.obj +errnoptr.obj +abortcpm.obj \
    +portmisc.obj +gtctmcpm.obj +dirent.obj +stubs.obj \
    +localtim.obj +mktime.obj +locmktim.obj +timeutil.obj +leapyear.obj \
    +tzset.obj +time.obj

echo
echo "==> built $OUTDIR/clibcpm.lib + $OUTDIR/crt0.obj"
ls -l clibcpm.lib crt0.obj
echo "modules in archive:"
"$WLIB" clibcpm.lib 2>/dev/null | grep -c '\.obj' || true

# Install as the CANONICAL cpm86 standard library.
#
# owcc -bcpm86 links via `system cpm86` (bld/wl/lnk/specs.sp): it sets
#   libpath '%WATCOM%/lib286/cpm86' <- where the auto-fetched clib is searched
#   format cpm86                    <- whose implicit start symbol is _cstart_
# and the compiled objects carry a default-library record naming `clibs` (small)
# or `clibm` (medium), so wlink auto-fetches the matching model lib from that
# dir. Because crt0 is ARCHIVED as a member (entry in the front-sorted BEGTEXT
# segment -> still lands at code-group offset 0), the format's implicit _cstart_
# reference pulls the startup FIRST from that same lib -- no `libfile`, exactly
# the 16-bit `system dos` convention. So a bare
#   owcc -bcpm86 -mcmodel={s,m} prog.c -o PROG.CMD
# links the whole model-correct library + startup with NO explicit
# `library`/`file` on the link line. The standalone crt0.obj (cstartcpm.obj/
# cstartmm.obj) is kept only for the explicit-`file` demo scripts.
# lib286/cpm86 is a .gitignored install dir.
DEST="$OW/lib286/cpm86"
mkdir -p "$DEST"
cp clibcpm.lib "$DEST/$LIBNAME"
cp crt0.obj    "$DEST/$CRT0NAME"
echo "==> installed canonical: $DEST/{$LIBNAME,$CRT0NAME}"

# --- Per-model math library (libm) --------------------------------------------
# The transcendentals (sin/cos/tan/atan/exp/log/sqrt/pow ...) are model-SENSITIVE
# for two reasons even though the arithmetic is identical: (1) code model -> near
# vs far RET (a medium far-code caller must far-call), (2) data model -> a func's
# private coefficient tables live in DGROUP (near data: s/m) or embedded in the
# code segment (far data: c). So there is one libm PER MODEL, exactly like the
# clib. We archive Watcom's OWN stock 80186-safe SOFT-FLOAT mathlib objects for
# this model (msdos.286/m$MODEL, verified 0 x87 ESC + 0 286-only opcodes) into
# libm$MODEL.lib. Kept SEPARATE from clib (the classic -lc/-lm split) so a
# program that never touches <math.h> pays nothing. Link math programs with
#   ... library clib$MODEL.lib library libm$MODEL.lib     (compile -fpc, no 8087)
MATHDIR="$B/mathlib/library/msdos.286/m$MODEL"
LIBMNAME="libm$MODEL.lib"
if [ -d "$MATHDIR" ]; then
    echo "==> Layer 1b: model-$MODEL math library (transcendentals, soft-float)"
    rm -f "$LIBMNAME"
    "$WLIB" -q -b "$LIBMNAME" $(printf '+%s ' "$MATHDIR"/*.obj) >/dev/null 2>&1
    cp "$LIBMNAME" "$DEST/$LIBMNAME"
    echo "==> installed math lib: $DEST/$LIBMNAME ($("$WLIB" "$LIBMNAME" 2>/dev/null | grep -c '\.obj') modules)"
else
    echo "!! math lib dir $MATHDIR not found -- skipping libm for model $MODEL" >&2
fi
echo "DONE."
