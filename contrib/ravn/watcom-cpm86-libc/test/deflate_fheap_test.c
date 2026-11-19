/* deflate_fheap_test.c -- reproduce the CP/M-86 large-model far-heap corruption
   that hangs Info-ZIP `zip`'s deflate (see
   tasks/memory/reference_zip_cpm86_needs_large_model.md, 2026-08-25).

   Root cause pinned by a 640 KB RAM dump at the hang: `_fcalloc` (the Watcom
   large-model far heap, port/farheap.c + the M9 CPM86_FARHEAP_PARAS accounting)
   hands deflate a BOGUS `window` far pointer -- it aims at the allocator's own
   control block / a wrong segment instead of a real 8 KB data block. So the
   input never lands in the window and the match/scan loop spins forever.

   This test allocates the EXACT deflate triple, in order and size, via the SAME
   entry point zip uses (zcalloc == _fcalloc):

       window = _fcalloc(WSIZE,     2)   -- 8 KB   (deflate.c: 2*sizeof(uch))
       prev   = _fcalloc(WSIZE,     2)   -- 8 KB   (sizeof(Pos))
       head   = _fcalloc(HASH_SIZE, 2)   -- 8 KB   (sizeof(Pos), HASH_BITS=12)

   and checks the four properties a correct far heap must give deflate:
     F_NULL   (0x01): an allocation returned NULL.
     F_ZERO   (0x02): _fcalloc did not zero a block (calloc contract).
     F_ALIAS  (0x04): writing one block corrupted another (overlap / bogus ptr).
     F_OVLAP  (0x08): two blocks' [seg:0 .. seg:size) linear ranges overlap.

   Build LARGE model (-mcmodel=l), linking clibl.lib + the M9 farheap.c with
   -DCPM86_FARHEAP_PARAS, exactly like build-zip-cpm86.sh -- the bug is
   model-specific, so a small-model build would NOT reproduce it.

   Expected: PASS under emu2 (far heap laid out benignly); FAIL on real CCP/M-86
   MAME until port/farheap.c is fixed. It is the oracle for that fix. */

#include <malloc.h>     /* _fcalloc / _ffree / malloc (far heap, large model) */
#include <i86.h>        /* FP_SEG / FP_OFF */

/* Force the CMD's own footprint up to ~zip's 190 KB so the far heap is handed
   out from a HIGH segment (near rc759's 384 KB ceiling) -- the bug lives at that
   boundary, not in a low, empty heap.  ~160 KB of BSS pad would blow DGROUP
   (64 KB near limit), so pad in FAR data: far arrays land outside DGROUP and
   lift the load-time far-data base.  Referenced (touched) below so the linker
   cannot drop them. */
#define PADN 4
static char __far g_pad[ PADN ][ 40000 ];

#ifdef MAME_DONE
#include "mamedone.h"   /* mame_done(): OUT 0x2FE for the host tap */
#endif

extern int cprintf( const char *, ... );

/* Match the zip build's deflate geometry (build-zip-cpm86.sh). */
#define WSIZE      0x1000u          /* -DWSIZE=0x1000                     */
#define HASH_BITS  12               /* -DHASH_BITS=12                     */
#define HASH_SIZE  (1u << HASH_BITS)

#define NBLK 3
#define F_NULL  0x01
#define F_ZERO  0x02
#define F_ALIAS 0x04
#define F_OVLAP 0x08

/* linear (20-bit) base address of a far pointer, seg:0 assumed by _fcalloc */
static unsigned long lin( void __far *p )
{
    return( (unsigned long)FP_SEG( p ) * 16uL + (unsigned long)FP_OFF( p ) );
}

int main( void )
{
    unsigned char __far *blk[NBLK];
    unsigned            sz[NBLK];
    unsigned long       base[NBLK];
    int                 i, j;
    unsigned            fail = 0;

    sz[0] = WSIZE * 2u;                 /* window */
    sz[1] = WSIZE * 2u;                 /* prev   */
    sz[2] = HASH_SIZE * 2u;             /* head   */

    /* 1. allocate the deflate triple via the SAME call zip uses */
    for( i = 0; i < NBLK; i++ ) {
        blk[i] = (unsigned char __far *)_fcalloc( sz[i], 1u );
        if( blk[i] == (unsigned char __far *)0 ) {
            cprintf( "block %d: _fcalloc(%u) returned NULL\r\n", i, sz[i] );
            fail |= F_NULL;
            base[i] = 0;
            continue;
        }
        base[i] = lin( blk[i] );
        cprintf( "block %d: %u B @ %04x:%04x (lin %05lx)\r\n",
                 i, sz[i], FP_SEG( blk[i] ), FP_OFF( blk[i] ), base[i] );
    }

    /* 2. calloc contract: every byte must read back zero before we touch it */
    for( i = 0; i < NBLK; i++ ) {
        if( blk[i] == 0 ) continue;
        for( j = 0; j < (int)sz[i]; j++ ) {
            if( blk[i][j] != 0 ) {
                cprintf( "block %d: NOT zeroed at %d (=%02x)\r\n",
                         i, j, blk[i][j] );
                fail |= F_ZERO;
                break;
            }
        }
    }

    /* 3. distinctness: stamp each whole block with its own byte value, THEN
          read them all back. If writing block k corrupted block i (overlap or
          a bogus pointer that aliases live memory), the read-back mismatches. */
    for( i = 0; i < NBLK; i++ )
        if( blk[i] )
            for( j = 0; j < (int)sz[i]; j++ )
                blk[i][j] = (unsigned char)( 0xA1 + i );

    for( i = 0; i < NBLK; i++ ) {
        if( blk[i] == 0 ) continue;
        for( j = 0; j < (int)sz[i]; j++ ) {
            if( blk[i][j] != (unsigned char)( 0xA1 + i ) ) {
                cprintf( "block %d: ALIAS/corrupt at %d: want %02x got %02x\r\n",
                         i, j, (unsigned)( 0xA1 + i ), blk[i][j] );
                fail |= F_ALIAS;
                break;
            }
        }
    }

    /* 4. explicit linear-range overlap check between every pair */
    for( i = 0; i < NBLK; i++ ) {
        if( blk[i] == 0 ) continue;
        for( j = i + 1; j < NBLK; j++ ) {
            if( blk[j] == 0 ) continue;
            if( base[i] < base[j] + sz[j] && base[j] < base[i] + sz[i] ) {
                cprintf( "OVERLAP: block %d [%05lx+%u] & block %d [%05lx+%u]\r\n",
                         i, base[i], sz[i], j, base[j], sz[j] );
                fail |= F_OVLAP;
            }
        }
    }

    cprintf( "%s (fail-mask 0x%02x)\r\n", fail ? "FAIL" : "PASS", fail );

#ifdef MAME_DONE
    /* HI byte = fail-mask (0 == PASS), LO byte = blocks obtained */
    {
        int got = 0;
        for( i = 0; i < NBLK; i++ ) if( blk[i] ) got++;
        mame_done( (unsigned)( ( fail & 0xFF ) << 8 ) | ( got & 0xFF ) );
    }
#endif
    return( fail != 0 );
}
