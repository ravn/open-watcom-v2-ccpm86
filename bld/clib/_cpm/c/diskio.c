/* diskio.c -- CP/M-86 disk FILE* seam for Open Watcom's GENUINE stdio layer.
 *
 * rc7xx-work#7 milestone 3, remaining half: lift the console-only write seam
 * (port/stdioshim.c) to a full disk FILE* path so fopen/fread/fwrite/fprintf/
 * fgets/fseek/ftell against real CP/M-86 disk files work through Watcom's
 * UNCHANGED stdio. This file SUPERSEDES stdioshim.c in the disk build: it owns
 * the same console __qwrite + isatty seam AND adds the five low-level primitives
 * fopen bottoms out into -- _sopen / __qread / __qwrite / __close / __lseek --
 * backed by CP/M-86 FCB BDOS calls (INT 0E0h). No DOS INT 21h anywhere.
 *
 * CP/M-86 record model (why this is not just "read()/write()"): CP/M has no
 * byte-granular file length -- storage is 128-byte records only. We use the
 * RANDOM-record BDOS calls (READ RANDOM fn 33 / WRITE RANDOM fn 34), which makes
 * byte position trivial: record = pos>>7, in-record offset = pos&127. A record
 * that has never been written reads back as EOF; we then fill the work buffer
 * with Ctrl-Z (0x1A), so a partially-written last record keeps a Ctrl-Z tail on
 * disk -- exactly CP/M's text-EOF convention, produced for free. On read, text
 * mode stops at the first Ctrl-Z; binary mode does not (a binary file's length
 * is only known to the nearest 128 bytes -- an inherent CP/M limitation, so
 * binary callers must track their own length).
 *
 * Text/binary '\n' <-> "\r\n" translation is done ABOVE this seam by Watcom's
 * text-mode fgetc/fputc (driven by the FILE flag fopen sets from the "t"/"b"
 * mode), so __qread/__qwrite move RAW bytes -- same boundary as the console
 * seam. We additionally enforce the Ctrl-Z text-EOF here because the record
 * model, not a byte count, delimits the file.
 */

#include "variety.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>       /* chmod/stat prototypes, mode_t, S_IWRITE (R/O attr) */
#include <utime.h>          /* struct utimbuf (utime) */
#include "qread.h"
#include "qwrite.h"
#include <stdlib.h>         /* exit() prototype (clib -we: no implicit decls) */

/* Forward prototypes for this seam's own public entry points (clib -we forbids
   defining a non-static function with no prior declaration). */
int  os_reports_lrbc( void );
long _WCNEAR __lseek( int handle, long offset, int origin );
int  _WCNEAR __close( int handle );
int  __apply_redirection( int argc, char **argv );  /* command-tail redirection */
void __close_redirection( void );

/* ---- CP/M-86 BDOS gateway ------------------------------------------------ */
/* INT 0E0h: function in CL, parameter (near offset, or a segment for fn 51) in
   DX, result byte returned in AL. Small model => DS is the one data group, so a
   near &object is the DMA/FCB offset the BDOS wants. */
extern unsigned char _bdos( unsigned char fn, unsigned param );
#pragma aux _bdos =             \
    "int 0E0h"                  \
    __parm [__cl] [__dx]              \
    __value [__al]                  \
    __modify [__ax __bx __cx __dx __es];

extern unsigned _getds( void );
#pragma aux _getds =            \
    "mov ax,ds"                 \
    __value [__ax]                  \
    __modify [__ax];

extern void _bdos_conout( int c );      /* BDOS C_WRITE (fn 2, char in DL) */
#pragma aux _bdos_conout =      \
    "mov cl,2"                  \
    "int 0E0h"                  \
    __parm [__dx]                   \
    __modify [__ax __bx __cx __es];

/* BDOS function numbers we use. */
#define BD_VERSION  12          /* S_BDOSVER: OS/BDOS version (runtime capability) */
#define BD_OPEN     15
#define BD_CLOSE    16
#define BD_DELETE   19
#define BD_RENAME   23          /* F_RENAME: old FCB in 0..15, new name in 16..31 */
#define BD_ATTRIB   30          /* F_ATTRIB: set attrs; F6' writes last-rec byte count */
#define BD_SFIRST   17          /* F_SFIRST: search dir for first match (no lock) */
#define BD_MAKE     22
#define BD_SETDMA   26
#define BD_READRAND 33
#define BD_WRITERND 34
#define BD_FILESIZE 35
#define BD_SETDMASEG 51

#define SECT        128         /* CP/M record size */
#define CPM_EOF     0x1A        /* Ctrl-Z: text end-of-file marker */

/* FCB byte offsets (36-byte CP/M FCB). */
#define FCB_DRIVE   0
#define FCB_NAME    1           /* 8 bytes */
#define FCB_TYPE    9           /* 3 bytes */
#define FCB_EX      12
#define FCB_S1      13
#define FCB_S2      14
#define FCB_RC      15
#define FCB_CR      32
#define FCB_LRBC    32          /* CP/M 3+ Last Record Byte Count shares FCB+32 */
#define FCB_R0      33          /* random record number, 3 bytes (little-endian) */

/* ---- open-file table ----------------------------------------------------- */
#define DISK_FIRST_FD 3         /* 0/1/2 reserved for stdin/stdout/stderr */
#define DISK_MAX      16        /* iotest opens NUM_FILES=10 tmpfiles at once;
                                   keep DISK_FIRST_FD+DISK_MAX <= __NFiles(20) so
                                   every handle fits the iomode table */

typedef struct {
    unsigned char fcb[36];
    long          pos;          /* current byte position */
    long          len;          /* exact logical length, tracked LOCALLY (the
                                   CP/M directory only knows length to the
                                   nearest 128-byte record, so we must remember
                                   the true end ourselves while the file is open) */
    unsigned char used;
    unsigned char text;         /* 1 = stop reads at Ctrl-Z */
    unsigned char readable;
    unsigned char writable;
    unsigned char append;       /* 1 = O_APPEND: every write repositions to
                                   fp->len first, so a preceding fseek()/rewind()
                                   cannot divert an append away from EOF (C/POSIX
                                   append semantics; iotest.c "a+" relies on it) */
    unsigned char wrote;        /* 1 = this handle has written => fp->len is the
                                   authoritative byte-exact length (used for EOF).
                                   0 = pure reader => re-derive length from disk on
                                   demand so it sees data another handle flushed
                                   (iotest.c "flushes": one "w" + one "r" handle on
                                   the same file, reader must see the flushed byte) */
    unsigned char ateof;
    unsigned char is_con;       /* 1 = console device (fopen("CON")): read/write
                                   route to the BDOS console, not to a disk FCB */
    unsigned char open_lrbc;    /* LRBC byte captured at open (0xFF = OS gave
                                   none => no exact length available) */
} dfile_t;

static dfile_t        dfiles[DISK_MAX];
static unsigned char  dma[SECT];        /* our 128-byte DMA / work buffer */
static dfile_t       *cache_fp;         /* which file's record is in dma */
static long           cache_rec;        /* which record is in dma (valid iff cache_fp) */

