/* heaptest.c -- run-verified exercise of Watcom's GENUINE near-heap malloc
   family + qsort on CP/M-86, resolved only by the lowlevel.c BDOS/arena seam.
   Every printed value has a hand-computable oracle (see build-heap.sh), so a
   correct run is independent evidence the retargeted heap works -- not a mere
   "it linked" check.

   Oracle:
     sorted : 0 1 2 3 4 5 6 7 8 9      (qsort of a fixed permutation)
     calloc : 0                        (calloc must zero: sum of 4 ints == 0)
     realloc: 0 40                     (grow preserves r[0]=0 (smallest after sort); r[19] set to 40)
     reuse  : ok                       (free then malloc same size succeeds) */

#include <stdlib.h>
#include <string.h>

extern int cprintf( const char *, ... );

static int cmp_int( const void *a, const void *b )
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return( x - y );
}

int main( void )
{
    static const int seed[10] = { 5, 3, 8, 1, 9, 2, 7, 4, 6, 0 };
    int   *p;
    int   *c;
    int   *r;
    int    i;
    int    sum;

    /* --- malloc + qsort: sort a fixed permutation to 0..9 --- */
    p = (int *)malloc( 10 * sizeof( int ) );
    if( p == 0 ) { cprintf( "malloc FAIL\n" ); return( 1 ); }
    memcpy( p, seed, sizeof( seed ) );
    qsort( p, 10, sizeof( int ), cmp_int );
    cprintf( "sorted :" );
    for( i = 0; i < 10; i++ )
        cprintf( " %d", p[i] );
    cprintf( "\n" );

    /* --- calloc must zero --- */
    c = (int *)calloc( 4, sizeof( int ) );
    if( c == 0 ) { cprintf( "calloc FAIL\n" ); return( 1 ); }
    sum = c[0] + c[1] + c[2] + c[3];
    cprintf( "calloc : %d\n", sum );

    /* --- realloc grows p to 20 ints, preserving the sorted prefix --- */
    r = (int *)realloc( p, 20 * sizeof( int ) );
    if( r == 0 ) { cprintf( "realloc FAIL\n" ); return( 1 ); }
    r[19] = 40;
    cprintf( "realloc: %d %d\n", r[0], r[19] );

    /* --- free + re-malloc the same size must succeed --- */
    free( r );
    free( c );
    p = (int *)malloc( 10 * sizeof( int ) );
    cprintf( "reuse  : %s\n", ( p != 0 ) ? "ok" : "FAIL" );

    return( 0 );
}
