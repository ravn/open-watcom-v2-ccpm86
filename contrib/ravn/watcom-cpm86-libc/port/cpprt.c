/* cpprt.c -- C++ iostream runtime heap bridge for the CP/M-86 OW-clib port.
 *
 * Watcom's generic.086 iostream objects allocate streambuf storage through the
 * __clib_malloc / __clib_free bridge (declared in lib_misc/h/clibsupp.h); on the
 * DOS clib these route to Watcom's own near heap. This contrib port links
 * Watcom's UNCHANGED near-heap manager (nmalloc.obj ... over port/lowlevel.c's
 * __brk bump across wc_arena), so we simply forward to malloc/free.
 *
 * Why this file is TINY compared with the abandoned scratch mini-clib's cpprt.c
 * (rc7xx-work#12): the scratch clib had to hand-supply __get_std_stream, ltoa,
 * ultoa, strupr and a __clib_flush no-op because it was NOT Watcom's real clib.
 * Here those all come from the genuine Watcom objects we already link --
 * iobaddr.obj (__get_std_stream_ = &__iob[h]), convert/c/ltoa+ultoa, and
 * string/c/strupr (aliased strupr_ -> _strupr_ at link time) -- so the ONLY
 * iostream seam left is the heap bridge.
 *
 * Worked example: cout << "x" for a freshly-constructed filebuf -> stfdoall.cpp
 * calls __clib_malloc(DEFAULT_BUF_SIZE=512) -> malloc(512) -> _nmalloc carves it
 * from wc_arena; the streambuf get/put pointers span that block. On stream
 * teardown stfdestr.cpp calls __clib_free(buf) -> free -> _nfree returns it.
 */
#include <stddef.h>
#include <stdlib.h>

void *__clib_malloc( size_t size ) { return malloc( size ); }
void  __clib_free( void *ptr )     { free( ptr ); }