/* ---- stdin/stdout redirection (shell-style  < file  > file  >> file) -------
   CP/M's CCP has no I/O redirection, so a hosted program does it ITSELF: after
   the crt0 command-tail parser has split the tail into argv, __apply_redirection
   scans argv for the redirect operators, opens each named file as an ordinary
   disk file (fd >= DISK_FIRST_FD), and records that fd here. __qread/__qwrite --
   the single seam the whole FILE* stack (scanf/getchar/printf/fwrite/...) bottoms
   out in -- then transparently reroute the console handles onto that fd, so the
   ENTIRE stdin/stdout stream lands on the file with ZERO FILE* surgery: the
   stdin/stdout FILE objects keep handles 0/1 and their buffering untouched.

   stderr (handle 2) is deliberately NOT rerouted by `>`, matching Unix shells:
   `prog > out.txt` still lets diagnostics reach the console. */
static int redir_in  = -1;      /* disk fd feeding stdin(0),  or -1 = console */
static int redir_out = -1;      /* disk fd draining stdout(1), or -1 = console */

static dfile_t *fd_to_file( int handle )
{
    int i = handle - DISK_FIRST_FD;
    if( i < 0 || i >= DISK_MAX || !dfiles[i].used )
        return( NULL );
    return( &dfiles[i] );
}

/* ---- FCB helpers --------------------------------------------------------- */

/* Point the BDOS DMA at our work buffer (offset AND segment, so we do not rely
   on the load-time default DMA base still being live). */
static void set_dma( void )
{
    _bdos( BD_SETDMASEG, _getds() );
    _bdos( BD_SETDMA, (unsigned)(size_t)&dma[0] );
}

/* Runtime OS capability probe: does this system expose an exact byte length?
   BDOS fn 12 (S_BDOSVER) returns the version in AL; CP/M 3.0+ (0x30) and the
   RC759's Concurrent CP/M-86 3.1 (0x31) carry the Last Record Byte Count (LRBC),
   plain CP/M-86 (a CP/M-2.2 filesystem, 0x2x) does not. This is decided AT
   RUNTIME -- the same binary runs on both and picks the exact-length path only
   where the OS actually supports it. Cached: probed once, reused thereafter.

   NOTE: the LRBC decode below is smoke-tested under emu2 only; emu2 is NOT the
   authoritative oracle for LRBC semantics -- the RC759 running real Concurrent
   CP/M-86 under MAME is. Until a MAME run confirms it, binary exact length on
   CP/M 3+ is UNVERIFIED (see KNOWN_ISSUES.md). Local write-tracking (fp->len)
   is the verified-everywhere behaviour and remains the safety net. */
static int os_has_lrbc( void )
{
    static signed char cached = -1;     /* -1 = unprobed, 0 = no, 1 = yes */
    if( cached < 0 )
        cached = (signed char)(( _bdos( BD_VERSION, 0 ) & 0xFF ) >= 0x30);
    return( cached );
}

/* Public probe so the test oracle can gate its LRBC-exact-length expectation on
   the actual OS: exact binary length is only guaranteed on CP/M 3+ (CCP/M-86). */
int os_reports_lrbc( void )
{
    return( os_has_lrbc() );
}

/* Store a 0-based record number into the FCB random-record field (r0,r1,r2). */
static void fcb_set_record( unsigned char *fcb, long rec )
{
    fcb[FCB_R0 + 0] = (unsigned char)(rec & 0xFF);
    fcb[FCB_R0 + 1] = (unsigned char)((rec >> 8) & 0xFF);
    fcb[FCB_R0 + 2] = (unsigned char)((rec >> 16) & 0xFF);
}

/* Parse "[d:]NAME.EXT" into a fresh FCB. Returns 0 on success, -1 on a name
   that cannot be a CP/M filename. Uppercases; space-pads name(8)/type(3). */
static int name_to_fcb( const char *name, unsigned char *fcb )
{
    int i;
    const char *p = name;

    for( i = 0; i < 36; i++ )
        fcb[i] = 0;
    for( i = FCB_NAME; i < FCB_NAME + 11; i++ )     /* name+type = spaces */
        fcb[i] = ' ';

    if( p[0] != '\0' && p[1] == ':' ) {             /* drive letter */
        char d = p[0];
        if( d >= 'a' && d <= 'z' )
            d = (char)(d - 0x20);
        if( d < 'A' || d > 'P' )
            return( -1 );
        fcb[FCB_DRIVE] = (unsigned char)(d - 'A' + 1);
        p += 2;
    }

    i = 0;                                          /* base name, up to 8 */
    while( *p != '\0' && *p != '.' ) {
        char c = *p++;
        if( c >= 'a' && c <= 'z' )
            c = (char)(c - 0x20);
        if( i < 8 )
            fcb[FCB_NAME + i] = (unsigned char)c;
        i++;
    }
    if( i == 0 )
        return( -1 );

    if( *p == '.' ) {                               /* extension, up to 3 */
        p++;
        i = 0;
        while( *p != '\0' ) {
            char c = *p++;
            if( c >= 'a' && c <= 'z' )
                c = (char)(c - 0x20);
            if( i < 3 )
                fcb[FCB_TYPE + i] = (unsigned char)c;
            i++;
        }
    }
    return( 0 );
}

/* Load the file's record `rec` into dma via READ RANDOM. Returns 0 if real data
   was read, 1 at/after EOF (dma filled with Ctrl-Z), -1 on a BDOS error. A tiny
   one-record cache avoids re-reading the record a partial op is sitting on. */
static int load_record( dfile_t *fp, long rec )
{
    unsigned char rc;

    if( cache_fp == fp && cache_rec == rec )
        return( 0 );
    fcb_set_record( fp->fcb, rec );
    set_dma();
    rc = _bdos( BD_READRAND, (unsigned)(size_t)&fp->fcb[0] );
    if( rc == 1 || rc == 4 ) {          /* 1 = reading unwritten data, 4 = past EOF */
        memset( dma, CPM_EOF, SECT );
        cache_fp = NULL;
        return( 1 );
    }
    if( rc != 0 ) {
        cache_fp = NULL;
        return( -1 );
    }
    cache_fp = fp;
    cache_rec = rec;
    return( 0 );
}

/* Compute the true text end-of-file byte position for O_APPEND. FILESIZE (fn 35)
   sets the FCB random record to the file's size in 128-byte records; we read the
   last record and scan back past its Ctrl-Z padding to the real text length. */
static long text_eof( dfile_t *fp )
{
    long  records;
    long  last;
    int   i;

    set_dma();
    _bdos( BD_FILESIZE, (unsigned)(size_t)&fp->fcb[0] );
    records = (long)fp->fcb[FCB_R0 + 0]
            | ((long)fp->fcb[FCB_R0 + 1] << 8)
            | ((long)fp->fcb[FCB_R0 + 2] << 16);
    if( records == 0 )
        return( 0 );
    last = records - 1;
    if( load_record( fp, last ) != 0 )              /* EOF/err => whole records */
        return( records * SECT );
    for( i = SECT; i > 0; i-- )                     /* trim trailing Ctrl-Z */
        if( dma[i - 1] != CPM_EOF )
            break;
    return( last * SECT + i );
}

/* Best-effort exact length of an already-open file, straight off the disk. This
   is the SEED for fp->len at open time; writes thereafter update fp->len
   exactly (see __qwrite). Text: text_eof() is byte-exact (Ctrl-Z scan). Binary:
   the CP/M directory has no sub-record length, so this rounds UP to the next
   128-byte sector -- the inherent CP/M-2.2 limit (tracked on the known-issues
   list). */
