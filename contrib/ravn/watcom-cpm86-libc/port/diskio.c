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
#include "qread.h"
#include "qwrite.h"

/* ---- CP/M-86 BDOS gateway ------------------------------------------------ */
/* INT 0E0h: function in CL, parameter (near offset, or a segment for fn 51) in
   DX, result byte returned in AL. Small model => DS is the one data group, so a
   near &object is the DMA/FCB offset the BDOS wants. */
extern unsigned char _bdos( unsigned char fn, unsigned param );
#pragma aux _bdos =             \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [al]                  \
    modify [ax bx cx dx es];

extern unsigned _getds( void );
#pragma aux _getds =            \
    "mov ax,ds"                 \
    value [ax]                  \
    modify [ax];

extern void _bdos_conout( int c );      /* BDOS C_WRITE (fn 2, char in DL) */
#pragma aux _bdos_conout =      \
    "mov cl,2"                  \
    "int 0E0h"                  \
    parm [dx]                   \
    modify [ax bx cx es];

/* BDOS function numbers we use. */
#define BD_VERSION  12          /* S_BDOSVER: OS/BDOS version (runtime capability) */
#define BD_OPEN     15
#define BD_CLOSE    16
#define BD_DELETE   19
#define BD_RENAME   23          /* F_RENAME: old FCB in 0..15, new name in 16..31 */
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
#define DISK_MAX      8

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
    unsigned char ateof;
    unsigned char open_lrbc;    /* LRBC byte captured at open (0xFF = OS gave
                                   none => no exact length available) */
} dfile_t;

static dfile_t        dfiles[DISK_MAX];
static unsigned char  dma[SECT];        /* our 128-byte DMA / work buffer */
static dfile_t       *cache_fp;         /* which file's record is in dma */
static long           cache_rec;        /* which record is in dma (valid iff cache_fp) */

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

    if( name_to_fcb( name, fp->fcb ) < 0 )
        return( -1 );

    acc = mode & (O_RDONLY | O_WRONLY | O_RDWR);
    fp->readable = (unsigned char)(acc == O_RDONLY || acc == O_RDWR);
    fp->writable = (unsigned char)(acc == O_WRONLY || acc == O_RDWR);
    fp->text     = (unsigned char)((mode & O_BINARY) == 0);
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
    return( DISK_FIRST_FD + i );
}

/* FILE fill path (fgetc/fread) -> __qread. Returns bytes read, 0 at EOF. */
int _WCNEAR __qread( int handle, void *buffer, unsigned len )
{
    dfile_t       *fp;
    unsigned char *out = (unsigned char *)buffer;
    unsigned       total = 0;

    if( handle < DISK_FIRST_FD )
        return( 0 );                                /* console: no stdin here */
    fp = fd_to_file( handle );
    if( fp == NULL || !fp->readable || fp->ateof )
        return( 0 );

    while( total < len ) {
        long     rec = fp->pos >> 7;
        unsigned off = (unsigned)(fp->pos & 127);
        unsigned avail = SECT - off;
        unsigned n = len - total;
        unsigned k;

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

    if( handle == STDOUT_FILENO || handle == STDERR_FILENO ) {
        unsigned i;
        for( i = 0; i < len; i++ )
            _bdos_conout( in[i] );
        return( (int)len );
    }

    fp = fd_to_file( handle );
    if( fp == NULL || !fp->writable )
        return( -1 );

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
    _bdos( BD_CLOSE, (unsigned)(size_t)&fp->fcb[0] );
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

/* ---- Low-level POSIX I/O + rename: the handleio-layer seam ----------------
 *
 * Watcom's own handleio open/creat/read/write/close (bld/clib/handleio/c/*.c)
 * all bottom into DOS INT 21h via __getOSHandle -- forbidden on this target --
 * so THIS file is the CP/M-86 replacement for that whole layer. These thin
 * POSIX entry points let a program (and Watcom's UNCHANGED clibtest
 * handleio/iotest.c) use fd-level I/O directly; they resolve onto the very same
 * dfiles[] handle table and BDOS record primitives the FILE* seam uses, and
 * deliberately skip Watcom's per-handle iomode table (__GetIOMode /
 * __handle_check), matching the rest of this seam. (ow#3 clibtest GAP.)
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
