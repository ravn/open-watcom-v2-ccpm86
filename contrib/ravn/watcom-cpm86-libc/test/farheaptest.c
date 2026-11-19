/* farheaptest.c -- overlap-detecting stress test for the Stage A far heap
   (Phase A4, tasks/plan-cpm86-big-model-2026-08-18.md).

   Allocates pseudo-random-sized blocks (a fixed LCG, so the run is
   reproducible) via _fmalloc -- the explicit far-heap API, not plain
   malloc(), so this exercises port/farheap.c's __AllocSeg regardless of
   compile model -- UNTIL _fmalloc fails, i.e. it uses as much far heap as
   the system actually has, not a fixed target. This matters on CP/M-86:
   the linked `OPTION FARHEAP=<size>` is only a CEILING (G_Max); the
   loader's real grant (G_Min..G_Max, whatever memory is genuinely free)
   can be smaller and is only known once the program is running -- reading
   a fixed compile-time constant here would either waste real, available
   RAM or (as first tried) request more than a given machine actually has
   and get refused at LOAD time entirely ("Concurrent Fejl: For lidt
   lager" on real Concurrent CP/M-86 -- see farheap.c's comments).

   Each block i gets a fill pattern with its OWN start value and slope:

       byte j of block i  ==  (unsigned char)( pattern_start(i) + pattern_slope(i) * j )

   pattern_start(i) = i*97+1 has period 256 in i (97 is odd, coprime with
   256). A first version used ONLY this -- a single shared +1 slope for
   every block -- which has a real blind spot caught in review: blocks
   256 apart in allocation order (e.g. i and i+256) got an IDENTICAL
   start value AND slope, so an overlap between that exact pair would be
   completely invisible, not just improbable. pattern_slope(i) fixes
   this by using a DIFFERENT period: `(i % 251) * 3 + 5`, forced odd, has
   period 251 (251 is prime, doesn't divide 256). Two DISTINCT blocks
   i != k can only share BOTH start and slope if 256 | (i-k) AND 251 |
   (i-k), i.e. only if 256*251 = 64256 | (i-k) -- far beyond MAXALLOC
   below, so no two blocks in one run ever get the identical sequence.
   When only ONE of start/slope differs, the two blocks' byte sequences
   can still coincide at isolated points (an 8-bit pattern space can't
   rule that out completely), but never over a whole overlapping run --
   a real allocator bug (overlapping segments) corrupts many contiguous
   bytes, and the chance of *all* of them coincidentally matching a
   different slope's sequence is astronomically small.

   All blocks are filled FIRST, then ALL are verified in a second pass --
   so a later allocation's fill corrupting an earlier block (the actual
   bug class this is designed to catch: overlapping segments/pointer
   arithmetic mistakes in __AllocSeg) is only checked for after every
   allocation has had a chance to stomp on it. */

#include <malloc.h>
#include <stdlib.h>
#include <i86.h>        /* FP_SEG */

extern int cprintf( const char *, ... );

/* _getds -- read the CURRENT DS register (== DGROUP's segment). Same
 * pattern as port/diskio.c's _getds. Used below to detect Watcom's OWN
 * _fmalloc() fallback: when the far heap proper is exhausted, fmalloc.c
 * (bld/clib/heap/c/fmalloc.c) tries `_nmalloc()` (the NEAR heap) as a
 * last resort and returns it disguised as a far pointer with segment ==
 * _DGroup(). Left undetected, that would silently blend near-heap-backed
 * allocations into a "far heap" measurement -- this test stops (without
 * counting) the first time it sees that segment, so `n`/`total` reflect
 * ONLY the Extra group's real capacity. */
extern unsigned _getds( void );
#pragma aux _getds =            \
    "mov ax,ds"                 \
    value [ax]                  \
    modify [ax];

/* Upper bound on how many blocks a run could plausibly need -- generous
   headroom over any real CP/M-86 far-heap budget seen so far (RC759's own
   261 K bytes brugerlager / 512-4095 B blocks tops out around 500). Not a
   target: the loop below stops at the first failed _fmalloc, whichever
   comes first. Also well under the 64256-block period at which the fill
   pattern below could first repeat for two different blocks. */
#define MAXALLOC  600

static unsigned lcg_seed = 12345u;

static unsigned lcg_next( void )
{
    lcg_seed = lcg_seed * 25173u + 13849u;
    return( lcg_seed );
}

static unsigned char pattern_start( int i )
{
    return( (unsigned char)( i * 97 + 1 ) );
}

static unsigned char pattern_slope( int i )
{
    return( (unsigned char)( ( ( i % 251 ) * 3 + 5 ) | 1 ) );
}

int main( void )
{
    static void  __far   *blk[MAXALLOC];
    static unsigned       sz[MAXALLOC];
    int                    n;
    int                    i;
    unsigned               j;
    unsigned char          start, slope;
    unsigned char __far   *p;
    int                    fails;
    unsigned long          total;

    total = 0;
    for( n = 0; n < MAXALLOC; n++ ) {
        unsigned trysz = 512u + ( lcg_next() % 3584u );  /* 512..4095 bytes */
        void __far *cand = _fmalloc( trysz );

        if( cand == 0 ) {
            cprintf( "far heap exhausted (real out-of-memory)\n" );
            break;
        }
        if( FP_SEG( cand ) == _getds() ) {
            /* Watcom's own near-heap fallback kicked in (fmalloc.c falls
             * back to _nmalloc() once the Extra group is really out of
             * room) -- this allocation is DGROUP-backed, not Extra-group,
             * so stop here without counting it. */
            cprintf( "far heap exhausted (near-heap fallback seg %04x reached)\n",
                      FP_SEG( cand ) );
            break;
        }
        blk[n] = cand;
        sz[n] = trysz;
        total += trysz;
        start = pattern_start( n );
        slope = pattern_slope( n );
        p = (unsigned char __far *)blk[n];
        for( j = 0; j < sz[n]; j++ )
            p[j] = (unsigned char)( start + slope * j );
    }
    cprintf( "allocated %lu bytes in %d blocks from the Extra group\n", total, n );

    fails = 0;
    for( i = 0; i < n; i++ ) {
        start = pattern_start( i );
        slope = pattern_slope( i );
        p = (unsigned char __far *)blk[i];
        for( j = 0; j < sz[i]; j++ ) {
            unsigned char want = (unsigned char)( start + slope * j );
            if( p[j] != want ) {
                cprintf( "MISMATCH block %d offset %u: want %02x got %02x\n",
                          i, j, (unsigned)want, (unsigned)p[j] );
                fails++;
                break;      /* one report per corrupted block is enough */
            }
        }
    }
    cprintf( "%s (%d block%s corrupted)\n",
              ( fails == 0 ) ? "PASS" : "FAIL",
              fails, ( fails == 1 ) ? "" : "s" );
    return( fails != 0 );
}