static long disk_len( dfile_t *fp )
{
    long records;

    if( fp->text )
        return( text_eof( fp ) );
    set_dma();
    _bdos( BD_FILESIZE, (unsigned)(size_t)&fp->fcb[0] );
    records = (long)fp->fcb[FCB_R0 + 0]
            | ((long)fp->fcb[FCB_R0 + 1] << 8)
            | ((long)fp->fcb[FCB_R0 + 2] << 16);
    if( records == 0 )
        return( 0 );
    /* Exact byte length on CP/M 3+ (CCP/M-86) via the Last Record Byte Count the
       OS wrote into FCB+32 at open. LRBC = bytes used in the file's final
       128-byte record; 0 means the last record is full. So:
           len = (records-1)*128 + (lrbc==0 ? 128 : lrbc)
       0xFF means no LRBC was supplied (plain CP/M-86 / 2.2, or the file was
       stored without one) -- fall back to the record-rounded length.

       IMPORTANT GAP: this only recovers an exact length that some program
       PERSISTED. Our own write path (see __qwrite/__close) writes whole 128-byte
       records and does NOT yet transmit an LRBC on close, so a binary file WE
       wrote reads back record-rounded even here -- only files created by a tool
       that recorded the LRBC come back byte-exact. Cross-reopen exact length for
       our own binary output is UNVERIFIED and needs both a write-side LRBC
       protocol and a MAME/RC759 oracle (emu2 is not authoritative). Within a
       single open handle, fp->len is always byte-exact (tracked by __qwrite).
       Tracked on KNOWN_ISSUES.md. */
    if( os_has_lrbc() && fp->open_lrbc != 0xFF ) {
        long lrbc = fp->open_lrbc;
        return( (records - 1) * SECT + (lrbc == 0 ? SECT : lrbc) );
    }
    return( records * SECT );
}

/* ---- the five stdio low-level primitives --------------------------------- */

/* Per-handle iomode table (Watcom handleio/c/iomode.c + stiomode.c, both pure C,
   no INT 21h). fdopen()/freopen()/dup() consult it to learn a handle's
   read/write/text/append flags, so _sopen MUST register every handle it opens.
   Handles 0/1/2 are pre-seeded by iomode.c's __init_mode (stdin=_READ,
   stdout/stderr=_WRITE); we register slots DISK_FIRST_FD.. as we open them.

   Only the streamio harness exercises fdopen()/dup() and thus links the iomode
   objects; the diskio/fscanf harnesses do not. OMF resolves EVERY external ref
   in a linked object (even from unreferenced functions such as dup()), so gate
   the iomode calls behind DISKIO_IOMODE -- defined only by build-streamio.sh --
   to keep this shared diskio.obj linkable in the leaner harnesses. */
#ifdef DISKIO_IOMODE
extern int _WCNEAR __SetIOMode_grow( int handle, unsigned value );
extern unsigned _WCNEAR __GetIOMode( int handle );
#define REGISTER_IOMODE( h, v )     ((void)__SetIOMode_grow( (h), (v) ))
#define QUERY_IOMODE( h )           __GetIOMode( (h) )

/* Translate an open() access mode into the iomode flag word fdopen() expects
   (values from <stdio.h>: _READ 0x1, _WRITE 0x2, _BINARY 0x40, _APPEND 0x80). */
static unsigned iomode_flags_of( int mode )
{
    int      acc = mode & (O_RDONLY | O_WRONLY | O_RDWR);
    unsigned f = 0;

    if( acc == O_RDONLY || acc == O_RDWR )
        f |= _READ;
    if( acc == O_WRONLY || acc == O_RDWR )
        f |= _WRITE;
    if( mode & O_BINARY )
        f |= _BINARY;
    if( mode & O_APPEND )
        f |= _APPEND;
    return( f );
}
#else  /* !DISKIO_IOMODE: harness links no iomode table */
#define REGISTER_IOMODE( h, v )     ((void)0)
#define QUERY_IOMODE( h )           (0u)
#endif

/* CP/M's console is reached by the reserved name "CON" (and, so the UNCHANGED
   Watcom clibtest that uses the NT CONIN$/CONOUT$ spellings also links, those).
   A stream opened on it is a device: reads/writes route to the BDOS console, not
   to a disk FCB. This is the streamio/iotest.c very first requirement --
   con = fopen("CON","w") -- and freopen(CONSOLE_IN,...) onto std streams. */
static int is_console_name( const char *name )
{
    static const char *const devs[] = { "CON", "CONIN$", "CONOUT$" };
    int d;

    for( d = 0; d < 3; d++ ) {
        const char *a = name, *b = devs[d];
        for( ;; ) {
            int ca = *a, cb = *b;
            if( ca >= 'a' && ca <= 'z' ) ca -= 'a' - 'A';   /* fold to upper */
            if( ca != cb )
                break;
            if( ca == 0 )
                return( 1 );                                /* full match */
            a++;
            b++;
        }
    }
    return( 0 );
}

/* fopen -> _sopen: allocate a slot, open/create the CP/M file, return an fd. The
   pmode (permission) vararg is unused on CP/M-86 (no per-file mode bits). */
