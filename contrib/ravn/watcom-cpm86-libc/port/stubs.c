/* Closure symbols the console/stdio demo references but never actually executes.
   A real full libc would supply the true versions; here each is unreachable on
   the console-only write path, so a trivial stub keeps the link DOS-free.

   _ismbblead          : single-byte CP/M console has no multibyte lead bytes.
   __fatal_runtime_error: only from the noefgfmt float stub (no %e/%f/%g here) and
                          from __InitFiles' out-of-memory path (never hit).

   iob.c's AYIN places a static rt_init record in the YI (fini) table holding the
   ADDRESS of __full_io_exit -- so the linker must resolve it -- but our crt0
   never walks the fini table, so it is never invoked (we fflush explicitly).
   NOTE: __InitFiles is NOT stubbed -- it is genuinely DOS-free (it only calls our
   arena lib_nmalloc to attach a __stream_link to each std FILE), so we link the
   real initfile.obj and call it from startup (see stdiotest.c main()).

   flush.c's read/seek branch references __lseek and fsync; on a write-only TTY
   stream (isatty=>_IOLBF) that branch is never taken, but the symbols must
   resolve. Return the DOS error convention (-1) should they ever be reached. */

#include <stddef.h>

int _ismbblead( unsigned int c ) { (void)c; return 0; }
/* NEAR: the clib declares this `_WCNEAR __fatal_runtime_error` (streamio/h),
   so noefgfmt / __InitFiles reach it via a NEAR call. In a far-code model
   (medium/large) a plain definition would compile FAR (retf) and the near
   caller's stack would mismatch on return -> control returns to a bogus CS.
   `__near` forces a near (ret) body; it is a no-op in near-code models
   (small/compact), so their stubs.obj stays byte-identical. */
void __near __fatal_runtime_error( char __far *msg, int rc ) { (void)msg; (void)rc; for( ;; ) ; }

/* __full_io_exit registers in the YI (fini) table via iob.c's AYIN; our crt0
   never walks that table (we fflush explicitly), so it is never called -- but
   the static YI record holds its address, so the symbol must resolve. Builds
   that link the REAL finalizer (streamio: ioexit.obj, which also gives the real
   fcloseall) define HAVE_IOEXIT to drop this stub and avoid a duplicate. */
/* NEAR: clib header declares `_WCNEAR __full_io_exit` and iob.c's AYIN record
   holds its address as a near (offset-only) pointer, so the definition must be
   near to match the fixup. `__near` is a no-op in near-code models. */
#ifndef HAVE_IOEXIT
void __near __full_io_exit( void ) {}
#endif

/* The disk build (build-diskio.sh) supplies the REAL __lseek in diskio.c, so
   exclude this stub there via -DDISKIO_LSEEK to avoid a duplicate symbol. */
#ifndef DISKIO_LSEEK
long __lseek( int handle, long offset, int origin )
{ (void)handle; (void)offset; (void)origin; return( -1L ); }
#endif

int fsync( int handle ) { (void)handle; return( -1 ); }

/* _nheapgrow -- CP/M-86 seam replacing the DOS near-heap "grow to 64K" step.
 *
 * Why this lives here (not the stock heap/c/heapgrow.c): in COMPACT/large-data
 * models (-mc, __BIG_DATA__) Watcom's fmalloc.c compiles a DOS-only init entry
 *     #if defined(__DOS__) && defined(__BIG_DATA__)
 *       static void ___nheapgrow(void){ _nheapgrow(); }  AXIN(___nheapgrow,...)
 * which registers ___nheapgrow in the XI/AXI startup-init table. Our minimal
 * crt0 (port/crt0cm.asm) does NOT walk that table -- it calls __CommonInit by
 * hand -- so ___nheapgrow is DEAD CODE at runtime. But the OMF init record
 * still holds its ADDRESS, so the linker must resolve _nheapgrow, and the stock
 * heapgrow.c version drags in genuine DOS internals (_osmode_PROTMODE == __osmode,
 * _RWD_psp == __psp, TinyMaxSet == INT 21h realloc) that do not exist on CP/M-86.
 *
 * On CP/M-86 there is no DOS-style pre-grow: the near heap IS the DGROUP arena,
 * grown on demand by port/lowlevel.c's sbrk/__brk as _nmalloc needs it. So the
 * correct CP/M behaviour for "grow near heap to 64K up front" is simply nothing.
 * (Defined for all models; in small/medium fmalloc.c does not reference it, so
 * this object member is only pulled into the compact clib.) */
void _nheapgrow( void ) {}


/* errno storage: fputc/flush reference the C `errno` datum (OMF symbol _errno;
   single-thread small model uses a plain global). The real one lives in RT data
   we don't link, so define it here. (`port/errnoptr.c` owns `__get_errno_ptr`,
   which returns &errno; builds that need the pointer hook link that object.) */
int errno;

/* Disk-build-only closure stubs (build-diskio.sh compiles with -DDISKIO_LSEEK).
   fopen() lowercases its mode char via tolower() (stock one indexes the __ctype
   table we don't link -- ASCII fold is all fopen needs). fgetc's __fill_buffer,
   on the TTY branch only (fp->_flag & _ISTTY), calls __flushall(_ISTTY) + getche()
   -- disk streams never take that branch, so both are unreachable but must
   resolve. Guarded so the other builds' stubs.obj stays byte-identical. */
#ifdef DISKIO_LSEEK
/* __flushall/getche NEAR: fgetc's __fill_buffer (declared with `_WCNEAR
   __flushall`) NEAR-calls __flushall on the TTY branch; a far-code definition
   would return via retf and corrupt CS. `__near` matches the header's linkage
   (no-op in near-code models). getche stays default (far, public API). */
int tolower( int c ) { return( ( c >= 'A' && c <= 'Z' ) ? c + ( 'a' - 'A' ) : c ); }
int __near __flushall( int mask ) { (void)mask; return( 0 ); }
int getche( void ) { return( -1 ); }
#endif

/* fflush(NULL) would flush every open stream via flushall -> the __InitFiles
   stream-link list, which we don't build. We only ever fflush(stdout), so the
   fp!=NULL path (a plain __flush) is taken; this stub covers the unused symbol.
   Builds that genuinely exercise flushall() (streamio) link the real
   flushall.obj and define HAVE_FLUSHALL to drop this stub. */
#ifndef HAVE_FLUSHALL
int flushall( void ) { return( 0 ); }
#endif
