/* dirent.c -- opendir()/readdir()/closedir()/rewinddir() for CP/M-86, backed by
 * the BDOS directory search calls F_SFIRST (fn 17) and F_SNEXT (fn 18). Fills
 * Watcom's own <dirent.h> `struct dirent` (on DOS/CP/M that header makes DIR and
 * struct dirent the SAME type), so Info-ZIP's unix OS layer -- and any POSIX-ish
 * caller -- can enumerate files with no source change.
 *
 * WHY this is a hand-written seam and not Watcom's stock opendir: Watcom's DOS
 * opendir is built on the INT 21h findfirst DTA, which does not exist under CP/M
 * BDOS (INT 0E0h). This file is the BDOS equivalent.
 *
 * ---------------------------------------------------------------------------
 * WILDCARDS -- the one non-obvious part (flagged in review):
 *   The CP/M FCB matches a name with '?' in each of the 11 name(8)/type(3)
 *   positions. BDOS has NO concept of '*': a literal '*' put in an FCB is matched
 *   as the byte 0x2A, not "the rest of the field". So the caller's DOS/Unix-style
 *   '*' MUST be expanded HERE -- within a field, '*' fills the REST of that field
 *   with '?'. Worked examples (name8 | type3 written as the 11 FCB bytes):
 *       "*.*"    -> "????????" "???"     (match everything)
 *       "*.C"    -> "????????" "C  "     (any name, type exactly C)
 *       "FOO*.?" -> "FOO?????" "?  "     (name FOO+anything, 1-char type)
 *       "AB.C"   -> "AB      " "C  "     (exact: matches AB.C, NOT ABC.C)
 *   A field with no '*' that ends early is SPACE-padded, i.e. an EXACT match on
 *   "no further characters" -- that is deliberate CP/M semantics, not a bug.
 *
 * SINGLE ACTIVE SCAN: CP/M's BDOS keeps ONE global search cursor -- F_SNEXT
 * always continues the most recent F_SFIRST. Two interleaved live scans, or any
 * intervening directory/file BDOS call, corrupt the cursor. A DIR is therefore
 * only valid for a tight opendir->readdir*->closedir loop with no other file I/O
 * in between. Info-ZIP's wild() scans one directory at a time, which fits.
 * ---------------------------------------------------------------------------
 */
#include "variety.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>

/* ---- CP/M-86 BDOS gateway (same INT 0E0h ABI as diskio.c) ---------------- */
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

#define BD_SETDMA       26      /* F_DMAOFF: set DMA (transfer) offset       */
#define BD_SETDMASEG    51      /* F_DMASEG: set DMA segment (CP/M-86)        */
#define BD_SFIRST       17      /* F_SFIRST: search directory, first match   */
#define BD_SNEXT        18      /* F_SNEXT:  search directory, next match     */

/* 36-byte FCB field offsets (search FCB and 32-byte dir entry share layout). */
#define FCB_DRIVE       0
#define FCB_NAME        1       /* 8 bytes */
#define FCB_TYPE        9       /* 3 bytes */
#define FCB_EX          12      /* extent number */
#define FCB_S2          14      /* extent high byte */

#define SECT            128

/* 128-byte DMA buffer BDOS copies the matched directory sector into. Static =>
 * lives in DGROUP, so its near offset is what F_DMAOFF wants in small model. */
static unsigned char dma[SECT];

/* Our DIR handle. `de` MUST be first: Watcom's <dirent.h> makes DIR == struct
 * dirent, so opendir() returns &de, which equals the malloc block -> closedir()
 * can free the same pointer. The FCB and `first` flag trail it as private state
 * carried across readdir() calls (each DIR keeps its own search pattern). */
typedef struct {
    struct dirent de;
    unsigned char fcb[36];
    unsigned char first;        /* 1 until the first readdir() issues F_SFIRST */
} cpmdir_t;

/* Point the BDOS DMA at our work buffer (segment AND offset, so we do not rely
 * on the load-time default DMA base still being live). */
static void set_dma( void )
{
    _bdos( BD_SETDMASEG, _getds() );
    _bdos( BD_SETDMA, (unsigned)(size_t)&dma[0] );
}

/* Expand a DOS/Unix path/pattern into a CP/M search FCB. Returns 0, or -1 on a
 * pattern that cannot map to a CP/M name. See the WILDCARDS block above for the
 * '*'-fill rule -- this is where it happens. A bare directory reference (".",
 * "", "A:", trailing separator) means "every file in the user area" -> all '?'. */
