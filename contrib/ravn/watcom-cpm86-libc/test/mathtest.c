/* mathtest.c -- per-model libm proof: Open Watcom's OWN soft-float transcendentals
 * (sin/cos/atan/exp/log/sqrt, -fpc, no 8087) run on CP/M-86 in EVERY memory model.
 *
 * The transcendentals are model-sensitive (near/far RET for the call convention;
 * their private coefficient tables sit in DGROUP for near-data models but embed
 * in the code segment for far-data compact) so each model links its OWN libm
 * (libm{s,m,c}.lib). This test asserts all three produce the SAME result: the
 * small-model output is the oracle (it matches the whetstone libm proof); medium
 * and compact must match it byte-for-byte.
 *
 * Operands are 'volatile' so nothing is constant-folded -- every value is a real
 * runtime call into the libm entry (IF@DSIN/DCOS/DATAN/DEXP/DLOG/CDSQRT) over the
 * __FDx soft-float core. Scaled to long via __FDI4 and printed with the proven
 * stdio %ld path (crt0 attaches stdout before main()).
 */
#include <stdio.h>
#include <math.h>

int main( void )
{
    volatile double one = 1.0, two = 2.0, ten = 10.0;
    long s  = (long)( sin( one )  * 1000000.0 );
    long c  = (long)( cos( one )  * 1000000.0 );
    long at = (long)( atan( one ) * 1000000.0 );
    long e  = (long)( exp( one )  * 1000000.0 );
    long l  = (long)( log( ten )  * 1000000.0 );
    long q  = (long)( sqrt( two ) * 1000000.0 );

    printf( "sin=%ld cos=%ld atan=%ld exp=%ld log=%ld sqrt=%ld\n", s, c, at, e, l, q );
    fflush( stdout );
    return( 0 );
}
