/* redirtest.c -- command-line stdin/stdout redirection on CP/M-86 (< > >>).
 *
 * CP/M's CCP has no I/O redirection; our port adds it in the C runtime. The crt0
 * command-tail parser splits argv, then port/diskio.c's __apply_redirection
 * recognises  <file / >file / >>file, opens each as a disk file, reroutes the
 * stdin(0)/stdout(1) low-level handles onto it (see __qread/__qwrite), and --
 * crucially -- REMOVES those operands from argv so main() never sees them.
 *
 * Invoke it as:   redirtest WORLD <IN.TXT >OUT.TXT
 * The crt0 tail is "WORLD <IN.TXT >OUT.TXT"; after redirection main() sees only
 * argc=2, argv[1]=="WORLD" (the redirect operands are gone), stdin is IN.TXT and
 * stdout is OUT.TXT. The program prints its surviving argv (proving < > were
 * stripped), copies stdin->stdout, and appends a byte count. The build script
 * diffs OUT.TXT against the expected bytes -- an independent host-side oracle.
 */
#include <stdio.h>

int main( int argc, char **argv )
{
    int c;
    int n = 0;
    int i;

    /* Proof that main() does NOT see the redirect operands: echo the argv the
       program actually received. Expected "argc=2 [WORLD]" -- no <IN / >OUT. */
    fprintf( stdout, "argc=%d", argc );
    for( i = 1; i < argc; i++ )
        fprintf( stdout, " [%s]", argv[i] );
    fputc( '\n', stdout );

    while( ( c = fgetc( stdin ) ) != EOF ) {
        fputc( c, stdout );
        n++;
    }
    fprintf( stdout, "|bytes=%d\n", n );
    fflush( stdout );
    return( 0 );
}
