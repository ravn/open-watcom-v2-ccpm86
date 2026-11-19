/* compact_farheap_test.c -- diagnostic oracle for the COMPACT-model (-mc)
 * CP/M-86 far heap. Catches the "near arena clobbers the base page" bug.
 *
 * THE BUG THIS GUARDS AGAINST (found 2026-08-19):
 *   In -mc, module-level data defaults to FAR, so the clib's near-heap arena
 *   `wc_arena[]` (port/lowlevel.c) lands in a FAR_DATA/AUTO segment, NOT in
 *   DGROUP. But lowlevel.c's wc_heap_init() still seeds the NEAR break with
 *   `_curbrk = (unsigned)&wc_arena[0]` -- taking the *near offset* of a far
 *   object, which is 0. The near heap then hands out DS:0x0000.. , writing
 *   straight over the CP/M-86 base page (DS:0x00..0xFF). That destroys the
 *   base-page EXTRA group descriptor at DS:0x0C (byte length) / DS:0x0F
 *   (segment) -- the very fields port/farheap.c reads to find the far heap.
 *   Net effect: the real far allocator (__AllocSeg) sees length 0 / seg 0 and
 *   returns _NULLSEG, so a big malloc() returns NULL (or worse, silent
 *   corruption). See tasks/memory/reference_cpm86_p_load_fixups.md sec 8 and
 *   reference_wlink_cpm86_far_data_type3.md.
 *
 * WHAT A CORRECT BUILD MUST SATISFY (each printed as 1=pass, 0=fail):
 *   c1  base-page EXTRA descriptor survives startup: DS:0x0C length nonzero
 *       AND DS:0x0F segment nonzero (the loader wrote them; nothing clobbered).
 *   c2  plain malloc() of a big block (compact -> _fmalloc -> far heap) != NULL.
 *   c3  that block lives OUTSIDE DGROUP: its segment != DS. (A near-arena
 *       collision would return DS:low instead -- this is the direct tripwire.)
 *   c4  >64 KB round-trips across NBLK far blocks (proves real far heap, not a
 *       single <=64 KB DGROUP arena).
 *   c5  PROGRAM FAR DATA coexists with the far heap: a __far canary array is
 *       seeded with a checkable pattern BEFORE any allocation, then re-verified
 *       AFTER filling >64 KB of far blocks. This is the UnZip scenario (its
 *       ~22 KB message strings are far data in the SAME EXTRA group the heap
 *       carves from). If __AllocSeg starts carving at EXTRA offset 0 it will
 *       hand out memory ON TOP of the far data -> the canary is corrupted.
 *       (c5==0 with c1..c4==1 means the base-page bug is fixed but the
 *       far-data/far-heap overlap is still live.)
 *   c6  the NEAR heap still works after the fix: an explicit _nmalloc() block
 *       is non-NULL, lives INSIDE DGROUP (its far segment == DS), and
 *       round-trips. Guards against the fix moving the near arena somewhere the
 *       near allocator can't use, and is the small/medium-model heap path.
 *   c7  heap reuse: after free()ing every far block, a fresh malloc(BSZ)
 *       succeeds again (the allocator reclaims, not just bump-and-die).
 *
 * Output: one line
 *     C <c1..c7>  L<len16> H<len_hi> S<extra_seg> D<ds> M<malloc_seg> F<canary_seg>
 * Expect "C 1111111 ..." on a correct build. On the ORIGINAL near-arena bug you
 * get "C 0000... L0001 H0000 S0000 ..." -- L/S zeroed = base page clobbered. If
 * only c5 is 0, the secondary far-data overlap has surfaced.
 */
#include <stdlib.h>
#include <malloc.h>

#pragma aux bdos_conout = "mov cl,2" "int 0E0h" parm [dl] modify [cl];
extern void bdos_conout( char c );
#pragma aux getds = "mov ax,ds" value [ax] modify [ax];
extern unsigned getds( void );

#define NBLK  8
#define BSZ   12000u          /* 8 * 12000 = 96000 bytes > 64 KB */
#define CANSZ 8192u           /* program far-data canary size */

/* Program FAR data: forced into a far segment (the EXTRA group in -mc) so it
 * shares the region the far heap carves slabs from. Uninitialised -> far BSS.
 * Seeded with CANBYTE() before any allocation, re-checked after. */
static char __far far_canary[CANSZ];
#define CANBYTE(k)  ((char)( 0x5Au ^ (unsigned char)((k) * 31u + 7u) ))

static void hex4( unsigned v )
{
    static const char h[] = "0123456789ABCDEF";
    bdos_conout( h[(v >> 12) & 0xF] );
    bdos_conout( h[(v >>  8) & 0xF] );
    bdos_conout( h[(v >>  4) & 0xF] );
    bdos_conout( h[ v        & 0xF] );
}
static void sp( void ) { bdos_conout( ' ' ); }

