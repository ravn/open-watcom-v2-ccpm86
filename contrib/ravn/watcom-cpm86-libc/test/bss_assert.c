/*
 * bss_assert.c -- verify that crt0 BSS zeroing covers exactly the right region.
 *
 * Three assertions, all observable under emu2 -P 255:
 *
 *   ASSERT 1 (BSS clean):  every byte in uninitialized statics is 0.
 *              Fails BEFORE the crt0 fix; passes after.
 *
 *   ASSERT 2 (data intact): initialized statics retain their compile-time
 *              values. A zero-fill loop with wrong bounds could corrupt them.
 *
 *   ASSERT 3 (const intact): read-only constants (CONST segment) retain their
 *              values. A loop that runs past __bss_end into CONST would corrupt
 *              them (CONST precedes BSS in the dosseg class order — so over-
 *              running the START of the fill destroys CONST; over-running the
 *              END destroys STACK).
 *
 * ASSERT 2 and 3 guard against "off-by-one in the wrong direction" bugs in the
 * zero-fill bounds.  Together the three assertions act as a bracket test:
 * only the closed interval [__bss_start, __bss_end) is modified.
 *
 * Oracle protocol (same word-stream as bssprobe/disktest):
 *   tag 0xB551, then fail-count, then sentinel 0xE0F0.
 *   fail-count == 0 means PASS.
 *
 * Build (small model, after building clibs.lib):
 *   owcc -mcmodel=s -o BSSAS.CMD test/bss_assert.c -I port -L build-sm port/clibs.lib
 * Run:
 *   emu2 -P 255 BSSAS.CMD
 */

#include <stdio.h>
#include "mamedone.h"

/* -----------------------------------------------------------------------
 * Section A: BSS (no initializer -> _BSS segment -> must be 0 after crt0)
 *
 * volatile prevents the compiler from assuming "I never wrote here, so it
 * must still be 0" and eliding the load.  We want the actual memory read.
 * ----------------------------------------------------------------------- */
static volatile unsigned char bss_buf[1024]; /* large array forces G-Min > G-Length */
static volatile unsigned      bss_scalar;
static volatile unsigned      bss_arr[8];

/* -----------------------------------------------------------------------
 * Section B: _DATA (explicit initializer -> kept in file image -> must
 * retain its value; a zero-fill that runs too far would corrupt this)
 * ----------------------------------------------------------------------- */
static unsigned      data_magic = 0xA55A;
static unsigned char data_bytes[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

/* -----------------------------------------------------------------------
 * Section C: CONST segment (const with initializer -> read-only segment
 * that precedes BSS in the dosseg class order; a fill that starts too
 * early would corrupt this)
 * ----------------------------------------------------------------------- */
static const unsigned      const_magic = 0x5AA5;
static const unsigned char const_bytes[8] = { 11, 13, 17, 19, 23, 29, 31, 37 };

int main( void )
{
    unsigned i;
    unsigned fail = 0;

    /* ------------------------------------------------------------------
     * ASSERT 1: BSS is zero.
     * Read before any write -- the point is what crt0 left here.
     * ------------------------------------------------------------------ */
    for( i = 0; i < 1024; ++i ) {
        if( bss_buf[i] != 0 ) { ++fail; }
    }
    if( bss_scalar != 0 ) { ++fail; }
    for( i = 0; i < 8; ++i ) {
        if( bss_arr[i] != 0 ) { ++fail; }
    }

    /* ------------------------------------------------------------------
     * ASSERT 2: initialized data is intact (not corrupted by zero-fill).
     * ------------------------------------------------------------------ */
    if( data_magic != 0xA55A ) { ++fail; }
    for( i = 0; i < 8; ++i ) {
        if( data_bytes[i] != (unsigned char)(i + 1) ) { ++fail; }
    }

    /* ------------------------------------------------------------------
     * ASSERT 3: constants are intact (not corrupted by zero-fill).
     * ------------------------------------------------------------------ */
    static const unsigned char expected[8] = { 11, 13, 17, 19, 23, 29, 31, 37 };
    if( const_magic != 0x5AA5 ) { ++fail; }
    for( i = 0; i < 8; ++i ) {
        if( const_bytes[i] != expected[i] ) { ++fail; }
    }

    printf( "BSS_ASSERT: %s (%u failures)\r\n",
            fail == 0 ? "PASS" : "FAIL", fail );
    fflush( stdout );

    mame_out( 0xB551 );
    mame_out( fail );
    mame_out( 0xE0F0 );
    return( (int)fail );
}
