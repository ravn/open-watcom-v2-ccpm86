/* stackguard_test.c -- demonstrate the crt0 stack-overflow canary, stkfree().
 *
 * MECHANISM (small model): crt0sm.asm pre-paints the STACK segment with the
 * WC_STACK_FILL sentinel and the stack grows DOWN from stktop. stkfree()
 * (an asm helper in crt0sm.asm) scans from the bottom (stkbot) up while the
 * byte is still the sentinel and returns the count == stack HEADROOM (bytes
 * never touched). 0 means the stack was driven to its floor, i.e. it
 * overflowed into the near-heap arena just below -- the exact failure that
 * crashed UnZip's >=32 KB DEFLATE path before the stack was raised to 2 KB.
 *
 * For a precise, characteristic sentinel build the diagnostic clib with
 *     WC_STACK_BYTES=<n> WC_STACK_FILL=0A5h bash build-lib.sh
 * (0xA5 is rarely pushed, so the low-water scan is exact). This test recurses,
 * sampling stkfree() at each frame, and STOPS the moment headroom drops below
 * a safety floor -- so the canary PREVENTS the overflow instead of crashing.
 * Run it against a 512-byte-stack clib (trips early, shallow max depth) and a
 * 2048-byte-stack clib (reaches a much deeper max depth) to see the margin.
 */

#pragma aux bdos_conout = "mov cl,2" "int 0E0h" parm [dl] modify [ax bx cx dx si di es];
extern void     bdos_conout( char c );
extern unsigned stkfree( void );          /* crt0sm.asm: stack headroom, bytes */

static void puts_( const char *s ) { while ( *s ) bdos_conout( *s++ ); }
static void nl( void ) { bdos_conout( '\r' ); bdos_conout( '\n' ); }
static void hex4( unsigned v )
{
    static const char h[] = "0123456789ABCDEF";
    bdos_conout( h[(v >> 12) & 15] ); bdos_conout( h[(v >> 8) & 15] );
    bdos_conout( h[(v >>  4) & 15] ); bdos_conout( h[ v       & 15] );
}

static unsigned min_headroom;
static unsigned max_depth;

/* Each frame burns ~40 B via a volatile array the optimizer must keep, then
 * samples the canary. The `stkfree() < 48` guard is the tripwire: it unwinds
 * BEFORE the stack reaches its floor, turning a would-be crash into a clean
 * "guard tripped" report. The recursive call is deliberately NOT in tail
 * position -- we read `pad` and the returned value AFTER it -- so Watcom can't
 * fold the recursion into a loop and the stack really grows per frame. */
static unsigned burn( unsigned depth )
{
    volatile char pad[40];
    unsigned f, sub;
    pad[0] = (char)depth;
    f = stkfree();
    if ( f < min_headroom ) min_headroom = f;
    if ( depth > max_depth ) max_depth = depth;
    if ( f < 48u ) { puts_( "  [canary tripped -- unwinding before overflow]" ); nl(); return depth; }
    sub = ( depth < 4000u ) ? burn( depth + 1 ) : depth;
    pad[39] = (char)( sub + depth );        /* use pad + sub AFTER the call */
    return sub + (unsigned char)pad[39];
}

int main( void )
{
    puts_( "stack headroom at entry (bytes) = " ); hex4( stkfree() ); nl();
    min_headroom = 0xFFFFu;
    max_depth    = 0;
    burn( 1 );
    puts_( "max recursion depth reached      = " ); hex4( max_depth ); nl();
    puts_( "min headroom (low-water) seen    = " ); hex4( min_headroom ); nl();
    if ( min_headroom == 0 )
        puts_( "RESULT: OVERFLOW -- stack hit its floor" );
    else
        puts_( "RESULT: OK -- canary kept headroom above the floor" );
    nl();
    return 0;
}
