/* Stage B (medium model, -mm -zm) far-call load-time-relocation oracle.
 *
 * Companion source for test_stageb_farcall.sh.  Four leaf functions, each in
 * its OWN <func>_TEXT segment under -mm -zm, of DELIBERATELY different sizes:
 * fK contains a loop so it spans more than one 16-byte paragraph.  cpmmain
 * (itself multi-paragraph) far-calls all four and writes each return byte via
 * BDOS C_WRITE.  The expected console output is exactly "OK!\r\n".
 *
 * This is a CORRECTNESS oracle, not a "did it run" check.  Each fN returns a
 * DISTINCT byte, so if wlink mislocates any far target's paragraph the call
 * lands in the wrong function and a WRONG byte (or a hang) results.  fK's
 * multi-paragraph body guarantees the targets packed after it sit at image
 * paragraphs > 1, which is exactly the shape of the bug fixed 2026-08-19:
 * wlink must derive a far target's group-relative paragraph from the packed
 * .CMD image layout, NOT from its grp_addr.seg frame number (which increments
 * by 1 per segment regardless of size).  With this program the two differ
 * (correct image paragraphs {5,6,7,9} vs the buggy frame deltas {1,2,3,4}),
 * so the buggy linker relocates every far call wrong and the output is not
 * "OK!\r\n".
 *
 * The `anchor` global forces a non-empty DATA group so the .CMD carries both a
 * CODE and a DATA descriptor; the Unicorn runner then enters at CS:0000 (small
 * model) rather than the 8080-model CS:0100.
 */
extern unsigned bdos( unsigned char func, unsigned dx );
#pragma aux bdos =              \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [ax]                  \
    modify [ax bx cx dx es];

char anchor = 1;

int fO( int x );
int fO( int x ) { return x + 'O'; }

int fK( int x );                /* loop body -> spans multiple paragraphs */
int fK( int x )
{
    int i;
    int s = 0;
    for( i = 0; i < x; ++i )
        s += 1;
    return s + ( 'K' - 4 );
}

int fBang( int x );
int fBang( int x ) { return x + '!'; }

int fNL( int x );
int fNL( int x ) { return ( x ^ 0 ) + '\n' - 1; }

void cpmmain( void )
{
    bdos( 2, fO( 0 ) );         /* 'O'                       */
    bdos( 2, fK( 4 ) );         /* 4 + ('K'-4)      = 'K'    */
    bdos( 2, fBang( 0 ) );      /* '!'                       */
    bdos( 2, 13 );              /* '\r' (literal)            */
    bdos( 2, fNL( 1 ) );        /* 1 + '\n' - 1     = '\n'   */
    bdos( 0, 0 );               /* P_TERMCPM                 */
}
