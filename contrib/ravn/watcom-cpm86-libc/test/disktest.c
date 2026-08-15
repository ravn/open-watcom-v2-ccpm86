/* disktest.c -- round-trip oracle for the CP/M-86 disk FILE* seam (diskio.c).
 *
 * rc7xx-work#7: exercise Open Watcom's UNCHANGED stdio FILE* layer against real
 * CP/M-86 disk files, resolved only by port/diskio.c (fopen -> _sopen, fwrite/
 * fputs -> __qwrite, fread/fgets/fgetc -> __qread, fseek/ftell -> __lseek,
 * fclose -> __close). Self-checking: every step is a VERIFY; on any failure it
 * prints the failing line and exits non-zero. Final line is DISKIO: PASS.
 *
 * The record model is CP/M's (128-byte sectors, Ctrl-Z text EOF), so we drive
 * the seam the way a real program does and let the numbers fall out.
 */
#include <stdio.h>
#include <string.h>

static int tests, failures;

#define VERIFY( expr )                                                     \
    do {                                                                   \
        ++tests;                                                           \
        if( !(expr) ) {                                                    \
            printf( "***FAIL*** line %u: %s\n", __LINE__, #expr );         \
            ++failures;                                                    \
        }                                                                  \
    } while( 0 )

int main( void )
{
    FILE *fp;
    char  buf[128];
    long  pos;
    int   c;

    /* --- write pass: create TEST.TXT, put text + a formatted number --- */
    fp = fopen( "TEST.TXT", "w" );
    VERIFY( fp != NULL );
    if( fp != NULL ) {
        VERIFY( fputs( "hello cpm86\n", fp ) >= 0 );
        VERIFY( fprintf( fp, "num=%d end\n", 12345 ) > 0 );
        VERIFY( fwrite( "ABCDE", 1, 5, fp ) == 5 );
        VERIFY( fclose( fp ) == 0 );
    }

    /* --- read pass: reopen and read back line-by-line --- */
    fp = fopen( "TEST.TXT", "r" );
    VERIFY( fp != NULL );
    if( fp != NULL ) {
        long line2_pos;

        VERIFY( fgets( buf, sizeof( buf ), fp ) != NULL );
        VERIFY( strcmp( buf, "hello cpm86\n" ) == 0 );

        /* capture the start of line 2 via ftell (robust to text CR/LF xlate) */
        line2_pos = ftell( fp );
        VERIFY( line2_pos > 0 );

        VERIFY( fgets( buf, sizeof( buf ), fp ) != NULL );
        VERIFY( strcmp( buf, "num=12345 end\n" ) == 0 );

        /* remaining "ABCDE" via fgetc */
        VERIFY( (c = fgetc( fp )) == 'A' );
        VERIFY( (c = fgetc( fp )) == 'B' );

        /* --- seek test: rewind to 0, then back to the captured line-2 pos --- */
        VERIFY( fseek( fp, 0L, SEEK_SET ) == 0 );
        VERIFY( ftell( fp ) == 0L );
        VERIFY( fseek( fp, line2_pos, SEEK_SET ) == 0 );
        pos = ftell( fp );
        VERIFY( pos == line2_pos );
        VERIFY( fgets( buf, sizeof( buf ), fp ) != NULL );
        VERIFY( strcmp( buf, "num=12345 end\n" ) == 0 );

        VERIFY( fclose( fp ) == 0 );
    }

    /* --- append pass: reopen "a", add a line, verify total on read --- */
    fp = fopen( "TEST.TXT", "a" );
    VERIFY( fp != NULL );
    if( fp != NULL ) {
        VERIFY( fputs( "tail\n", fp ) >= 0 );
        VERIFY( fclose( fp ) == 0 );
    }

    fp = fopen( "TEST.TXT", "r" );
    VERIFY( fp != NULL );
    if( fp != NULL ) {
        int lines = 0;
        while( fgets( buf, sizeof( buf ), fp ) != NULL )
            ++lines;
        /* "ABCDE" had no newline, so appended "tail\n" merges into line 3
           ("ABCDEtail\n"); total = 3 lines. */
        VERIFY( lines == 3 );
        VERIFY( fclose( fp ) == 0 );
    }

    /* --- cleanup + remove() --- */
    VERIFY( remove( "TEST.TXT" ) == 0 );
    fp = fopen( "TEST.TXT", "r" );
    VERIFY( fp == NULL );       /* gone */

    printf( "DISKIO: %s (%d tests, %d failures)\n",
            failures == 0 ? "PASS" : "FAIL", tests, failures );
    return( failures == 0 ? 0 : 1 );
}
