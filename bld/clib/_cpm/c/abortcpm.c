/* Lightweight abort() for CP/M-86: warm-boot terminate via BDOS function 0
 * (int E0h, CL=0), avoiding Watcom's signal/raise/exit-with-message machinery
 * that clib/process/c/abort.c would pull in. fail.h calls abort() only on the
 * failure-overflow path (main_terminated || errors>5); a PASS run never reaches
 * it, but the symbol must still resolve at link time.
 */
#include <stdlib.h>

static void _cpm_reset( void );
#pragma aux _cpm_reset = \
    "xor dx,dx"          \
    "mov cl,0"           \
    "int 0E0h"           \
    __modify [__ax __cx __dx];

void abort( void )
{
    _cpm_reset();
    for( ;; )
        ;               /* not reached: BDOS 0 does not return */
}
