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
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>                    /* S_IWRITE: the only chmod bit we map */
#ifdef MAME_DONE
#include "mamedone.h"                    /* mame_done(): OUT 0x2FE stop-signal */
#endif

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

    /* --- binary NON-sector-aligned CROSS-REOPEN exactness (write-side LRBC).
       Write 100 bytes (100 % 128 == 100, a partial final record), CLOSE, then
       reopen COLD and demand the fresh handle reports 100 -- proving __close
       persisted the exact byte count via the CP/M 3 F_ATTRIB/F6' byte-count
       protocol so a *new* program sees the true size, not the 128-rounded
       record count. This is only guaranteed where the OS actually supports the
       LRBC (CP/M 3+ / Concurrent CP/M-86), so it is gated on os_reports_lrbc():
       on the RC759 (CCP/M-86 3.1) it runs and must pass; on plain CP/M-86 2.2 it
       is skipped (no LRBC). Authoritative oracle: RC759 under MAME. --- */
    if( os_reports_lrbc() ) {
        fp = fopen( "EXACT.DAT", "wb" );
        VERIFY( fp != NULL );
        if( fp != NULL ) {
            int i;
            for( i = 0; i < 100; i++ )
                VERIFY( fputc( 'A' + ( i % 26 ), fp ) == ( 'A' + ( i % 26 ) ) );
            VERIFY( fclose( fp ) == 0 );
        }
        fp = fopen( "EXACT.DAT", "rb" );             /* COLD reopen */
        VERIFY( fp != NULL );
        if( fp != NULL ) {
            VERIFY( fseek( fp, 0L, SEEK_END ) == 0 );
            VERIFY( ftell( fp ) == 100L );           /* exact, not 128 */
            VERIFY( fclose( fp ) == 0 );
        }
        VERIFY( remove( "EXACT.DAT" ) == 0 );
    }
    (void)os_reports_lrbc;      /* runtime OS-capability probe (see KNOWN_ISSUES) */

    /* --- low-level POSIX I/O seam (open/creat/read/write/close/lseek/tell/
       filelength/eof) + rename. All lengths here are asserted WITHIN a single
       open handle, where fp->len is byte-exact on ANY CP/M (the record-rounding
       limit only bites a binary file reopened cold -- KNOWN_ISSUES #1). --- */
    {
        static const char msg[] = "lowlevel-seam 0123456789";
        char rbuf[40];
        int  h;
        int  L = (int)sizeof( msg ) - 1;             /* 24 bytes, not record-aligned */

        h = open( "LOWA.DAT", O_RDWR | O_CREAT | O_TRUNC | O_BINARY );
        VERIFY( h >= 0 );
        if( h >= 0 ) {
            VERIFY( write( h, msg, L ) == L );
            VERIFY( filelength( h ) == (long)L );    /* byte-exact this session */
            VERIFY( tell( h ) == (long)L );          /* position after write */
            VERIFY( eof( h ) == 1 );                 /* at end */
            VERIFY( lseek( h, 0L, SEEK_SET ) == 0L );
            VERIFY( eof( h ) == 0 );                 /* no longer at end */
            VERIFY( read( h, rbuf, L ) == L );
            VERIFY( memcmp( rbuf, msg, L ) == 0 );   /* round-trip intact */
            VERIFY( eof( h ) == 1 );                 /* consumed to end again */
            VERIFY( lseek( h, 10L, SEEK_SET ) == 10L );
            VERIFY( tell( h ) == 10L );
            VERIFY( read( h, rbuf, 5 ) == 5 );
            VERIFY( memcmp( rbuf, msg + 10, 5 ) == 0 );
            VERIFY( close( h ) == 0 );
        }

        /* rename LOWA.DAT -> LOWB.DAT: old name must vanish, data must survive */
        VERIFY( rename( "LOWA.DAT", "LOWB.DAT" ) == 0 );
        VERIFY( open( "LOWA.DAT", O_RDONLY | O_BINARY ) == -1 );  /* old gone */
        h = open( "LOWB.DAT", O_RDONLY | O_BINARY );
        VERIFY( h >= 0 );
        if( h >= 0 ) {
            VERIFY( read( h, rbuf, L ) == L );
            VERIFY( memcmp( rbuf, msg, L ) == 0 );   /* renamed data intact */
            VERIFY( close( h ) == 0 );
        }
        VERIFY( rename( "NOSUCH.DAT", "X.DAT" ) == -1 );  /* missing source fails */
        VERIFY( remove( "LOWB.DAT" ) == 0 );
    }

    /* --- chmod: on CP/M-86 the ONLY writability attribute is the read-only
       (R/O) bit, so chmod maps ONLY S_IWRITE -> clear R/O, !S_IWRITE -> set R/O;
       all other mode bits are ignored. This checks the seam is wired to F_ATTRIB
       and maps errors: chmod succeeds on an existing file both ways, fails
       (ENOENT) on a missing one, and never corrupts the file's data. The actual
       enforcement of R/O (a blocked write/delete) is OS-policy that differs
       between emu2 and real CP/M, so it is not hard-asserted here. --- */
    {
        static const char cmsg[] = "chmod-roundtrip";
        char  cbuf[24];
        int   L = (int)sizeof( cmsg ) - 1;
        int   h;

        h = open( "CHM.DAT", O_WRONLY | O_CREAT | O_TRUNC | O_BINARY );
        VERIFY( h >= 0 );
        if( h >= 0 ) {
            VERIFY( write( h, cmsg, L ) == L );
            VERIFY( close( h ) == 0 );
        }
        VERIFY( chmod( "CHM.DAT", S_IREAD ) == 0 );          /* make R/O          */
        VERIFY( chmod( "CHM.DAT", S_IREAD | S_IWRITE ) == 0 );/* back to R/W       */
        VERIFY( chmod( "NOSUCH.DAT", S_IWRITE ) == -1 );     /* missing -> ENOENT  */

        h = open( "CHM.DAT", O_RDONLY | O_BINARY );          /* data must survive  */
        VERIFY( h >= 0 );
        if( h >= 0 ) {
            VERIFY( read( h, cbuf, L ) == L );
            VERIFY( memcmp( cbuf, cmsg, L ) == 0 );
            VERIFY( close( h ) == 0 );
        }
        VERIFY( remove( "CHM.DAT" ) == 0 );
    }

    /* --- tmpnam / tmpfile: unique name + write/rewind/read round-trip, then
       auto-removal on fclose (Watcom fclose fires __RmTmpFileFn on _TMPFIL). --- */
    {
        char  nm1[L_tmpnam];
        char  nm2[L_tmpnam];
        static const char tmsg[] = "tmpfile-roundtrip";
        char  tbuf[24];
        FILE *tf;
        int   L = (int)sizeof( tmsg ) - 1;

        VERIFY( tmpnam( nm1 ) == nm1 );          /* returns the caller buffer */
        VERIFY( tmpnam( nm2 ) == nm2 );
        VERIFY( strcmp( nm1, nm2 ) != 0 );       /* successive names differ */
        VERIFY( open( nm1, O_RDONLY ) == -1 );   /* tmpnam does not create */

        tf = tmpfile();
        VERIFY( tf != NULL );
        if( tf != NULL ) {
            VERIFY( fwrite( tmsg, 1, L, tf ) == (size_t)L );
            rewind( tf );
            VERIFY( fread( tbuf, 1, L, tf ) == (size_t)L );
            VERIFY( memcmp( tbuf, tmsg, L ) == 0 );  /* data survives round-trip */
            VERIFY( fclose( tf ) == 0 );             /* auto-removes the temp */
        }
    }

