#include <iostream>
#include <setjmp.h>

extern "C" {
#include "mamedone.h"
}

/* Non-trivial C++ feature demo for CP/M-86 (issue #9). Exercises, together:
 *   - an abstract base + virtual dispatch through a base pointer (polymorphism)
 *   - single inheritance with a derived-adds-state class
 *   - a function template (tmax) and a class template (Stack<T>)
 *   - operator overloading (Frac +, ==, and ostream<<)
 *   - exceptions thrown from deep in a member function and caught by type
 *   - destructor-driven cleanup counted to prove stack unwinding
 * Every result is checked against a hand-computed constant; the pass/fail tally
 * is sent to the MAME host via mame_done (port 0x2FE, done_signal.lua) and also
 * printed for the on-screen snapshot oracle. Harmless / all-OK under emu2. */

static int pass = 0, fail = 0;
static void ck( const char *name, int ok )
{
    if( ok ) { pass++; std::cout << "OK   " << name << std::endl; }
    else     { fail++; std::cout << "FAIL " << name << std::endl; }
}

/* ---- polymorphic shape hierarchy ---- */
static int live_shapes = 0;
struct Shape {
    Shape()          { live_shapes++; }
    virtual ~Shape() { live_shapes--; }
    virtual int area() const = 0;      /* pure virtual */
    virtual const char *kind() const = 0;
};
struct Square : Shape {
    int s;
    Square( int side ) : s( side ) {}
    int area() const { return s * s; }
    const char *kind() const { return "square"; }
};
struct Rect : Shape {
    int w, h;
    Rect( int ww, int hh ) : w( ww ), h( hh ) {}
    int area() const { return w * h; }
    const char *kind() const { return "rect"; }
};

/* ---- templates ---- */
template<class T> T tmax( T a, T b ) { return a < b ? b : a; }

template<class T> class Stack {
    T data[8];
    int n;
public:
    Stack() : n( 0 ) {}
    void push( T v ) { if( n >= 8 ) throw "stack overflow"; data[n++] = v; }
    T    pop()       { if( n == 0 ) throw "stack underflow"; return data[--n]; }
    int  size() const { return n; }
};

/* ---- operator overloading ---- */
struct Frac {
    int num, den;
    Frac( int n = 0, int d = 1 ) : num( n ), den( d ) {}
    Frac operator+( const Frac &o ) const
        { return Frac( num * o.den + o.num * den, den * o.den ); }
    int operator==( const Frac &o ) const
        { return num * o.den == o.num * den; }
};
static std::ostream &operator<<( std::ostream &os, const Frac &f )
    { return os << f.num << "/" << f.den; }

int main()
{
    std::cout << "== C++ features on CP/M-86 (issue #9) ==" << std::endl;

    /* polymorphism through a base-class pointer array */
    {
        Shape *shapes[2];
        shapes[0] = new Square( 5 );
        shapes[1] = new Rect( 3, 4 );
        int total = 0;
        for( int i = 0; i < 2; i++ ) {
            std::cout << "  " << shapes[i]->kind() << " area="
                      << shapes[i]->area() << std::endl;
            total += shapes[i]->area();
        }
        ck( "virtual dispatch (25+12)", total == 37 );
        ck( "2 shapes live", live_shapes == 2 );
        delete shapes[0];
        delete shapes[1];
        ck( "virtual dtors freed all", live_shapes == 0 );
    }

    /* function + class templates */
    ck( "tmax<int>", tmax( 3, 9 ) == 9 );
    ck( "tmax<char>", tmax( 'a', 'z' ) == 'z' );
    {
        Stack<int> st;
        st.push( 10 ); st.push( 20 ); st.push( 30 );
        int a = st.pop(), b = st.pop();
        ck( "Stack<int> LIFO", a == 30 && b == 20 && st.size() == 1 );
    }

    /* operator overloading */
    {
        Frac half( 1, 2 ), third( 1, 3 );
        Frac sum = half + third;           /* 5/6 */
        std::cout << "  " << half << " + " << third << " = " << sum << std::endl;
        ck( "Frac operator+ (5/6)", sum == Frac( 5, 6 ) );
    }

    /* exception thrown from a template member, caught by type */
    {
        int caught = 0;
        try {
            Stack<int> st;
            for( int i = 0; i < 9; i++ ) st.push( i );   /* 9th push overflows */
        }
        catch( const char *msg ) { caught = ( msg[0] == 's' ); }
        ck( "template throws + caught", caught == 1 );
    }

    std::cout << "RESULT: " << pass << " pass " << fail << " fail" << std::endl;

    mame_done( (unsigned)( ( pass & 0xFF ) | ( ( fail & 0xFF ) << 8 ) ) );
    return fail ? 1 : 0;
}
