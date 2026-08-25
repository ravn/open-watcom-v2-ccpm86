/* memtest128.c -- probe: does Concurrent CP/M-86 BDOS function 128 (M_ALLOC)
   work on the real RC759, and does it return the ACTUAL granted size in the
   MPB?  Confirms the API for the farheap.c B2 rewrite before committing to it.

   MPB (kern/mpb.def): { start, min, max, pdadr, flags } (5 words).
   Call: CL=128, DX=&MPB (DS-relative).  Out: BX=0 ok / 0xFFFF fail; CX=err;
   MPB updated: mpb_start=granted base seg, mpb_max=ACTUAL granted paras.

   Expected: on real MAME CCP/M-86 -> BX=0, mpb_start!=0, mpb_max<=want.
   Under emu2 (which only implements CP/M-86 fns 53-57, not 128) -> BX=0xFFFF,
   which itself proves the emu2 fidelity gap this pairs with. */

#include <stdint.h>
#ifdef MAME_DONE
#include "mamedone.h"
#endif

extern int cprintf( const char *, ... );

/* BDOS gateway returning BX (memory calls report success/fail in BX, err in CX).
   CL=function, DX=near &param. */
extern unsigned _bdos_mem( unsigned char fn, void *param );
#pragma aux _bdos_mem =         \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [bx]                  \
    modify [ax bx cx dx es];

struct mpb {
    uint16_t start;   /* 0 = relocatable request */
    uint16_t min;     /* least acceptable, paragraphs */
    uint16_t max;     /* wanted, paragraphs          */
    uint16_t pdadr;   /* 0 = calling process         */
    uint16_t flags;   /* 0 = plain unused memory     */
};

int main( void )
{
    static struct mpb m;   /* static -> lives in DS (DGROUP), not SS */
    unsigned bx;

    m.start = 0;
    m.min   = 0x0080;      /* 2 KB min  */
    m.max   = 0x0400;      /* 64 KB wanted */
    m.pdadr = 0;
    m.flags = 0;

    bx = _bdos_mem( 128, &m );

    cprintf( "BDOS128 bx=%04x start=%04x max=%04x (paras)\r\n",
             bx, m.start, m.max );
    if( bx != 0xFFFF && m.start != 0 )
        cprintf( "OK: granted %u paras (%u KB) at seg %04x\r\n",
                 m.max, (unsigned)( m.max >> 6 ), m.start );
    else
        cprintf( "FAIL: fn 128 not supported / no memory\r\n" );

#ifdef MAME_DONE
    /* HI=1 if fn128 unsupported/failed, LO=granted paras low byte */
    mame_done( (unsigned)( ( ( bx == 0xFFFF || m.start == 0 ) ? 1 : 0 ) << 8 )
               | ( m.max & 0xFF ) );
#endif
    return( 0 );
}
