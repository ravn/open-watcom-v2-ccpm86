/* CP/M-86 console glue for the Aztec-libc bring-up (ravn/open-watcom-v2#13).
 *
 * Provides putchar() directly on top of the BDOS console-output call so that
 * Aztec's stdlib puts.c -- recompiled by Watcom wcc -- can run WITHOUT first
 * bringing up Aztec's full FILE/channel/buffered-stdio subsystem (that whole
 * subsystem is the later milestone; it was also the tarpit that broke the DR C
 * runtime, see #12).  This file is OUR glue, not Aztec source, so it is
 * committable; the Aztec puts.c stays uncommitted under ../src/.
 *
 * BDOS gateway: CP/M-86 enters BDOS via INT 0E0h, function in CL, argument in
 * DX, result in AL (same convention as cpm86-clib/cpmsys.h in the #10 work).
 */
extern unsigned char _bdos( unsigned char func, unsigned param );
#pragma aux _bdos =             \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [al]                  \
    modify [ax bx cx dx es];

#define BDOS_CONOUT 2           /* DL = character to write */

int putchar( int c )
{
    if( c == '\n' )                     /* CP/M console needs CR before LF */
        _bdos( BDOS_CONOUT, '\r' );
    _bdos( BDOS_CONOUT, (unsigned char)c );
    return c;
}
