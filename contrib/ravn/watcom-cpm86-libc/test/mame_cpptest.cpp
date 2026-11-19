#include <iostream>
#include <setjmp.h>

extern "C" {
#include "mamedone.h"
}

/* Combined RC759/MAME acceptance test for the C++ additions (issue #9):
 * exceptions + setjmp/longjmp get a NUMERIC self-check (pass/fail counted and
 * reported to the host via mame_done over port 0x2FE, caught by done_signal.lua);
 * iostreams gets the VISUAL screen oracle -- the cout lines below are rendered on
 * the RC759 text screen and captured in the exit snapshot. Deterministic; the
 * OUT to 0x2FE is undecoded (harmless) and also a no-op under emu2. */

static int pass = 0, fail = 0;
static void ck( const char *name, int ok )
{
    if( ok ) { pass++; std::cout << "OK   " << name << std::endl; }
    else     { fail++; std::cout << "FAIL " << name << std::endl; }
}

/* --- exceptions --- */
static int unwind_count = 0;
struct Res { ~Res() { unwind_count++; } };
static void thrower() { Res r; throw 42; }

/* --- setjmp/longjmp --- */
static jmp_buf jb;
static void c_level( int v ) { char pad[40]; pad[0] = (char)v; longjmp( jb, pad[0] ); }
static void b_level( int v ) { c_level( v ); }
static void a_level( int v ) { b_level( v ); }

int main()
{
    int r;

    std::cout << "== C++ on CP/M-86 (issue #9) ==" << std::endl;

    /* exceptions: caught value + stack unwinding ran the local dtor */
    unwind_count = 0;
    int caught = -1;
    try { Res outer; thrower(); }
    catch( int e ) { caught = e; }
    ck( "throw/catch value", caught == 42 );
    ck( "unwind ran 2 dtors", unwind_count == 2 );

    /* setjmp value pass-through */
    if( ( r = setjmp( jb ) ) == 0 ) longjmp( jb, 7 );
    ck( "setjmp returns 7", r == 7 );

    /* longjmp(0) observed as 1 */
    if( ( r = setjmp( jb ) ) == 0 ) longjmp( jb, 0 );
    ck( "longjmp(0)->1", r == 1 );

    /* cross-function longjmp through 3 frames */
    if( ( r = setjmp( jb ) ) == 0 ) a_level( 42 );
    ck( "cross-frame longjmp", r == 42 );

    /* re-entrant loop + volatile persistence */
    {
        volatile int i = 0; volatile long sum = 0;
        setjmp( jb );
        if( i < 5 ) { sum += i; i++; longjmp( jb, 1 ); }
        ck( "reentrant loop sum", i == 5 && sum == 10 );
    }

    /* iostreams formatting (visual oracle on screen) */
    std::cout << "ios: dec=" << 255 << " hex=" << std::hex << 255
              << std::dec << " long=" << 100000L << std::endl;

    std::cout << "RESULT: " << pass << " pass " << fail << " fail" << std::endl;

    mame_done( (unsigned)( ( pass & 0xFF ) | ( ( fail & 0xFF ) << 8 ) ) );
    return fail ? 1 : 0;
}