_WCRTLINK int _sopen( const char *name, int mode, int shflag, ... )
{
    int      i;
    int      acc;
    dfile_t *fp;

    (void)shflag;
    for( i = 0; i < DISK_MAX; i++ )
        if( !dfiles[i].used )
            break;
    if( i >= DISK_MAX )
        return( -1 );                               /* too many open files */
    fp = &dfiles[i];

    /* Console device (fopen("CON")): no FCB, no BDOS OPEN/MAKE -- just mark it a
       console and route __qread/__qwrite to the BDOS console. It always "opens";
       reads return EOF (the streamio test never reads bytes back from CON, it
       redirects stdin onto a real file first), writes go to C_WRITE. */
    if( is_console_name( name ) ) {
        acc = mode & (O_RDONLY | O_WRONLY | O_RDWR);
        fp->is_con   = 1;
        fp->readable = (unsigned char)(acc == O_RDONLY || acc == O_RDWR);
        fp->writable = (unsigned char)(acc == O_WRONLY || acc == O_RDWR);
        fp->append   = 0;
        fp->text     = 1;
        fp->wrote    = 0;
        fp->pos      = 0;
        fp->len      = 0;
        fp->ateof    = 0;
        fp->open_lrbc = 0xFF;
        fp->used     = 1;
        REGISTER_IOMODE( DISK_FIRST_FD + i, iomode_flags_of( mode ) | _ISTTY );
        return( DISK_FIRST_FD + i );
    }

    if( name_to_fcb( name, fp->fcb ) < 0 )
        return( -1 );

    fp->is_con   = 0;
    acc = mode & (O_RDONLY | O_WRONLY | O_RDWR);
    fp->readable = (unsigned char)(acc == O_RDONLY || acc == O_RDWR);
    fp->writable = (unsigned char)(acc == O_WRONLY || acc == O_RDWR);
    fp->append   = (unsigned char)((mode & O_APPEND) != 0);
    fp->text     = (unsigned char)((mode & O_BINARY) == 0);
    fp->wrote    = 0;
    fp->pos      = 0;
    fp->len      = 0;
    fp->ateof    = 0;
    fp->open_lrbc = 0xFF;                           /* assume: no LRBC from OS */

    /* Request the exact byte length from the OS at open time. On CP/M 3+
       (CCP/M-86) the caller signals interest by pre-setting FCB+32 to 0xFF; the
       BDOS OPEN then replaces it with the Last Record Byte Count. Plain CP/M-86
       (2.2) leaves it untouched, so it stays 0xFF and we fall back to the
       record-rounded length. Only meaningful for binary files (text files use
       text_eof's byte-exact Ctrl-Z scan regardless of OS). */
    if( os_has_lrbc() && !fp->text )
        fp->fcb[FCB_LRBC] = 0xFF;

    if( mode & O_TRUNC ) {
        _bdos( BD_DELETE, (unsigned)(size_t)&fp->fcb[0] );
        if( _bdos( BD_MAKE, (unsigned)(size_t)&fp->fcb[0] ) == 0xFF )
            return( -1 );
        /* fresh/empty file: exact length is 0 */
    } else if( _bdos( BD_OPEN, (unsigned)(size_t)&fp->fcb[0] ) == 0xFF ) {
        if( !(mode & O_CREAT) )
            return( -1 );
        if( _bdos( BD_MAKE, (unsigned)(size_t)&fp->fcb[0] ) == 0xFF )
            return( -1 );
        /* fresh/empty file: exact length is 0 */
    } else {
        /* opened an EXISTING file. Capture the LRBC the OS just wrote into FCB+32
           (0xFF still means "none supplied"), then clear FCB+32 so it does not
           disturb the random-record I/O that follows. Seed the exact length from
           disk: text_eof() is byte-exact (Ctrl-Z scan); binary uses the LRBC on
           CP/M 3+ for byte-exact length, else rounds UP to a 128-byte sector --
           the inherent CP/M-2.2 limit. Writes thereafter track fp->len exactly. */
        fp->open_lrbc = fp->fcb[FCB_LRBC];
        fp->fcb[FCB_CR] = 0;
        fp->len = disk_len( fp );
    }

    fp->used = 1;
    if( mode & O_APPEND )
        fp->pos = fp->len;
    REGISTER_IOMODE( DISK_FIRST_FD + i, iomode_flags_of( mode ) );
    return( DISK_FIRST_FD + i );
}

/* FILE fill path (fgetc/fread) -> __qread. Returns bytes read, 0 at EOF. */
int _WCNEAR __qread( int handle, void *buffer, unsigned len )
{
    dfile_t       *fp;
    unsigned char *out = (unsigned char *)buffer;
    unsigned       total = 0;

    if( handle < DISK_FIRST_FD ) {                  /* console handle (0/1/2) */
        if( redir_in >= 0 )                         /* stdin redirected -> disk */
            handle = redir_in;                      /*   fall through to the file */
        else
            return( 0 );                            /* console: no stdin here */
    }
    fp = fd_to_file( handle );
    if( fp == NULL || !fp->readable )
        return( 0 );
    /* NOTE: do NOT short-circuit on a latched fp->ateof here. Watcom's fgetc()
       re-calls __qread after a previous 0-byte (EOF) read (it gates on _cnt, not
       a sticky EOF flag), and iotest.c "flushes" depends on that retry seeing a
       byte another handle just fflush()ed. The read loop below re-derives EOF
       from disk for a pure reader on every call, so a stale ateof cannot wedge
       the stream shut. */
    if( fp->is_con )                                /* CON device: no byte input */
        return( 0 );
    fp->ateof = 0;                                  /* recomputed below each call */

    while( total < len ) {
        long     rec = fp->pos >> 7;
        unsigned off = (unsigned)(fp->pos & 127);
        unsigned avail = SECT - off;
        unsigned n = len - total;
        unsigned k;

        /* Byte-exact EOF. For a handle that has WRITTEN (fp->wrote), fp->len is
           the authoritative logical length -- iotest.c "r+b" writes 101 bytes
           then fsetpos()es to offset 101 (inside record 0, bytes 0..127) and
           asserts fgetc()==EOF; without the exact len we would hand back the
           Ctrl-Z padding at dma[101]. For a PURE READER, fp->len was only the
           snapshot taken at open, so re-derive it from disk on demand -- this is
           how iotest.c "flushes" sees a byte another handle just fflush()ed
           (one "w" handle + one "r" handle on the same file). */
        if( fp->pos >= fp->len ) {
            if( !fp->wrote ) {
                long dl = disk_len( fp );           /* re-read length off disk */
                if( dl > fp->len )
                    fp->len = dl;
            }
            if( fp->pos >= fp->len ) {
                fp->ateof = 1;
                break;
            }
        }
        if( fp->len - fp->pos < (long)avail )       /* don't cross the exact end */
            avail = (unsigned)( fp->len - fp->pos );

        if( load_record( fp, rec ) != 0 ) {         /* no data at/after here */
            fp->ateof = 1;
            break;
        }
        if( n > avail )
            n = avail;
        if( fp->text ) {                            /* stop at first Ctrl-Z */
            for( k = 0; k < n; k++ ) {
                if( dma[off + k] == CPM_EOF ) {
                    fp->ateof = 1;
                    break;
                }
            }
            n = k;                                  /* bytes before the Ctrl-Z */
        }
        if( n == 0 )
            break;
        memcpy( out + total, &dma[off], n );
        total += n;
        fp->pos += n;
        if( fp->ateof )
            break;
    }
    return( (int)total );
}

/* FILE flush path (flush/fwrite) -> __qwrite. Console handles 1/2 go to the CP/M
   console via BDOS C_WRITE (verbatim: text-mode fputc already made "\r\n" in the
   FILE buffer). Disk handles do a read-modify-write of each 128-byte record via
   WRITE RANDOM, so an untouched record tail keeps its Ctrl-Z EOF marker. */
int _WCNEAR __qwrite( int handle, const void *buffer, unsigned len )
{
    const unsigned char *in = (const unsigned char *)buffer;
    dfile_t             *fp;
    unsigned             total = 0;

    if( handle == STDOUT_FILENO && redir_out >= 0 ) {
        handle = redir_out;                         /* stdout redirected -> disk */
    } else if( handle == STDOUT_FILENO || handle == STDERR_FILENO ) {
        unsigned i;
        for( i = 0; i < len; i++ )
            _bdos_conout( in[i] );                  /* console (stderr stays here) */
        return( (int)len );
    }

    fp = fd_to_file( handle );
    if( fp == NULL || !fp->writable )
        return( -1 );
    if( fp->is_con ) {                              /* fopen("CON")/freopen -> console */
        unsigned i;
        for( i = 0; i < len; i++ )
            _bdos_conout( in[i] );
        return( (int)len );
    }

    if( fp->append )                                /* C append: divert to EOF */
        fp->pos = fp->len;
    fp->wrote = 1;                                  /* len is now authoritative */

    while( total < len ) {
        long     rec = fp->pos >> 7;
        unsigned off = (unsigned)(fp->pos & 127);
        unsigned avail = SECT - off;
        unsigned n = len - total;

        if( n > avail )
            n = avail;
        if( load_record( fp, rec ) < 0 )            /* fills Ctrl-Z if new */
            break;
        memcpy( &dma[off], in + total, n );
        fcb_set_record( fp->fcb, rec );
        set_dma();
        if( _bdos( BD_WRITERND, (unsigned)(size_t)&fp->fcb[0] ) != 0 )
            break;                                  /* disk full / error */
        cache_fp = fp;                              /* dma still holds this record */
        cache_rec = rec;
        total += n;
        fp->pos += n;
        if( fp->pos > fp->len )                     /* extend the exact length */
            fp->len = fp->pos;
    }
    if( total == 0 && len != 0 )
        return( -1 );
    return( (int)total );
}

