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
#include <i86.h>
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
    static struct mpb m[4];   /* static -> lives in DS (DGROUP), not SS */
    static unsigned min_want[4] = { 1, 4, 16, 64 };
    static unsigned max_want[4] = { 4, 16, 64, 1024 };
    unsigned i, j, bx, ok = 1, successes = 0;

    cprintf( "BDOS128 contract test\r\n" );
    for( i = 0; i < 4; ++i ) {
        m[i].start = 0;
        m[i].min = min_want[i];
        m[i].max = max_want[i];
        m[i].pdadr = 0;
        m[i].flags = 0;
        bx = _bdos_mem( 128, &m[i] );
        cprintf( "alloc min=%u bx=%04x start=%04x max=%u\r\n",
                 min_want[i], bx, m[i].start, m[i].max );
        if( bx == 0xFFFF )
            continue; /* exhaustion is valid after earlier successful grants */
        ++successes;
        if( m[i].start == 0 || m[i].max < m[i].min ||
            m[i].max > max_want[i] ) {
            cprintf( "FAIL: invalid returned MPB\r\n" );
            ok = 0;
            continue;
        }
        {
            unsigned char __far *p =
                (unsigned char __far *)MK_FP( m[i].start, 0 );
            unsigned long bytes = (unsigned long)m[i].max * 16UL;
            unsigned char pattern = (unsigned char)( 0x31 + i );
            for( j = 0; (unsigned long)j < bytes; ++j )
                p[j] = pattern;
            for( j = 0; (unsigned long)j < bytes; ++j )
                if( p[j] != pattern ) {
                    ok = 0;
                    break;
                }
        }
    }
    cprintf( "BDOS128 result: %s (%u grants)\r\n",
             ok && successes ? "PASS" : "FAIL", successes );
#ifdef MAME_DONE
    mame_done( (unsigned)( ( ok && successes ? 0 : 1 ) << 8 )
               | ( successes & 0xFF ) );
#endif
    return ok && successes ? 0 : 1;
}
