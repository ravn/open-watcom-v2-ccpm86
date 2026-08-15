/* floattest.c -- rc7xx-work #8: proof that Open Watcom's OWN double SOFT-FLOAT
 * (-fpc, the chip-less __FDxemu path) runs on CP/M-86 with NO 8087. The operands
 * are 'volatile' so the compiler CANNOT constant-fold the arithmetic: every op
 * below is emitted as a genuine runtime CALL into Watcom's UNCHANGED __FDx
 * runtime (__FDM/__FDD/__FDA/__FDS + __FDI4 double->long). No inline 8087 opcode
 * is emitted or executed and no interrupt-vector emulator is installed.
 *
 * Printing uses the already-proven stdio FILE* write-path; stdout is attached
 * by crt0 via __CommonInit (port/cominit.c, ow#16) before main() runs, so we
 * only fflush(stdout) at the end.
 *
 * Oracle (hand-computed, independent of the compiler):
 *     a=355, b=113
 *     (long)((a/b)*1e6) = 3141592   (355/113 = 3.14159292...; __FDD,__FDM,__FDI4)
 *     (long)(a*b)       = 40115     (__FDM)
 *     (long)(a+b)       = 468       (__FDA)
 *     (long)(a-b)       = 242       (__FDS)
 */
#include <stdio.h>

int main( void )
{
    volatile double a = 355.0, b = 113.0;
    long pi6 = (long)((a / b) * 1000000.0);
    long mul = (long)(a * b);
    long add = (long)(a + b);
    long sub = (long)(a - b);

    printf( "pi6=%ld mul=%ld add=%ld sub=%ld\n", pi6, mul, add, sub );
    fflush( stdout );
    return( 0 );
}