/* fseek/ftell -> __lseek. Byte-granular thanks to the random-record model.
   SEEK_END returns fp->len: byte-exact for text and for anything written this
   session (fp->len is tracked exactly by __qwrite). A binary file reopened
   read-only inherits its seed from disk_len(): exact only if a prior program
   persisted an LRBC on CP/M 3+, otherwise sector-rounded (128-byte-record
   limit). See KNOWN_ISSUES.md for the binary-reopen-length gap. */
long _WCNEAR __lseek( int handle, long offset, int origin )
{
    dfile_t *fp = fd_to_file( handle );
    long     base;

    if( fp == NULL )
        return( -1L );
    switch( origin ) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = fp->pos;
        break;
    case SEEK_END:
        /* End-of-file position from the byte-exact length we keep in fp->len:
           seeded at open (text via Ctrl-Z scan; binary via a persisted LRBC on
           CP/M 3+, else sector-rounded) and extended exactly by every __qwrite. */
        base = fp->len;
        break;
    default:
        return( -1L );
    }
    if( base + offset < 0 )
        return( -1L );
    fp->pos = base + offset;
    fp->ateof = 0;
    return( fp->pos );
}

/* Public POSIX seek wrappers that Watcom's fseek()/ftell() bottom out into.
   The stock lseek()/_tell() drag in the whole per-handle iomode table
   (__GetIOMode/__handle_check/__NFiles), which our minimal seam deliberately
   omits, so we route straight to __lseek -- the same philosophy as the rest of
   this file. _tell(h) is "where am I" == lseek(h, 0, SEEK_CUR). */
_WCRTLINK long lseek( int handle, long offset, int origin )
{
    return( __lseek( handle, offset, origin ) );
}

_WCRTLINK long _tell( int handle )
{
    return( __lseek( handle, 0L, SEEK_CUR ) );
}

/* fclose -> __close. Console handles are a no-op; disk handles get a BDOS CLOSE
   (which commits the directory entry) and the slot is freed. */
int _WCNEAR __close( int handle )
{
    dfile_t *fp;

    if( handle < DISK_FIRST_FD )
        return( 0 );
    fp = fd_to_file( handle );
    if( fp == NULL )
        return( -1 );
    if( fp->is_con ) {                              /* console device: just free */
        fp->used = 0;
        return( 0 );
    }
    _bdos( BD_CLOSE, (unsigned)(size_t)&fp->fcb[0] );
    /* Write-side exact length (LRBC) -- KNOWN_ISSUES #2. On CP/M 3+ / Concurrent
       CP/M-86 (the RC759's OS) a program records a file's exact byte length by
       re-issuing F_ATTRIB (BDOS fn 30) AFTER close with the F6' request flag set
       (bit 7 of FCB byte 6) and the Last Record Byte Count in FCB+32; the OS then
       stores that count in the directory so a fresh open reports the exact size
       instead of the 128-rounded record count. We do it only for a BINARY file
       we actually WROTE: text files terminate with Ctrl-Z (text_eof() scans for
       it) and must not be truncated. len & 0x7F is the used bytes of the final
       record; 0 means len is an exact multiple of 128 (already record-exact, so
       nothing to record). Note this is the documented F_ATTRIB protocol, NOT
       setting FCB+32 at F_CLOSE -- the close path treats FCB+32 as the sequential
       current-record byte and will not honour it for a handle that has written.
       Gated on os_has_lrbc() so plain CP/M-86 2.2 is untouched. */
    if( fp->wrote && !fp->text && os_has_lrbc() ) {
        unsigned char lrbc = (unsigned char)(fp->len & 0x7F);
        if( lrbc != 0 ) {
            fp->fcb[6]        |= 0x80;              /* F6' = "set byte count"      */
            fp->fcb[FCB_LRBC]  = lrbc;             /* FCB+32 = used bytes, last rec */
            _bdos( BD_ATTRIB, (unsigned)(size_t)&fp->fcb[0] );
            fp->fcb[6]        &= 0x7F;              /* clear F6' again (tidy up)    */
        }
    }
    if( cache_fp == fp )
        cache_fp = NULL;
    fp->used = 0;
    return( 0 );
}

/* isatty: on CP/M-86 the three standard handles are the console (a tty); disk
   files are not. Same seam as the console-only stdioshim.c. */
int isatty( int handle )
{
    return( handle >= 0 && handle <= 2 );
}

/* remove/unlink -> BDOS DELETE (fn 19). BDOS returns 0xFF when no directory
   entry matched; map that to the C contract (-1, errno=ENOENT). This is the
   primitive Watcom's own clibtest (streamio/file) uses to clean temp files. */
int remove( const char *name )
{
    unsigned char fcb[36];

    if( name_to_fcb( name, fcb ) < 0 ) {
        errno = ENOENT;
        return( -1 );
    }
    if( _bdos( BD_DELETE, (unsigned)(size_t)&fcb[0] ) == 0xFF ) {
        errno = ENOENT;
        return( -1 );
    }
    return( 0 );
}

int unlink( const char *name )
{
    return( remove( name ) );
}

/* chmod: CP/M-86 has no POSIX permission bits -- the only writability attribute
   a file carries is the read-only (R/O) bit, t1', which is bit 7 of the first
   file-type byte (FCB byte 9). We therefore support ONLY the write permission
   bit of `pmode`: S_IWRITE set => make the file R/W (clear R/O); S_IWRITE clear
   => make it R/O (set R/O). Every other mode bit (read, execute, and the CP/M
   system/archive attributes) has no portable meaning here and is IGNORED -- a
   deliberate, documented limitation, not a silent drop. Applied via F_ATTRIB
   (BDOS fn 30), which matches the file by name and rewrites its directory
   attributes; BDOS returns 0xFF when no directory entry matched. */
int chmod( const char *name, mode_t pmode )
{
    unsigned char fcb[36];

    if( name_to_fcb( name, fcb ) < 0 ) {
        errno = ENOENT;
        return( -1 );
    }
    if( pmode & S_IWRITE )
        fcb[FCB_TYPE] &= 0x7F;                      /* writable: clear R/O bit */
    else
        fcb[FCB_TYPE] |= 0x80;                      /* read-only: set R/O bit  */
    if( _bdos( BD_ATTRIB, (unsigned)(size_t)&fcb[0] ) == 0xFF ) {
        errno = ENOENT;
        return( -1 );
    }
    return( 0 );
}

