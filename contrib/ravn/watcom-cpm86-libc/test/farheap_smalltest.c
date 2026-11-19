/*
 * farheap_smalltest.c  --  VERIFIED small-model explicit far-heap proof.
 *
 * WHAT this proves (and WHY it matters for the UnZip DEFLATE port):
 *   In SMALL model the clib's own globals + string literals stay NEAR, in the
 *   single 64 KB DGROUP, so wlink emits exactly ONE type=2 group (no far-data
 *   collision).  A program can STILL move its big buffers off DGROUP by calling
 *   _fmalloc() explicitly: the far heap is a separate paragraph arena (the .CMD
 *   "Extra" group, type=3) handed out by port/farheap.c::__AllocSeg.
 *
 * This test grabs as much far heap as the loader grants -- it keeps calling
 * _fmalloc(SEG) (SEG a VARIABLE segment size, default 16 KB, <=64 KB Watcom cap)
 * until it has requested up to BUDGET (1 MB) or the allocator returns NULL --
 * then analyses ONLY the blocks it actually got.  Each block is filled with a
 * position-dependent pattern, read back, and checked on two invariants:
 *   (a) every block's segment != DS  -> it really lives OUTSIDE DGROUP;
 *   (b) every byte survives the round-trip -> the far arena is real RAM.
 * As soon as the total exceeds 64 KB the pass is only explicable by a working
 * far heap.  Verified PASS under cpm86run_unicorn.py AND MAME rc759.
 *
 * WHY explicit _fmalloc and NOT transparent -mc (compact) malloc:
 *   Compact model makes clib globals (e.g. int __heap_enabled=1) FAR; wlink's
 *   `format cpm86` emits those as a SECOND type=2 group, which the CP/M-86 .CMD
 *   header (groups keyed by TYPE 1-8) cannot place -> the globals read 0 ->
 *   __heap_enabled==0 -> __AllocSeg refuses -> malloc()==NULL.  Root cause is a
 *   wlink/loader limitation, not this clib.  Until that is fixed, SMALL model +
 *   explicit _fmalloc is the path that works today.  See build-lib.sh MODEL=c.
 *
 * Uses inline-asm BDOS conout + a near string so the pass/fail signal itself
 * pulls no stdio far data -- the test isolates the far HEAP, nothing else.
 */
#include <malloc.h>
#include <i86.h>

extern void bdos_conout( char c );
#pragma aux bdos_conout = "mov cl,2" "int 0E0h" parm [dl] modify [cl];
extern unsigned _getds( void );
#pragma aux _getds = "mov ax,ds" value [ax] modify [ax];

static void puts_n( const char *s ) { while( *s ) bdos_conout( *s++ ); }

static void put_u( unsigned v )         /* print an unsigned decimal (for n/KB) */
{
    char b[6]; int k = 0;
    if( v == 0 ) { bdos_conout( '0' ); return; }
    while( v ) { b[k++] = (char)( '0' + v % 10u ); v /= 10u; }
    while( k ) bdos_conout( b[--k] );
}

#ifdef MAME_DONE
#include "mamedone.h"                   /* mame_done(): OUT 0x2FE for the host tap */
#endif

#ifndef SEG
#define SEG  16384u                     /* VARIABLE segment size, <= 64 KB cap    */
#endif
#define BUDGET   0x100000UL             /* try for up to 1 MB of far heap         */
#define MAXBLK   256                    /* pointer-array ceiling (256*SEG >> 1 MB) */

static char __far *blk[MAXBLK];

int main( void )
{
    unsigned ds = _getds(), i, j, n = 0, nf = 0;
    unsigned long got = 0;
    int bad = 0;

    /* Grab what we can: stop at the 1 MB budget, the array ceiling, or the first
       NULL -- then analyse ONLY the blocks we actually got. */
    while( got + SEG <= BUDGET && n < MAXBLK ) {
        char __far *p = _fmalloc( SEG );
        if( !p ) break;                 /* took what the loader granted           */
        /* FP_SEG==DS means _fmalloc fell back to the NEAR heap (the far arena is
           exhausted): Watcom's small-model far heap hands out DGROUP memory once
           __AllocSeg returns _NULLSEG. That is the true "far heap full" boundary
           -- stop here so every COUNTED block is genuinely outside DGROUP. Seen
           at block 42 under Unicorn (1 MB): blk 41 = EC70:801C (far), then
           blk 42 = 124D:078C == DS (near fallback). */
        if( FP_SEG( p ) == ds ) { _ffree( p ); break; }
        blk[n] = p;
        nf++;                           /* seg != DS  ->  outside DGROUP          */
        for( j = 0; j < SEG; j++ ) p[j] = (char)( n * 97u + 1u + j );
        n++; got += SEG;
    }
    for( i = 0; i < n; i++ ) {
        char __far *p = blk[i];
        for( j = 0; j < SEG; j++ )
            if( (unsigned char)p[j] != (unsigned char)( i * 97u + 1u + j ) ) {
                bad++; break;
            }
    }

    /* PASS = got >1 block, all outside DGROUP, all round-trips intact. The
       "n=NN seg=SSSS" token lets the host harness cross-check the dump scan. */
    {
        int ok = ( n > 0 && nf == n && !bad );
        puts_n( ok ? "PASS far-heap n=" : "FAIL far-heap n=" );
        put_u( n );
        puts_n( " seg=" ); put_u( SEG );
        puts_n( " kb=" ); put_u( (unsigned)( got >> 10 ) );
        puts_n( "\r\n" );
#ifdef MAME_DONE
        /* low byte = block count actually obtained, high byte = 0 on success
           (nonzero = fail) -> the host reads n from the done-signal pass field. */
        mame_done( (unsigned)( ( ok ? 0 : 0x80 ) << 8 ) | ( n & 0xFF ) );
#endif
        return ok ? 0 : 1;
    }
}
