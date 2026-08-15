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
void __fatal_runtime_error( char __far *msg, int rc ) { (void)msg; (void)rc; for( ;; ) ; }

/* __full_io_exit registers in the YI (fini) table via iob.c's AYIN; our crt0
   never walks that table (we fflush explicitly), so it is never called -- but
   the static YI record holds its address, so the symbol must resolve. */
void __full_io_exit( void ) {}

/* The disk build (build-diskio.sh) supplies the REAL __lseek in diskio.c, so
   exclude this stub there via -DDISKIO_LSEEK to avoid a duplicate symbol. */
#ifndef DISKIO_LSEEK
long __lseek( int handle, long offset, int origin )
{ (void)handle; (void)offset; (void)origin; return( -1L ); }
#endif

int fsync( int handle ) { (void)handle; return( -1 ); }

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
int tolower( int c ) { return( ( c >= 'A' && c <= 'Z' ) ? c + ( 'a' - 'A' ) : c ); }
int __flushall( int mask ) { (void)mask; return( 0 ); }
int getche( void ) { return( -1 ); }
#endif

/* fflush(NULL) would flush every open stream via flushall -> the __InitFiles
   stream-link list, which we don't build. We only ever fflush(stdout), so the
   fp!=NULL path (a plain __flush) is taken; this stub covers the unused symbol. */
int flushall( void ) { return( 0 ); }