/* ---- file-status seam: access / stat / utime -----------------------------
 *
 * Watcom's own access/stat/utime (bld/clib/... ) all bottom into DOS INT 21h
 * (get/set file attributes fn 43h, find-first fn 4Eh, set-file-time fn 5701h)
 * -- forbidden on this target -- so THIS is the CP/M-86 replacement. Everything
 * a program can learn about a CP/M file it learns from ONE directory entry:
 * existence, the read-only (R/O) bit, and the size to the nearest 128-byte
 * record (exact on CP/M 3+ via the LRBC). CP/M-2.2 keeps NO timestamps, so the
 * three st_*time fields and utime() are best-effort no-ops (documented below).
 */

/* Read a file's DIRECTORY ENTRY via F_SFIRST (BDOS fn 17) -- the RIGHT primitive
   for a status probe: it scans the directory and copies the matched 32-byte
   entry to the current DMA, allocating NO open-file lock (unlike F_OPEN fn 15).
   That matters on Concurrent CP/M-86 (the RC759's multitasking OS), where every
   unmatched F_OPEN leaks a system lock-list entry and a few status calls in a
   row overflow the small, system-wide list -- the BDOS then aborts the program
   to the CCP (observed: an F_OPEN-based probe exited disktest to A>). F_SFIRST
   has no such cost and needs no close.

   On success (return 1) the 32-byte entry is copied to `ent`: byte 0 = user #,
   bytes 1..8 = name, bytes 9..11 = type -- and the high bit of byte 9 (t1') is
   the read-only attribute, carried straight from the physical directory on real
   CP/M and set the same way by emu2's search. Returns 0 = no such file, -1 =
   illegal CP/M name. AL holds the entry's index (0..3) within the 128-byte DMA;
   mask to stay inside our work buffer. */
static int stat_probe( const char *name, unsigned char *ent )
{
    unsigned char fcb[36];
    int           al;

    if( name_to_fcb( name, fcb ) < 0 )
        return( -1 );
    set_dma();
    al = _bdos( BD_SFIRST, (unsigned)(size_t)&fcb[0] );
    if( al == 0xFF )
        return( 0 );
    memcpy( ent, &dma[(al & 3) * 32], 32 );
    return( 1 );
}

/* access(name, mode): existence + permission check. CP/M carries only the R/O
   attribute, so W_OK is the only permission we can truthfully answer -- a file
   with the R/O bit set fails W_OK (errno EACCES). R_OK/X_OK/F_OK succeed for any
   file that exists (every CP/M file is readable and, being no OS distinction,
   nominally "executable"). A missing file (or illegal name) is ENOENT. */
_WCRTLINK int access( const char *name, int mode )
{
    unsigned char ent[32];

    if( stat_probe( name, ent ) != 1 ) {
        errno = ENOENT;
        return( -1 );
    }
    if( (mode & W_OK) && (ent[FCB_TYPE] & 0x80) ) { /* wants write, file is R/O */
        errno = EACCES;
        return( -1 );
    }
    return( 0 );
}

/* stat(name, buf): fill the POSIX status of a CP/M file. What CP/M can back
   truthfully: st_size (from F_SIZE fn 35, rounded UP to the next 128-byte record
   -- the inherent CP/M limit for a probe that does not open the file), the write
   bit of st_mode (from the R/O attribute), execute for a ".CMD" transient
   program, and st_dev (the drive). st_atime/st_mtime/st_ctime are left 0 for
   now: CP/M-2.2 has no timestamps at all, and while the CP/M-3 medium this runs
   on (RC759 Concurrent CP/M-86 3.1) DOES carry per-file SFCB datestamps, reading
   them via F_TIMEDATE (fn 102) is a tracked enhancement -- 0 is an honest "not
   yet read", not a claim that the epoch is 1970. */
_WCRTLINK int stat( const char *name, struct stat *buf )
{
    unsigned char ent[32];
    unsigned char fcb[36];
    long          records;
    mode_t        perm;

    if( stat_probe( name, ent ) != 1 ) {
        errno = ENOENT;
        return( -1 );
    }

    /* Size via F_SIZE (fn 35): fills the FCB random record with the file's size
       in 128-byte records, straight off the directory -- no open, no lock. That
       is only 128-byte-granular, so on CP/M 3+ (CCP/M-86) we additionally OPEN
       with the LRBC probe (FCB+32 = 0xFF) to recover the exact byte length of
       the final record -- WITHOUT it, stat() reports a record-rounded st_size
       (e.g. 384 for a 358-byte file) while the open handle reads the exact 358,
       and the mismatch derails length-driven callers such as UnZip's
       end-of-central-directory scan (G.ziplen = statbuf.st_size). */
    name_to_fcb( name, fcb );
    _bdos( BD_FILESIZE, (unsigned)(size_t)&fcb[0] );
    records = (long)fcb[FCB_R0 + 0]
            | ((long)fcb[FCB_R0 + 1] << 8)
            | ((long)fcb[FCB_R0 + 2] << 16);

    memset( buf, 0, sizeof( *buf ) );
    buf->st_size = records * SECT;                    /* record-rounded default */
    if( records > 0 && os_has_lrbc() ) {
        unsigned char ofcb[36];
        name_to_fcb( name, ofcb );
        ofcb[FCB_LRBC] = 0xFF;                        /* ask CP/M 3+ for the LRBC */
        if( _bdos( BD_OPEN, (unsigned)(size_t)&ofcb[0] ) != 0xFF ) {
            unsigned char lrbc = ofcb[FCB_LRBC];
            _bdos( BD_CLOSE, (unsigned)(size_t)&ofcb[0] );
            if( lrbc != 0xFF )                        /* exact final-record length */
                buf->st_size = (records - 1) * SECT + (lrbc == 0 ? SECT : lrbc);
        }
    }

    perm = S_IREAD;                                  /* every CP/M file is readable */
    if( !(ent[FCB_TYPE] & 0x80) )                    /* R/O bit clear => writable */
        perm |= S_IWRITE;
    if( (ent[FCB_TYPE + 0] & 0x7F) == 'C'            /* ".CMD" transient => exec */
     && (ent[FCB_TYPE + 1] & 0x7F) == 'M'
     && (ent[FCB_TYPE + 2] & 0x7F) == 'D' )
        perm |= S_IEXEC;
    /* replicate owner bits into group/other -- CP/M has no per-class permission */
    buf->st_mode = (mode_t)(S_IFREG | perm | (perm >> 3) | (perm >> 6));

    buf->st_nlink = 1;
    buf->st_dev = buf->st_rdev = 0;                  /* CP/M dir entry carries no drive */
    return( 0 );
}

/* utime(name, times): CP/M's directory has no host-settable timestamp field we
   can write from here -- CP/M-2.2 has none at all, and CP/M-3 datestamps are
   maintained by the BDOS itself (create/update/access), not set to an arbitrary
   value by a user call. So we honour the POSIX contract we CAN keep -- report
   ENOENT for a missing file -- and otherwise succeed as a no-op (a program that
   sets times and never reads them back, as clibtest does, must see success).
   The `times` argument is accepted and ignored. */
_WCRTLINK int utime( const char *name, const struct utimbuf *times )
{
    unsigned char ent[32];

    (void)times;
    if( stat_probe( name, ent ) != 1 ) {
        errno = ENOENT;
        return( -1 );
    }
    return( 0 );
}

