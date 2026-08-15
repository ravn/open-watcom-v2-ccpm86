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

extern int os_reports_lrbc( void );     /* exact binary length only on CP/M 3+ */

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
        int  lines = 0;
        long eofpos;

        while( fgets( buf, sizeof( buf ), fp ) != NULL )
            ++lines;
        /* "ABCDE" had no newline, so appended "tail\n" merges into line 3
           ("ABCDEtail\n"); total = 3 lines. */
        VERIFY( lines == 3 );

        /* text-mode SEEK_END must land on the EXACT logical end (the Ctrl-Z
           text-EOF), i.e. exactly where sequential reading stopped -- NOT
           rounded up to the next 128-byte record. Capture the read-stop
           position, then prove SEEK_END matches it. */
        eofpos = ftell( fp );
        VERIFY( eofpos > 0 );
        VERIFY( fseek( fp, 0L, SEEK_END ) == 0 );
        VERIFY( ftell( fp ) == eofpos );

        VERIFY( fclose( fp ) == 0 );
    }

    /* --- binary absolute-seek pass: the real fseek proof. Text-mode round-trip
       via ftell only shows fseek(ftell()) is self-consistent; it does NOT show
       that seeking to a KNOWN absolute byte lands on the right byte (CR/LF
       translation muddies text offsets). So write 256 bytes whose value == index
       in binary mode (no '\r' translation, 256 == exactly two 128-byte records
       so SEEK_END is sector-aligned) and seek to hand-known positions. --- */
    fp = fopen( "BIN.DAT", "wb" );
    VERIFY( fp != NULL );
    if( fp != NULL ) {
        int i;
        for( i = 0; i < 256; i++ )
            VERIFY( fputc( i, fp ) == i );
        VERIFY( fclose( fp ) == 0 );
    }

    fp = fopen( "BIN.DAT", "rb" );
    VERIFY( fp != NULL );
    if( fp != NULL ) {
        VERIFY( fseek( fp, 200L, SEEK_SET ) == 0 );  /* absolute */
        VERIFY( ftell( fp ) == 200L );
        VERIFY( fgetc( fp ) == 200 );                /* byte 200 == 200 */

        VERIFY( fseek( fp, 50L, SEEK_SET ) == 0 );   /* backward absolute */
        VERIFY( fgetc( fp ) == 50 );                 /* now at 51 */

        VERIFY( fseek( fp, 10L, SEEK_CUR ) == 0 );   /* 51 -> 61 */
        VERIFY( ftell( fp ) == 61L );
        VERIFY( fgetc( fp ) == 61 );

        VERIFY( fseek( fp, -8L, SEEK_END ) == 0 );   /* 256 - 8 = 248 */
        VERIFY( ftell( fp ) == 248L );
        VERIFY( fgetc( fp ) == 248 );

        VERIFY( fclose( fp ) == 0 );
    }
    VERIFY( remove( "BIN.DAT" ) == 0 );

    /* --- binary NON-sector-aligned WITHIN-SESSION SEEK_END exactness. Write 200
       bytes (200 % 128 == 72, NOT a record multiple) in binary mode and, WITHOUT
       closing, demand that SEEK_END reports 200 -- not 256 (the record-rounded
       length). This is the verified-everywhere guarantee: fp->len is tracked
       exactly by every __qwrite, so seeks relative to the true end are correct
       for the whole life of the open handle, on ANY CP/M.
       What this does NOT prove: exact length after CLOSE + REOPEN of a binary
       file. That needs the OS to persist a sub-record byte count (LRBC), which
       (a) our write path does not yet transmit and (b) only CP/M 3+ / CCP/M-86
       supports -- and the authoritative oracle for it is the RC759 under MAME,
       not emu2. Tracked on KNOWN_ISSUES.md (#binary-reopen-length). --- */
    fp = fopen( "ODD.DAT", "wb" );
    VERIFY( fp != NULL );
    if( fp != NULL ) {
        int  i;
        long here;
        for( i = 0; i < 200; i++ )
            VERIFY( fputc( i & 0xFF, fp ) == (i & 0xFF) );
        here = ftell( fp );
        VERIFY( here == 200L );                      /* current position exact */
        VERIFY( fseek( fp, 0L, SEEK_END ) == 0 );
        VERIFY( ftell( fp ) == 200L );               /* end exact, not 256 */
        VERIFY( fseek( fp, -8L, SEEK_END ) == 0 );   /* 200 - 8 = 192 */
        VERIFY( ftell( fp ) == 192L );
        VERIFY( fclose( fp ) == 0 );
    }
    VERIFY( remove( "ODD.DAT" ) == 0 );
    (void)os_reports_lrbc;      /* runtime OS-capability probe (see KNOWN_ISSUES) */

    /* --- cleanup + remove() --- */
    VERIFY( remove( "TEST.TXT" ) == 0 );
    fp = fopen( "TEST.TXT", "r" );
    VERIFY( fp == NULL );       /* gone */

    printf( "DISKIO: %s (%d tests, %d failures)\n",
            failures == 0 ? "PASS" : "FAIL", tests, failures );
    return( failures == 0 ? 0 : 1 );
}