int main( void )
{
    unsigned char near *le = (unsigned char near *)0x000CU;  /* base-page EXTRA len  */
    unsigned near      *be = (unsigned near *)0x000FU;        /* base-page EXTRA seg  */
    char *blk[NBLK];
    union { char *p; unsigned w[2]; } u;
    unsigned ds, extra_len16, extra_hi, extra_seg, malloc_seg, canary_seg;
    unsigned i, j;
    int c1, c2, c3, c4, c5, c6, c7;

    /* Seed program far data BEFORE any allocation. */
    for( i = 0; i < CANSZ; i++ )
        far_canary[i] = CANBYTE( i );
    { union { char __far *p; unsigned w[2]; } uc; uc.p = far_canary; canary_seg = uc.w[1]; }

    ds = getds();
    extra_len16 = (unsigned)le[0] | ((unsigned)le[1] << 8);
    extra_hi    = (unsigned)le[2];
    extra_seg   = *be;

    /* c1: base page intact -- a real far-heap reservation has nonzero length
     * (high byte carries most of a >64 KB size, e.g. 0x30000 -> hi=3) AND a
     * nonzero segment. The bug zeroes both. */
    c1 = ( (extra_len16 | extra_hi) != 0 ) && ( extra_seg != 0 );

    /* c2/c3: one big far allocation. */
    u.p = malloc( BSZ );
    malloc_seg = u.w[1];
    c2 = ( u.p != NULL );
    c3 = ( c2 && malloc_seg != ds );     /* must NOT be in DGROUP */
    blk[0] = u.p;

    /* c4: fill+verify >64 KB total across NBLK blocks. */
    c4 = c2;
    for( i = 1; c4 && i < NBLK; i++ ) {
        blk[i] = malloc( BSZ );
        if( blk[i] == NULL ) { c4 = 0; break; }
    }
    if( c4 ) {
        for( i = 0; i < NBLK; i++ )
            for( j = 0; j < BSZ; j++ )
                blk[i][j] = (char)( i * 97u + 1u + j );
        for( i = 0; c4 && i < NBLK; i++ )
            for( j = 0; j < BSZ; j++ )
                if( (unsigned char)blk[i][j] != (unsigned char)( i * 97u + 1u + j ) )
                    { c4 = 0; break; }
    }

    /* c5: program far data survived the heap traffic. If __AllocSeg carved
     * over far_canary, the >64 KB of writes above have trashed the pattern. */
    c5 = 1;
    for( i = 0; i < CANSZ; i++ )
        if( far_canary[i] != CANBYTE( i ) ) { c5 = 0; break; }

    /* c6: the NEAR heap (explicit _nmalloc) still works and lives in DGROUP. */
    {
        char near *np = (char near *)_nmalloc( 256u );
        union { char near *p; unsigned w; } un; un.p = np;
        c6 = ( np != NULL );
        if( c6 ) {
            for( j = 0; j < 256u; j++ ) np[j] = (char)( j ^ 0x33u );
            for( j = 0; c6 && j < 256u; j++ )
                if( (unsigned char)np[j] != (unsigned char)( j ^ 0x33u ) ) c6 = 0;
            /* a near block's implicit segment is DS: prove it is in DGROUP by
             * reading it back through an explicit DS:offset far pointer. */
            if( c6 ) {
                char __far *fp;
                union { char __far *p; unsigned w[2]; } uf;
                uf.w[0] = un.w; uf.w[1] = ds; fp = uf.p;
                if( (unsigned char)fp[0] != (unsigned char)( 0 ^ 0x33u ) ) c6 = 0;
            }
        }
    }

    /* c7: heap reuse -- free everything, then a fresh big malloc must succeed. */
    c7 = 0;
    if( c2 ) {
        for( i = 0; i < NBLK; i++ ) if( blk[i] ) free( blk[i] );
        { char *r = malloc( BSZ ); c7 = ( r != NULL ); if( r ) free( r ); }
    }

    bdos_conout( 'C' ); sp();
    bdos_conout( c1 ? '1' : '0' );
    bdos_conout( c2 ? '1' : '0' );
    bdos_conout( c3 ? '1' : '0' );
    bdos_conout( c4 ? '1' : '0' );
    bdos_conout( c5 ? '1' : '0' );
    bdos_conout( c6 ? '1' : '0' );
    bdos_conout( c7 ? '1' : '0' ); sp();
    bdos_conout( 'L' ); hex4( extra_len16 ); sp();
    bdos_conout( 'H' ); hex4( extra_hi );    sp();
    bdos_conout( 'S' ); hex4( extra_seg );   sp();
    bdos_conout( 'D' ); hex4( ds );          sp();
    bdos_conout( 'M' ); hex4( malloc_seg );  sp();
    bdos_conout( 'F' ); hex4( canary_seg );
    bdos_conout( '\r' ); bdos_conout( '\n' );
    return ( c1 && c2 && c3 && c4 && c5 && c6 && c7 ) ? 0 : 1;
}
