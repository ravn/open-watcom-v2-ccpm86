/* Console printf for CP/M-86 built on Watcom's OWN __prtf formatter core.
   No stdio, no FILE*, no DOS: __prtf(dest,fmt,args,callback) is a pure
   formatter (verified 0x INT21h) whose output goes through a callback.
   Our callback writes each char to the CP/M-86 console via BDOS C_WRITE
   (INT E0h, CL=2). This is the whole retarget seam in one function. */
#include "variety.h"
#include "widechar.h"
#include <stdio.h>
#include <stdarg.h>
#include "printf.h"

extern void _bdos_conout( int c );
#pragma aux _bdos_conout =      \
    "mov cl,2"                  \
    "int 0E0h"                  \
    parm [dx]                   \
    modify [ax bx cx es];

static prtf_callback_t con_putc;   /* pick up the callback calling convention */
static void PRTF_CALLBACK con_putc( PTR_PRTF_SPECS specs, PRTF_CHAR_TYPE op_char )
{
    if( op_char == '\n' )
        _bdos_conout( '\r' );      /* CP/M console wants CR before LF */
    _bdos_conout( op_char );
    specs->_output_count++;        /* __prtf returns this count */
}

int cprintf( const char *format, ... )
{
    va_list args;
    char    dummy[2];              /* __prtf sets specs._dest = dest; unused by us */
    int     len;

    va_start( args, format );
    len = __prtf( dummy, format, args, con_putc );
    va_end( args );
    return( len );
}