#ifdef FSCANF_TEST
    /* --- fscanf read path (Watcom's UNCHANGED scnf.c engine): write known
       text, parse it back. Integer + string conversions only; scan_float is
       linked (unconditional in scnf.c) but not exercised here. Gated behind
       FSCANF_TEST because it drags the soft-float/ctype/mbyte stack -- only the
       dedicated build-fscanf.sh harness defines it; build-diskio stays lean. --- */
    {
        FILE *sf;
        int   a = 0, b = 0, n;
        char  word[16];

        sf = fopen( "SCAN.DAT", "w" );
        VERIFY( sf != NULL );
        if( sf != NULL ) {
            VERIFY( fputs( "123 -456 hello", sf ) >= 0 );
            VERIFY( fclose( sf ) == 0 );
        }
        sf = fopen( "SCAN.DAT", "r" );
        VERIFY( sf != NULL );
        if( sf != NULL ) {
            n = fscanf( sf, "%d %d %15s", &a, &b, word );
            VERIFY( n == 3 );                    /* three fields converted */
            VERIFY( a == 123 );
            VERIFY( b == -456 );                 /* signed decimal */
            VERIFY( strcmp( word, "hello" ) == 0 );
            VERIFY( fscanf( sf, "%d", &a ) == EOF );  /* nothing left */
            VERIFY( fclose( sf ) == 0 );
        }
        VERIFY( remove( "SCAN.DAT" ) == 0 );
    }
#endif

    /* --- cleanup + remove() --- */
    VERIFY( remove( "TEST.TXT" ) == 0 );
    fp = fopen( "TEST.TXT", "r" );
    VERIFY( fp == NULL );       /* gone */

    printf( "DISKIO: %s (%d tests, %d failures)\n",
            failures == 0 ? "PASS" : "FAIL", tests, failures );
#ifdef MAME_DONE
    /* Stream the result record to the MAME host as a word sequence (tag, full
       16-bit test count, failures, end sentinel). disk_done.lua collects them,
       snapshots the screen (the printed DISKIO line above is the human oracle),
       and stops the emulator. Must be last -- output is already flushed. */
    mame_out( 0xD15C );                 /* tag: "disk" result record */
    mame_out( (unsigned)tests );        /* full count -- 511 does not fit a byte */
    mame_out( (unsigned)failures );     /* 0 == PASS */
    mame_out( 0xE0F0 );                 /* end sentinel */
#endif
    return( failures == 0 ? 0 : 1 );
}
