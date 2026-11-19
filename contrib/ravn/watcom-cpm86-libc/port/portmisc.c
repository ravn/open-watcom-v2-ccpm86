/* portmisc.c -- CP/M-86 seams for the few hosted C-library entry points that
 * have no meaningful implementation on a flat, single-user CP/M-86 system.
 * Everything that CAN be reused is reused (string/ctype/stdio FILE*, heap,
 * time-conversion + the real time()/__getctime() wall-clock seam all link
 * from stock Watcom source).  This file supplies only what genuinely cannot
 * exist -- or would drag in irrelevant machinery -- on CP/M-86:
 *
 * setmode()    : all CP/M-86 file I/O is binary; a mode switch is a no-op.
 * signal()     : plain CP/M-86 delivers no asynchronous signals; installing a
 *                handler is accepted and ignored (returns 0).
 * getenv()     : CP/M-86 has NO environment block (unlike MS-DOS, where the
 *                environment lives in a segment pointed at by PSP:[2Ch] -- the
 *                mechanism Aztec C's DOS getenv.asm and every DOS C runtime use;
 *                on CP/M-86 both Aztec C and Digital Research C simply omit
 *                getenv, because the OS has neither the block nor a SET command).
 *                We instead back getenv() with a plain text file "ENV" on the
 *                current drive, one "NAME=VALUE" per line -- the only channel on
 *                CP/M-86 whose *contents* the CCP does not fold to upper case.
 *                This revives the case-preserving env route that hosted programs
 *                already use, e.g. UnZip's envargs(getenv("UNZIP")): a line
 *                "UNZIP=-t" lets a lowercase option letter (test-archive, != -T
 *                set-timestamp) reach the parser even though the CCP uppercases
 *                the command tail.  The file is read once, lazily, on the first
 *                getenv() call; if it is absent every lookup returns NULL (the
 *                C-standard "name not found"), so default behaviour is unchanged.
 *                We supply getenv here rather than linking Watcom's getenv.c
 *                because that one performs a locale-correct multibyte (DBCS)
 *                name comparison and so pulls the whole __mbsnextc/__ismbblead
 *                codepage subsystem in -- pure dead weight against the hard 64 KB
 *                single-code-segment ceiling.
 * environ      : the (empty) environment vector, kept defined for the few
 *                programs that reference the symbol directly.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

int setmode( int handle, int mode )
{
    (void)handle; (void)mode;
    return( 0 );                    /* all I/O already binary */
}

void ( *signal( int sig, void (*func)( int ) ) )( int )
{
    (void)sig; (void)func;
    return( (void (*)( int ))0 );   /* no async signals: accept & ignore */
}

/* File-backed environment (see the getenv() note in the file header).
 *
 * On first use we slurp "ENV" from the current drive -- a small text file of
 * "NAME=VALUE" lines -- into env_buf, split each line in place, and index the
 * NAME/VALUE spans.  Worked example: a file whose single line is "UNZIP=-t\r\n"
 * loads as env_buf = "UNZIP\0-t\0", env_name[0]->"UNZIP", env_val[0]->"-t", so
 * getenv("UNZIP") returns "-t" with its lower-case 't' intact (the CCP never
 * saw the file's bytes).  Missing file -> env_count stays 0 -> every lookup
 * returns NULL, i.e. exactly the old no-environment behaviour.
 */
#define ENV_FILE    "ENV"           /* current-drive text file, NAME=VALUE/line */
#define ENV_BUFSZ   512             /* whole file must fit here (rest ignored)  */
#define ENV_MAXVAR  16              /* at most this many NAME=VALUE entries     */

static char  env_buf[ENV_BUFSZ];
static char *env_name[ENV_MAXVAR];
static char *env_val[ENV_MAXVAR];
static int   env_count = -1;        /* -1 = file not yet read; >=0 = loaded     */

static void env_load( void )
{
    FILE   *fp;
    size_t  n;
    char   *p, *end;

    env_count = 0;                  /* mark loaded even if the file is absent   */
    fp = fopen( ENV_FILE, "r" );
    if( fp == NULL )
        return;                     /* no file -> no variables, getenv == NULL  */
    n = fread( env_buf, 1, ENV_BUFSZ - 1, fp );
    fclose( fp );
    env_buf[n] = '\0';

    p   = env_buf;
    end = env_buf + n;
    while( p < end && env_count < ENV_MAXVAR ) {
        char *line = p, *nl, *eq;

        /* terminate the current line at the first CR or LF (handles \n, \r\n) */
        nl = line;
        while( nl < end && *nl != '\n' && *nl != '\r' )
            ++nl;
        p = nl;                                 /* advance past the line ...    */
        while( p < end && (*p == '\n' || *p == '\r') )
            ++p;                                /* ... and its terminator(s)    */
        if( nl == line )
            continue;                           /* blank line -> skip           */
        *nl = '\0';

        eq = strchr( line, '=' );               /* split NAME=VALUE             */
        if( eq == NULL )
            continue;                           /* no '=' -> not a variable     */
        *eq = '\0';
        env_name[env_count] = line;
        env_val[env_count]  = eq + 1;
        ++env_count;
    }
}

char *getenv( const char *name )
{
    int i;

    if( env_count < 0 )
        env_load();
    for( i = 0; i < env_count; ++i )
        if( strcmp( env_name[i], name ) == 0 )  /* case-sensitive, per C std    */
            return( env_val[i] );
    return( (char *)0 );                        /* not found                    */
}

char **environ = (char **)0;
