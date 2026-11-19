/* coninput_test.c -- console (stdin) scanf on CP/M-86.
 *
 * Unlike scanffmt_test.c (which uses sscanf on a literal), this reads from the
 * CONSOLE via scanf(stdin) -> fgetc(stdin) -> __qread(0,...) -> port/diskio.c's
 * con_read(), which pulls bytes with BDOS C_READ (fn 1) and maps CR->'\n', ^Z->
 * EOF. So it exercises the real stdin byte path, not just the %-parser.
 *
 * Feed the runner "3.14159 42\n" on stdin. Oracle: n=2 f=314159 i=42
 * (the runner echoes the typed line first, which the harness strips before the
 * oracle compare).
 */
#include <stdio.h>

extern void __setEFGfmt( void );

int main( void )
{
    double f = 0.0;
    int    i = 0, n;

    __setEFGfmt();
    n = scanf( "%lf %d", &f, &i );
    printf( "n=%d f=%ld i=%d\n", n, (long)( f * 100000.0 ), i );
    fflush( stdout );
    return( 0 );
}
