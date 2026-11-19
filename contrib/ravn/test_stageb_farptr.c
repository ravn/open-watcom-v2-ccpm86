/* Stage B (medium model, -mm -zm) POINTER-TO-CODE-STUB relocation oracle.
 *
 * Companion source for test_stageb_farcall.sh.  This is a stronger relocation
 * check than a plain far-CALL test: instead of executing a far call and
 * trusting the return value, it takes a FAR POINTER to each code stub and
 * verifies the bytes in memory at that pointer are the stub's expected code.
 * A relocation error therefore fails as "the pointer does not point at the
 * stub", independent of whether the stub would have executed correctly.
 *
 * Each stub's body is `return 0xHHLL;`, which Watcom compiles to
 * `mov ax,0xHHLL ; retf` -- so the stub's first three code bytes are
 * B8 LL HH, a self-describing magic.  The far pointers live in a DATA table,
 * so each pointer's segment word is a FIX_BASE fixup whose LOCATION is in the
 * DATA group (fixup record nibble 0x2X, target CODE 0x_1) -- a code path that
 * the far-CALL test (all fixups located in CODE) never exercises.  cpmmain
 * follows each relocated pointer and prints the magic's low byte on a match,
 * '?' otherwise; correct relocation yields exactly "OK!\r\n".
 *
 * The magic low bytes are chosen as 'O','K','!','\n'.  The functions are
 * emitted in their own <func>_TEXT segments (via -mm -zm) at DIFFERENT image
 * paragraphs, so a linker that derives the group-relative paragraph from the
 * grp_addr.seg frame number (which increments by 1 per segment regardless of
 * size) instead of from the packed .CMD image layout points every pointer at
 * the wrong place -- the exact bug fixed 2026-08-19.
 *
 * Must be linked with test_stageb_begdata.obj FIRST so the base page is
 * reserved and the pointer table starts at DS:0100, not DS:0000 (see that
 * file).  cpmmain is the entry point (freestanding: no clib startup).
 */
extern unsigned bdos( unsigned char func, unsigned dx );
#pragma aux bdos =              \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [ax]                  \
    modify [ax bx cx dx es];

int sO( void );    int sO( void )    { return 0x114F; }   /* prologue B8 4F 11 -> 'O'  */
int sK( void );    int sK( void )    { return 0x224B; }   /* prologue B8 4B 22 -> 'K'  */
int sBang( void ); int sBang( void ) { return 0x3321; }   /* prologue B8 21 33 -> '!'  */
int sCR( void );   int sCR( void )   { return 0x440D; }   /* prologue B8 0D 44 -> '\r' */

static unsigned char __far * const stubs[4] = {
    (unsigned char __far *)sO,
    (unsigned char __far *)sK,
    (unsigned char __far *)sBang,
    (unsigned char __far *)sCR
};
static const unsigned magic[4] = { 0x114F, 0x224B, 0x3321, 0x440D };

void cpmmain( void )
{
    int i;
    for( i = 0; i < 4; ++i ) {
        unsigned char __far *p = stubs[i];
        unsigned m = magic[i];
        if( p[0] == 0xB8
         && p[1] == (unsigned char)m
         && p[2] == (unsigned char)( m >> 8 ) ) {
            bdos( 2, (unsigned char)m );        /* the stub is where the pointer says */
        } else {
            bdos( 2, '?' );                     /* mislocated far pointer             */
        }
    }
    bdos( 2, 10 );                              /* '\n' (magics already emitted O K ! CR) */
    bdos( 0, 0 );                               /* P_TERMCPM */
}
