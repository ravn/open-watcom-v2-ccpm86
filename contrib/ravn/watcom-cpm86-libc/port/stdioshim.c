/* stdioshim.c -- CP/M-86 write seam for Open Watcom's GENUINE stdio FILE* layer.

   Watcom's stdio buffers into a FILE and flushes via __qwrite(handle,buf,len)
   (see bld/clib/streamio/c/flush.c, fwrite.c; declared in qwrite.h). For 16-bit
   DOS that seam bottoms out in TinyWrite -> INT 21h AH=40h (io086.asm). We
   OVERRIDE __qwrite with a BDOS console writer, so the WHOLE FILE* path --
   printf / fprintf / fputs / puts / fwrite -> __fprtf -> fputc (buffer) ->
   __flush -> __qwrite -- runs on CP/M-86 with zero DOS, reusing cprintf.c's
   BDOS C_WRITE pattern.

   Scope (rc7xx-work#7): console handles only. STDOUT_FILENO(1)/STDERR_FILENO(2)
   go to the CP/M console via BDOS C_WRITE (INT E0h, CL=2), CR before LF (the
   console wants CR/LF, exactly as the direct-__prtf cprintf callback does).
   Any other handle returns -1 (no disk FILE* yet -- that needs the CP/M record
   model: 128-byte sectors, Ctrl-Z text EOF, tracked in #7). stdcbench only
   writes to the console, so this unblocks the wc-stdcbench relink.

   Buffering/flush note: our minimal crt0 does NOT walk Watcom's xinit/xfini
   runtime-init tables, so the stdio auto-flush-at-exit (__full_io_exit,
   registered via AYIN in iob.c) never runs. Callers must fflush(stdout) before
   returning -- or the buffered tail is lost. The test does exactly that. */

#include "variety.h"
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include "qwrite.h"

extern void _bdos_conout( int c );
#pragma aux _bdos_conout =      \
    "mov cl,2"                  \
    "int 0E0h"                  \
    parm [dx]                   \
    modify [ax bx cx es];

/* Override Watcom's __qwrite: the single low-level write the FILE* flush path
   calls. We honour only the two console handles; everything above (the FILE
   buffering, orientation, error flags) is Watcom's unmodified stdio.

   Bytes are written VERBATIM: unlike cprintf.c's direct-__prtf callback, this
   seam sits BELOW Watcom's text-mode fputc, which has already translated each
   '\n' into "\r\n" in the FILE buffer (fputc.c, non-_BINARY path). Adding
   another CR here would double it ("\r\r\n"), so we don't. */
int _WCNEAR __qwrite( int handle, const void *buffer, unsigned len )
{
    const unsigned char *p = (const unsigned char *)buffer;
    unsigned             i;

    if( handle != STDOUT_FILENO && handle != STDERR_FILENO )
        return( -1 );                  /* no disk FILE* seam yet (see #7) */

    for( i = 0; i < len; i++ )
        _bdos_conout( p[i] );          /* CR/LF already in buffer from fputc */
    return( (int)len );
}

/* isatty seam: Watcom's __ioalloc -> __chktty calls isatty() to pick a buffering
   mode; the DOS isatty bottoms out in INT 21h AH=44h (IOCTL). We replace it -- on
   CP/M-86 the three standard handles (stdin/stdout/stderr = 0/1/2) are the console
   (a tty), everything else is not. Returning tty here also makes stdout line-
   buffered (_IOLBF), so each '\n' auto-flushes through our __qwrite. */
int isatty( int handle )
{
    return( handle >= 0 && handle <= 2 );
}
