/* scanffmt_test.c -- scanf-side float parsing (sscanf %lf) on CP/M-86.
 *
 * The mirror of floatfmt_test.c: reading a double from text goes through
 * __EFG_scanf, which __setEFGfmt() points at __cnvs2d (in libm) -- so, exactly
 * like %f printing, the program calls __setEFGfmt() once and links libm. Uses
 * sscanf (string source) so the oracle is deterministic without console input.
 *
 * Oracle (independent of the compiler): n=2 f=314159 i=42
 *   sscanf("3.14159 42","%lf %d") -> f=3.14159 (x100000 = 314159), i=42, n=2.
 */
#include <stdio.h>

extern void __setEFGfmt( void );        /* install real %e/%f/%g <-> double */

int main( void )
{
    double f = 0.0;
    int    i = 0, n;

    __setEFGfmt();
    n = sscanf( "3.14159 42", "%lf %d", &f, &i );
    printf( "n=%d f=%ld i=%d\n", n, (long)( f * 100000.0 ), i );
    fflush( stdout );
    return( 0 );
}
