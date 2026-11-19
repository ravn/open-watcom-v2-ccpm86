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
 * getenv()     : CP/M-86 has NO environment block at all, so a lookup ALWAYS
 *                fails -- getenv() correctly returns NULL (C standard: NULL
 *                when the name is not found).  We implement it here rather
 *                than linking Watcom's getenv.c because that one performs a
 *                locale-correct multibyte (DBCS) name comparison and so pulls
 *                the whole __mbsnextc/__ismbblead codepage subsystem in -- pure
 *                dead weight against the hard 64 KB single-code-segment ceiling
 *                on a system that can never have a variable to compare.
 * environ      : the (empty) environment vector, kept defined for the few
 *                programs that reference the symbol directly.
 */

#include <stddef.h>

/* Forward prototypes for this seam's own entry points (clib -we: no implicit
   function declarations). */
int   setmode( int handle, int mode );
void (*signal( int sig, void (*func)( int ) ))( int );
char *getenv( const char *name );

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

/* CP/M-86 has no environment block; every lookup fails. */
char *getenv( const char *name )
{
    (void)name;
    return( (char *)0 );
}

char **environ = (char **)0;