/* exit(status): CP/M-86 has NO DOS INT 21h/4Ch, but Watcom's own exit() bottoms
   into _exit -> __exit which terminates via exactly that -- forbidden here. So
   this is the CP/M-86 replacement. Flush + close every open FILE* first (via
   __full_io_exit, so a buffered failure message the streamio test wrote to the
   console is actually emitted) and then BDOS System Reset (fn 0) ends the
   process. streamio/iotest.c only calls exit() on a VERIFY/EXPECT failure, so on
   a clean run this is never reached -- but every build must resolve it. */
extern void _WCNEAR __full_io_exit( void );
_WCRTLINK void exit( int status )
{
    (void)status;
    __full_io_exit();
    _bdos( 0, 0 );                                  /* BDOS fn 0: System Reset */
    for( ;; )                                       /* not reached */
        ;
}

/* ---- Low-level POSIX I/O + rename: the handleio-layer seam ----------------
 *
 * Watcom's own handleio open/creat/read/write/close (bld/clib/handleio/c/ files)
 * all bottom into DOS INT 21h via __getOSHandle -- forbidden on this target --
 * so THIS file is the CP/M-86 replacement for that whole layer. These thin
 * POSIX entry points let a program (and Watcom's UNCHANGED clibtest
 * handleio/iotest.c) use fd-level I/O directly; they resolve onto the very same
 * dfiles[] handle table and BDOS record primitives the FILE* seam uses. _sopen
 * now also registers each handle in Watcom's per-handle iomode table
 * (__SetIOMode_grow, pure C -- no INT 21h) so fdopen()/freopen()/dup() can learn
 * a handle's read/write/text flags. (ow#3 clibtest GAP.)
 */

/* open(name, mode[, pmode]) -> _sopen. CP/M-86 has no per-file permission bits,
   so the pmode vararg is accepted for source compatibility and ignored. */
_WCRTLINK int open( const char *name, int mode, ... )
{
    return( _sopen( name, mode, 0 ) );
}

/* creat(name, pmode) == open write-only, create + truncate. */
_WCRTLINK int creat( const char *name, mode_t pmode )
{
    (void)pmode;
    return( _sopen( name, O_WRONLY | O_CREAT | O_TRUNC, 0 ) );
}

/* read/write/close: fd-level, straight onto the record-model primitives. */
_WCRTLINK int read( int handle, void *buffer, unsigned len )
{
    return( __qread( handle, buffer, len ) );
}

_WCRTLINK int write( int handle, const void *buffer, unsigned len )
{
    return( __qwrite( handle, buffer, len ) );
}

_WCRTLINK int close( int handle )
{
    return( __close( handle ) );
}

/* dup(oldfd): return a second handle onto the same stream. Watcom's own
   handleio/c/dup.c bottoms into a TinyDOS INT 21h duplicate -- forbidden here --
   so this is the CP/M-86 replacement. The streamio clibtest only ever dups a
   console handle: old_stdout_fd = dup(fileno(stdout)), then fdopen()s it and
   prints the final "Tests completed" banner through it. So we implement the
   console case fully (a fresh is_con slot routing to C_WRITE); a disk dup would
   need shared-position refcounting the seam does not carry, so it is refused. */
_WCRTLINK int dup( int handle )
{
    dfile_t *fp;
    int      i;

    for( i = 0; i < DISK_MAX; i++ )                 /* find a free slot */
        if( !dfiles[i].used )
            break;
    if( i >= DISK_MAX ) {
        errno = EMFILE;
        return( -1 );
    }
    fp = &dfiles[i];

    if( handle >= 0 && handle <= 2 ) {              /* dup of a std console handle */
        fp->is_con   = 1;
        fp->readable = (unsigned char)(handle == STDIN_FILENO);
        fp->writable = (unsigned char)(handle != STDIN_FILENO);
        fp->append   = 0;
        fp->text     = 1;
        fp->wrote    = 0;
        fp->pos      = 0;
        fp->len      = 0;
        fp->ateof    = 0;
        fp->open_lrbc = 0xFF;
        fp->used     = 1;
        REGISTER_IOMODE( DISK_FIRST_FD + i, QUERY_IOMODE( handle ) );
        return( DISK_FIRST_FD + i );
    }

    errno = EBADF;                                  /* disk dup not supported */
    return( -1 );
}


/* tell(h) == lseek(h, 0, SEEK_CUR): byte-exact current position. */
_WCRTLINK long tell( int handle )
{
    return( __lseek( handle, 0L, SEEK_CUR ) );
}

/* filelength(h): the exact logical length tracked in fp->len. Byte-exact for
   text files and for anything written this session (__qwrite extends fp->len by
   the true byte count); a binary file merely REOPENED read-only inherits the
   record-rounded seed on plain CP/M-2.2 -- the KNOWN_ISSUES #1 length LIMIT. */
_WCRTLINK long filelength( int handle )
{
    dfile_t *fp = fd_to_file( handle );

    if( fp == NULL )
        return( -1L );
    return( fp->len );
}

/* eof(h): 1 at/after end-of-file, 0 before it, -1 on a bad handle -- the classic
   Watcom/DOS contract, decided by the byte-exact position vs. length. */
_WCRTLINK int eof( int handle )
{
    dfile_t *fp = fd_to_file( handle );

    if( fp == NULL )
        return( -1 );
    return( fp->pos >= fp->len ? 1 : 0 );
}

/* rename(old, new) -> BDOS RENAME FILE (fn 23). The 36-byte control block holds
   the EXISTING file in bytes 0..15 and the NEW name in bytes 16..31 (the drive
   byte at +16 is 0); BDOS returns 0xFF when no directory entry matched the old
   name. Worked example: rename("LOWA.DAT","LOWB.DAT") builds
   fcb = {drv,'LOWA    ','DAT',0.., 0,'LOWB    ','DAT'} and fn 23 flips the
   directory entry's name+type in place, keeping the file's data blocks. */
int rename( const char *old, const char *new )
{
    unsigned char fcb[36];
    unsigned char nfcb[36];
    int           i;

    if( name_to_fcb( old, fcb ) < 0 || name_to_fcb( new, nfcb ) < 0 ) {
        errno = ENOENT;
        return( -1 );
    }
    for( i = 0; i < 16; i++ )                       /* new-name half: drive 0 */
        fcb[16 + i] = 0;
    for( i = FCB_NAME; i < FCB_TYPE + 3; i++ )      /* copy name(1..8)+type(9..11) */
        fcb[16 + i] = nfcb[i];
    if( _bdos( BD_RENAME, (unsigned)(size_t)&fcb[0] ) == 0xFF ) {
        errno = ENOENT;
        return( -1 );
    }
    return( 0 );
}

/* ---- tmpnam / tmpfile: the streamio-layer temp-file seam ------------------
 *
 * Watcom's own tmpfl.c/tmputil.c build the temp name from getenv("TMP") +
 * getpid() + a hex suffix and lean on access()/_fullpath/_RWD state that this
 * freestanding CP/M-86 target does not carry. This is the "quite simple"
 * self-contained replacement: names are "TMPnnnnn.$$$" in the current drive,
 * uniqueness is proven by trying to open() the candidate (a CP/M directory has
 * no other namespace to collide with), and removal-on-close reuses Watcom's
 * OWN fclose path -- fclose calls (*__RmTmpFileFn)(fp) whenever _TMPFIL is set,
 * so we set that flag and point the hook at rm_tmpfile below. Worked example:
 * first call yields "TMP00000.$$$"; tmpfile() fopen()s it "wb+", registers
 * {fp,"TMP00000.$$$"} in tmpreg[0], and a later fclose(fp) fires rm_tmpfile,
 * which remove()s "TMP00000.$$$" after the handle is already closed. */