static int pattern_to_fcb( const char *name, unsigned char *fcb )
{
    int         i;
    const char *p = name;

    for( i = 0; i < 36; i++ )
        fcb[i] = 0;
    for( i = FCB_NAME; i < FCB_NAME + 11; i++ )     /* name+type default: SPACE */
        fcb[i] = ' ';

    if( p == NULL )
        p = "";

    /* Leading "." or "./" -> current directory (CP/M has no subdirs). */
    if( p[0] == '.' && (p[1] == '\0' || p[1] == '/' || p[1] == '\\') )
        p = (p[1] == '\0') ? "" : p + 2;

    if( p[0] != '\0' && p[1] == ':' ) {             /* drive letter */
        char d = p[0];
        if( d >= 'a' && d <= 'z' )
            d = (char)(d - 0x20);
        if( d < 'A' || d > 'P' )
            return( -1 );
        fcb[FCB_DRIVE] = (unsigned char)(d - 'A' + 1);
        p += 2;
    }

    /* Nothing (or only a path separator) left => enumerate everything. */
    if( p[0] == '\0' || ((p[0] == '/' || p[0] == '\\') && p[1] == '\0') ) {
        for( i = FCB_NAME; i < FCB_NAME + 11; i++ )
            fcb[i] = '?';
        return( 0 );
    }

    i = 0;                                          /* base name field (8) */
    while( *p != '\0' && *p != '.' ) {
        char c = *p++;
        if( c == '*' ) {                            /* '*' fills rest with '?' */
            while( i < 8 )
                fcb[FCB_NAME + i++] = '?';
            while( *p != '\0' && *p != '.' )        /* consume to field end   */
                p++;
            break;
        }
        if( c >= 'a' && c <= 'z' )
            c = (char)(c - 0x20);
        if( i < 8 )
            fcb[FCB_NAME + i] = (unsigned char)c;    /* '?' passes through as-is */
        i++;
    }

    if( *p == '.' ) {                               /* type field (3) */
        p++;
        i = 0;
        while( *p != '\0' ) {
            char c = *p++;
            if( c == '*' ) {
                while( i < 3 )
                    fcb[FCB_TYPE + i++] = '?';
                break;
            }
            if( c >= 'a' && c <= 'z' )
                c = (char)(c - 0x20);
            if( i < 3 )
                fcb[FCB_TYPE + i] = (unsigned char)c;
            i++;
        }
    }
    return( 0 );
}

_WCRTLINK DIR *opendir( const char *dirname )
{
    cpmdir_t *d = (cpmdir_t *)malloc( sizeof( cpmdir_t ) );

    if( d == NULL ) {
        errno = ENOMEM;
        return( NULL );
    }
    if( pattern_to_fcb( dirname, d->fcb ) < 0 ) {   /* not a CP/M-able name    */
        free( d );
        errno = ENOENT;
        return( NULL );
    }
    d->first = 1;
    d->de.d_name[0] = '\0';
    return( (DIR *)d );
}

_WCRTLINK struct dirent *readdir( DIR *dirp )
{
    cpmdir_t      *d = (cpmdir_t *)dirp;
    unsigned char *ent;
    int            al;
    int            i, k;

    if( d == NULL ) {
        errno = EBADF;
        return( NULL );
    }

    set_dma();
    for( ;; ) {
        al = _bdos( d->first ? BD_SFIRST : BD_SNEXT,
                    (unsigned)(size_t)&d->fcb[0] );
        d->first = 0;
        if( al == 0xFF )                            /* 0xFF = no more matches  */
            return( NULL );

        ent = &dma[(al & 3) * 32];                  /* matched 32-byte entry   */

        /* Report each file ONCE: a file larger than one extent has several
         * directory entries; only extent 0 (EX and S2 both zero) is the file's
         * canonical entry. Skipping the rest avoids duplicate names. The search
         * FCB already has EX==0 (restricting most BDOS to first extents), so this
         * is belt-and-suspenders for implementations that ignore that. */
        if( ent[FCB_EX] != 0 || ent[FCB_S2] != 0 )
            continue;

        k = 0;                                      /* build "NAME.EXT" */
        for( i = 0; i < 8; i++ ) {
            char c = (char)(ent[FCB_NAME + i] & 0x7F); /* mask attribute bits */
            if( c == ' ' )
                break;
            d->de.d_name[k++] = c;
        }
        for( i = 0; i < 3; i++ ) {
            char c = (char)(ent[FCB_TYPE + i] & 0x7F);
            if( c == ' ' )
                break;
            if( i == 0 )
                d->de.d_name[k++] = '.';
            d->de.d_name[k++] = c;
        }
        d->de.d_name[k] = '\0';

        /* CP/M carries only R/O (t1'), SYSTEM (t2') and ARCHIVE (t3') in the
         * high bits of the three type bytes; there is no size/time in a plain
         * CP/M 2.2 directory entry, so those dirent fields stay zero (callers
         * needing size/time use stat()). */
        d->de.d_attr = 0;
        if( ent[FCB_TYPE + 0] & 0x80 ) d->de.d_attr |= _A_RDONLY;
        if( ent[FCB_TYPE + 1] & 0x80 ) d->de.d_attr |= _A_SYSTEM;
        if( ent[FCB_TYPE + 2] & 0x80 ) d->de.d_attr |= _A_ARCH;
        d->de.d_time = 0;
        d->de.d_date = 0;
        d->de.d_size = 0;
        d->de.d_ino  = 0;
        return( &d->de );
    }
}

_WCRTLINK void rewinddir( DIR *dirp )
{
    if( dirp != NULL )
        ((cpmdir_t *)dirp)->first = 1;              /* next readdir re-issues F_SFIRST */
}

_WCRTLINK int closedir( DIR *dirp )
{
    if( dirp == NULL ) {
        errno = EBADF;
        return( -1 );
    }
    free( dirp );
    return( 0 );
}
