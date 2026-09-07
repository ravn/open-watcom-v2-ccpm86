/* bssprobe.c -- does the CP/M-86 loader hand us a ZEROED BSS on real hardware?
 *
 * The .CMD data group descriptor carries both G-Length (how many paragraphs of
 * initialised data are actually IN the file) and G-Min (how many the loader
 * must allocate).  When G-Min > G-Length the tail is BSS: memory the loader
 * allocates but the file image does not cover.  Nothing in crt0sm.asm clears
 * it, so its contents are whatever the loader leaves behind.
 *
 * This probe declares a large uninitialised static array -- guaranteeing that
 * tail exists -- and counts how many of its bytes are non-zero.  Under a
 * zero-filling host the answer is 0; on hardware that recycles dirty memory it
 * is not.  Reported over the same 0x2FE word-stream the disk oracle uses.
 *
 *   tag 0xB550, then nonzero-count, then total/16, then sentinel 0xE0F0.
 */
#include <stdio.h>
#include "mamedone.h"

#define PROBE_BYTES 16384

/* Uninitialised => lands in BSS, i.e. in the G-Min-over-G-Length tail. */
static unsigned char probe[PROBE_BYTES];

int main( void )
{
    unsigned  i;
    unsigned  nonzero = 0;

    /* Read BEFORE writing anything: the point is what the loader left here. */
    for( i = 0; i < PROBE_BYTES; ++i ) {
        if( probe[i] != 0 ) {
            ++nonzero;
        }
    }

    printf( "BSSPROBE: %u of %u bytes non-zero\r\n", nonzero,
            (unsigned)PROBE_BYTES );
    printf( "BSSPROBE: %s\r\n", nonzero == 0 ? "ZEROED" : "DIRTY" );
    fflush( stdout );

    mame_out( 0xB550 );
    mame_out( nonzero );
    mame_out( (unsigned)( PROBE_BYTES / 16 ) );
    mame_out( 0xE0F0 );
    return( 0 );
}