extern void _WCNEAR (*__RmTmpFileFn)( FILE *fp );   /* defined in Watcom fclose */

#define TMPFILE_MAX 16                              /* concurrent tmpfile()s;
                                                       iotest opens NUM_FILES=10
                                                       at once (must not exceed
                                                       DISK_MAX slots) */

static struct {
    FILE *fp;
    char  name[L_tmpnam];
} tmpreg[TMPFILE_MAX];

/* fclose's removal hook: find fp's registered temp name and delete it. Called
   AFTER __close (see Watcom fclose.c), so remove() acts on a closed file. */
static void _WCNEAR rm_tmpfile( FILE *fp )
{
    int i;

    for( i = 0; i < TMPFILE_MAX; i++ ) {
        if( tmpreg[i].fp == fp ) {
            remove( tmpreg[i].name );
            tmpreg[i].fp = NULL;
            return;
        }
    }
}

/* tmpnam(s): write a free "TMPnnnnn.$$$" into s (or a static buffer if NULL)
   and return it; NULL if all TMP_MAX candidates are taken. A candidate is free
   when open(O_RDONLY) fails -- i.e. the file does not exist. */
_WCRTLINK char *tmpnam( char *s )
{
    static char tmpnam_buf[L_tmpnam];
    static unsigned next = 0;
    char    *dst = ( s != NULL ) ? s : tmpnam_buf;
    unsigned n = next;
    unsigned tries;
    int      h;

    /* `unsigned` is 16-bit here, so bound the search by an explicit attempt
       count (TMP_MAX == 26^3 fits in 16 bits) and let n wrap; a 5-digit name
       spans n in 0..65535, more than enough distinct candidates. */
    for( tries = 0; tries < (unsigned)TMP_MAX; tries++, n++ ) {
        dst[0] = 'T'; dst[1] = 'M'; dst[2] = 'P';
        dst[3] = (char)( '0' + ( n / 10000 ) % 10 );
        dst[4] = (char)( '0' + ( n /  1000 ) % 10 );
        dst[5] = (char)( '0' + ( n /   100 ) % 10 );
        dst[6] = (char)( '0' + ( n /    10 ) % 10 );
        dst[7] = (char)( '0' +   n           % 10 );
        dst[8] = '.'; dst[9] = '$'; dst[10] = '$'; dst[11] = '$';
        dst[12] = '\0';
        h = open( dst, O_RDONLY );
        if( h == -1 ) {                 /* does not exist -> name is free */
            next = n + 1;
            return( dst );
        }
        close( h );                     /* exists -> keep looking */
    }
    return( NULL );
}

/* tmpfile(): create+open a unique temp file "wb+" (O_RDWR|O_CREAT|O_TRUNC,
   binary) and arrange auto-removal at fclose via the _TMPFIL / __RmTmpFileFn
   contract. Returns NULL if no name is free, the open fails, or tmpreg[] is
   full. */
_WCRTLINK FILE *tmpfile( void )
{
    char  name[L_tmpnam];
    FILE *fp;
    int   i;

    if( tmpnam( name ) == NULL )
        return( NULL );
    fp = fopen( name, "wb+" );
    if( fp == NULL )
        return( NULL );
    for( i = 0; i < TMPFILE_MAX; i++ ) {
        if( tmpreg[i].fp == NULL ) {
            tmpreg[i].fp = fp;
            strcpy( tmpreg[i].name, name );
            fp->_flag |= _TMPFIL;       /* fclose -> (*__RmTmpFileFn)(fp) */
            __RmTmpFileFn = rm_tmpfile;
            return( fp );
        }
    }
    fclose( fp );                       /* registry full: don't leak an entry */
    remove( name );
    return( NULL );
}

/* ---- command-tail redirection front end ---------------------------------- */

/* Parse ONE redirect operand. Recognises  <name  >name  >>name  and the split
   forms  < name / > name / >> name  (operator and file in separate argv slots).
   On a match, opens the file and latches redir_in / redir_out; returns the
   number of argv slots consumed (1 for the joined form, 2 for the split form),
   or 0 if `a` is not a redirect operand (an ordinary program operand). `next`
   is argv[i+1] (may be NULL) for the split form. */
static int redirect_one( char *a, char *next )
{
    const char *name;
    int         mode;
    int         is_in;
    int         consumed = 1;
    int         fd;

    if( a[0] == '<' ) {
        is_in = 1;
        mode  = O_RDONLY;                        /* text: reads stop at Ctrl-Z */
        name  = a + 1;
    } else if( a[0] == '>' && a[1] == '>' ) {
        is_in = 0;
        mode  = O_WRONLY | O_CREAT | O_APPEND;    /* append, create if absent */
        name  = a + 2;
    } else if( a[0] == '>' ) {
        is_in = 0;
        mode  = O_WRONLY | O_CREAT | O_TRUNC;     /* truncate/create */
        name  = a + 1;
    } else {
        return( 0 );                             /* not a redirect operand */
    }

    if( *name == '\0' ) {                        /* split form: name is next slot */
        if( next == NULL )
            return( 1 );                         /* dangling operator: just drop it */
        name = next;
        consumed = 2;
    }

    fd = _sopen( name, mode, 0 );
    if( fd >= 0 ) {                              /* open failure: leave on console */
        if( is_in ) {
            if( redir_in >= 0 )
                __close( redir_in );             /* last `<` wins */
            redir_in = fd;
        } else {
            if( redir_out >= 0 )
                __close( redir_out );            /* last `>`/`>>` wins */
            redir_out = fd;
        }
    }
    return( consumed );
}

/* __apply_redirection -- called from crt0 (via cominit.c's __CommonRedirect)
   AFTER the command tail is split into argv and BEFORE main(). Scans argv[1..]
   for redirect operands, opens them, and compacts argv so main() sees only the
   real operands. Returns the new argc. argv[0] (program name) is always kept. */
int __apply_redirection( int argc, char **argv )
{
    int i;
    int out = 1;                                 /* compacted write index */

    for( i = 1; i < argc; i++ ) {
        char *next = ( i + 1 < argc ) ? argv[i + 1] : NULL;
        int   used = redirect_one( argv[i], next );
        if( used == 0 ) {
            argv[out++] = argv[i];               /* ordinary operand: keep it */
        } else {
            i += used - 1;                       /* skip a split-form file slot */
        }
    }
    argv[out] = NULL;
    return( out );
}

/* __close_redirection -- called from crt0 AFTER main() returns. Flush the
   buffered stdout FILE (our minimal crt0 runs no atexit chain, so nothing else
   would) then commit each redirected disk file: __close writes the final partial
   record, appends the Ctrl-Z text EOF, and closes the directory entry. */
void __close_redirection( void )
{
    if( redir_out >= 0 ) {
        fflush( stdout );                        /* drain the FILE buffer first */
        __close( redir_out );
        redir_out = -1;
    }
    if( redir_in >= 0 ) {
        __close( redir_in );
        redir_in = -1;
    }
}
