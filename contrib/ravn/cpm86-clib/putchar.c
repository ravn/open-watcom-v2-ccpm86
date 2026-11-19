/* putchar.c -- CP/M-86 console output for the owcc -bcpm86 C library.
 *
 * The single I/O seam of this minimal CP/M-86 clib: one character to the
 * console via BDOS C_WRITE (function 2, INT 0E0h with CL=2, DL=char).  CP/M
 * consoles expect CR before LF, so '\n' is translated to CR,LF.
 *
 * Packaged into clibs.lib; wlink pulls this module only when the program
 * references putchar (CMT_DEFAULT_LIBRARY auto-fetch).
 */
extern void _bdos_conout( int c );
#pragma aux _bdos_conout =      \
    "mov cl,2"                  \
    "int 0E0h"                  \
    parm [dx]                   \
    modify [ax bx cx es];

int putchar( int c )
{
    if( c == '\n' )
        _bdos_conout( '\r' );
    _bdos_conout( c );
    return c;
}
