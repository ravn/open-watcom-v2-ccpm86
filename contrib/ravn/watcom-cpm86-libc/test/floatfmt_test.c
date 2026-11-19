/* floatfmt_test.c -- OPT-IN real %e/%f/%g printf formatting on CP/M-86.
 *
 * The default clib links the noefgfmt stub, so %f prints nothing. Calling
 * __setEFGfmt() once installs Watcom's genuine double->decimal formatter
 * (_EFG_Format + the dtoa/cvt subsystem, archived in the clib but pulled ONLY by
 * this reference). Our minimal crt0 does not walk Watcom's auto-init table, so
 * this explicit call is the install hook. Operands are volatile (no folding);
 * this exercises the -fpc soft-float path (no 8087) through the formatter.
 *
 * Oracle (independent of the compiler): f=3.1416 e=2.500e+00 g=0.001 a=0x1.8p+0
 * (%a is C99 hex-float; 1.5 == 0x1.8 x 2^0. Needs the SOURCE-built efgfmt in the
 * clib -- the prebuilt libm formatter lacks %a.)
 */
#include <stdio.h>

extern void __setEFGfmt( void );        /* install real %e/%f/%g/%a formatter */

int main( void )
{
    volatile double x = 3.14159265, y = 2.5, z = 0.001, w = 1.5;

    __setEFGfmt();
    printf( "f=%.4f e=%.3e g=%g a=%a\n", x, y, z, w );
    fflush( stdout );
    return( 0 );
}
