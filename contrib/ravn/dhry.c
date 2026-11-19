/****************************************************************************
*
*   Dhrystone 2.1 benchmark, ported to *freestanding* CP/M-86 (Open Watcom).
*
*   This is a real, non-trivial C program -- structs, unions, enums, pointer
*   chasing, string handling and many cross-function calls -- built with the
*   native `wl format cpm86` linker route (the former raw+bin2cmd pipeline was
*   retired) and run under the CP/M-86 emulators in this folder.
*
*   CP/M-86 gives us no C runtime, so everything the standard Dhrystone relies
*   on from libc is provided here directly:
*
*       printf/scanf  -> putstr()/putdec()/putch() over BDOS INT 0E0h, and a
*                        fixed NUMBER_OF_RUNS (no interactive input)
*       malloc        -> a static bump allocator (alloc_rec)
*       strcpy/strcmp -> freestanding implementations below
*       times()/clock -> removed; an instruction-level emulator has no wall
*                        clock, so instead of a bogus timing we run a fixed
*                        number of iterations and *verify* the benchmark's
*                        documented final values (that is the real proof the
*                        generated 16-bit code executed correctly).
*
*   The benchmark procedures (Proc_1..Proc_8, Func_1..Func_3) are kept
*   faithful to Reinhold P. Weicker's Dhrystone 2.1 so the published expected
*   results still apply.
*
*   Original benchmark: (c) 1988 Reinhold P. Weicker.
*
****************************************************************************/

/* ---- freestanding BDOS console output (CP/M-86, INT 0E0h) --------------- */

/* BDOS entry: CL = function, DX = parameter, result in AX (see hello.c). */
extern unsigned bdos( unsigned char func, unsigned dx );
#pragma aux bdos =              \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [ax]                  \
    modify [ax bx cx dx es];

#define BDOS_CONOUT   2         /* C_WRITE: print char in DL */
#define BDOS_TERMCPM  0         /* P_TERMCPM: terminate      */

static void putch( char c )
{
    if( c == '\n' )
        bdos( BDOS_CONOUT, '\r' );      /* CP/M console wants CR + LF */
    bdos( BDOS_CONOUT, (unsigned char)c );
}

static void putstr( char *s )
{
    while( *s )
        putch( *s++ );
}

static void putdec( int n )
{
    char buf[7];
    int  i = 0;
    unsigned u;

    if( n < 0 ) {
        putch( '-' );
        u = (unsigned)(-n);
    } else {
        u = (unsigned)n;
    }
    do {
        buf[i++] = (char)('0' + (u % 10));
        u /= 10;
    } while( u );
    while( i )
        putch( buf[--i] );
}

/* ---- freestanding string helpers --------------------------------------- */

char *strcpy( char *d, char *s )
{
    char *r = d;
    while( (*d++ = *s++) != 0 )
        ;
    return r;
}

int strcmp( char *a, char *b )
{
    while( *a && *a == *b ) {
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* ---- static allocator (replaces malloc) -------------------------------- */

static char  Mem_Pool[256];
static unsigned Mem_Used = 0;

/* ---- Dhrystone types (from dhry.h) ------------------------------------- */

typedef enum { Ident_1, Ident_2, Ident_3, Ident_4, Ident_5 } Enumeration;

typedef int  One_Thirty;
typedef int  One_Fifty;
typedef char Capital_Letter;
typedef int  Boolean;
typedef char Str_30[31];
typedef int  Arr_1_Dim[50];
typedef int  Arr_2_Dim[50][50];

typedef struct record {
    struct record *Ptr_Comp;
    Enumeration    Discr;
    union {
        struct { Enumeration Enum_Comp; int Int_Comp; char Str_Comp[31]; } var_1;
        struct { Enumeration E_Comp_2;  char Str_2_Comp[31]; }            var_2;
        struct { char Ch_1_Comp; char Ch_2_Comp; }                       var_3;
    } variant;
} Rec_Type, *Rec_Pointer;

#define Null  0
#define true  1
#define false 0

static Rec_Pointer alloc_rec( void )
{
    Rec_Pointer p = (Rec_Pointer)&Mem_Pool[Mem_Used];
    Mem_Used += (sizeof(Rec_Type) + 1) & ~1u;
    return p;
}

/* ---- Global Variables (from dhry_1.c) ---------------------------------- */

Rec_Pointer Ptr_Glob, Next_Ptr_Glob;
int         Int_Glob;
Boolean     Bool_Glob;
char        Ch_1_Glob, Ch_2_Glob;
int         Arr_1_Glob[50];
int         Arr_2_Glob[50][50];

/* forward declarations */
Enumeration Func_1( Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val );
Boolean     Func_2( Str_30 Str_1_Par_Ref, Str_30 Str_2_Par_Ref );
Boolean     Func_3( Enumeration Enum_Par_Val );
void Proc_1( Rec_Pointer Ptr_Val_Par );
void Proc_2( One_Fifty *Int_Par_Ref );
void Proc_3( Rec_Pointer *Ptr_Ref_Par );
void Proc_4( void );
void Proc_5( void );
void Proc_6( Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par );
void Proc_7( One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref );
void Proc_8( Arr_1_Dim Arr_1_Par_Ref, Arr_2_Dim Arr_2_Par_Ref,
             int Int_1_Par_Val, int Int_2_Par_Val );

#define NUMBER_OF_RUNS 20000

/* ---- benchmark body (from dhry_1.c main) ------------------------------- */

static void dhry_run( void )
{
    One_Fifty   Int_1_Loc;
    One_Fifty   Int_2_Loc;
    One_Fifty   Int_3_Loc;
    char        Ch_Index;
    Enumeration Enum_Loc;
    Str_30      Str_1_Loc;
    Str_30      Str_2_Loc;
    int         Run_Index;
    int         Number_Of_Runs;

    Next_Ptr_Glob = alloc_rec();
    Ptr_Glob      = alloc_rec();

    Ptr_Glob->Ptr_Comp                = Next_Ptr_Glob;
    Ptr_Glob->Discr                   = Ident_1;
    Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
    Ptr_Glob->variant.var_1.Int_Comp  = 40;
    strcpy( Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING" );
    strcpy( Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING" );

    Arr_2_Glob[8][7] = 10;

    putstr( "\n" );
    putstr( "Dhrystone Benchmark, Version 2.1 (Language: C)\n" );
    putstr( "\n" );
    putstr( "Program compiled without 'register' attribute\n" );
    putstr( "\n" );

    Number_Of_Runs = NUMBER_OF_RUNS;
    putstr( "Execution starts, " );
    putdec( Number_Of_Runs );
    putstr( " runs through Dhrystone\n" );

    for( Run_Index = 1; Run_Index <= Number_Of_Runs; ++Run_Index ) {

        Proc_5();
        Proc_4();
        Int_1_Loc = 2;
        Int_2_Loc = 3;
        strcpy( Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING" );
        Enum_Loc  = Ident_2;
        Bool_Glob = !Func_2( Str_1_Loc, Str_2_Loc );
        while( Int_1_Loc < Int_2_Loc ) {
            Int_3_Loc = 5 * Int_1_Loc - Int_2_Loc;
            Proc_7( Int_1_Loc, Int_2_Loc, &Int_3_Loc );
            Int_1_Loc += 1;
        }
        Proc_8( Arr_1_Glob, Arr_2_Glob, Int_1_Loc, Int_3_Loc );
        Proc_1( Ptr_Glob );
        for( Ch_Index = 'A'; Ch_Index <= Ch_2_Glob; ++Ch_Index ) {
            if( Enum_Loc == Func_1( Ch_Index, 'C' ) ) {
                Proc_6( Ident_1, &Enum_Loc );
                strcpy( Str_2_Loc, "DHRYSTONE PROGRAM, 3'RD STRING" );
                Int_2_Loc = Run_Index;
                Int_Glob  = Run_Index;
            }
        }
        Int_2_Loc = Int_2_Loc * Int_1_Loc;
        Int_1_Loc = Int_2_Loc / Int_3_Loc;
        Int_2_Loc = 7 * (Int_2_Loc - Int_3_Loc) - Int_1_Loc;
        Proc_2( &Int_1_Loc );
    }

    putstr( "Execution ends\n" );
    putstr( "\n" );
    putstr( "Final values of the variables used in the benchmark:\n" );
    putstr( "\n" );
    putstr( "Int_Glob:            " );      putdec( Int_Glob );        putstr( "\n" );
    putstr( "        should be:   5\n" );
    putstr( "Bool_Glob:           " );      putdec( Bool_Glob );       putstr( "\n" );
    putstr( "        should be:   1\n" );
    putstr( "Ch_1_Glob:           " );      putch( Ch_1_Glob );        putstr( "\n" );
    putstr( "        should be:   A\n" );
    putstr( "Ch_2_Glob:           " );      putch( Ch_2_Glob );        putstr( "\n" );
    putstr( "        should be:   B\n" );
    putstr( "Arr_1_Glob[8]:       " );      putdec( Arr_1_Glob[8] );   putstr( "\n" );
    putstr( "        should be:   7\n" );
    putstr( "Arr_2_Glob[8][7]:    " );      putdec( Arr_2_Glob[8][7] );putstr( "\n" );
    putstr( "        should be:   " );      putdec( Number_Of_Runs + 10 ); putstr( "\n" );
    putstr( "Ptr_Glob->\n" );
    putstr( "  Ptr_Comp:          " );      putdec( (int)Ptr_Glob->Ptr_Comp ); putstr( "\n" );
    putstr( "        should be:   (implementation-dependent)\n" );
    putstr( "  Discr:             " );      putdec( Ptr_Glob->Discr ); putstr( "\n" );
    putstr( "        should be:   0\n" );
    putstr( "  Enum_Comp:         " );      putdec( Ptr_Glob->variant.var_1.Enum_Comp ); putstr( "\n" );
    putstr( "        should be:   2\n" );
    putstr( "  Int_Comp:          " );      putdec( Ptr_Glob->variant.var_1.Int_Comp );  putstr( "\n" );
    putstr( "        should be:   17\n" );
    putstr( "  Str_Comp:          " );      putstr( Ptr_Glob->variant.var_1.Str_Comp );  putstr( "\n" );
    putstr( "        should be:   DHRYSTONE PROGRAM, SOME STRING\n" );
    putstr( "Next_Ptr_Glob->\n" );
    putstr( "  Ptr_Comp:          " );      putdec( (int)Next_Ptr_Glob->Ptr_Comp ); putstr( "\n" );
    putstr( "        should be:   (implementation-dependent), same as above\n" );
    putstr( "  Discr:             " );      putdec( Next_Ptr_Glob->Discr ); putstr( "\n" );
    putstr( "        should be:   0\n" );
    putstr( "  Enum_Comp:         " );      putdec( Next_Ptr_Glob->variant.var_1.Enum_Comp ); putstr( "\n" );
    putstr( "        should be:   1\n" );
    putstr( "  Int_Comp:          " );      putdec( Next_Ptr_Glob->variant.var_1.Int_Comp );  putstr( "\n" );
    putstr( "        should be:   18\n" );
    putstr( "  Str_Comp:          " );      putstr( Next_Ptr_Glob->variant.var_1.Str_Comp );  putstr( "\n" );
    putstr( "        should be:   DHRYSTONE PROGRAM, SOME STRING\n" );
    putstr( "Int_1_Loc:           " );      putdec( Int_1_Loc );  putstr( "\n" );
    putstr( "        should be:   5\n" );
    putstr( "Int_2_Loc:           " );      putdec( Int_2_Loc );  putstr( "\n" );
    putstr( "        should be:   13\n" );
    putstr( "Int_3_Loc:           " );      putdec( Int_3_Loc );  putstr( "\n" );
    putstr( "        should be:   7\n" );
    putstr( "Enum_Loc:            " );      putdec( Enum_Loc );   putstr( "\n" );
    putstr( "        should be:   1\n" );
    putstr( "Str_1_Loc:           " );      putstr( Str_1_Loc );  putstr( "\n" );
    putstr( "        should be:   DHRYSTONE PROGRAM, 1'ST STRING\n" );
    putstr( "Str_2_Loc:           " );      putstr( Str_2_Loc );  putstr( "\n" );
    putstr( "        should be:   DHRYSTONE PROGRAM, 2'ND STRING\n" );
    putstr( "\n" );
}

/* ---- benchmark procedures (from dhry_1.c / dhry_2.c) ------------------- */

void Proc_1( Rec_Pointer Ptr_Val_Par )
{
    Rec_Pointer Next_Record = Ptr_Val_Par->Ptr_Comp;

    *Ptr_Val_Par->Ptr_Comp = *Ptr_Glob;
    Ptr_Val_Par->variant.var_1.Int_Comp = 5;
    Next_Record->variant.var_1.Int_Comp = Ptr_Val_Par->variant.var_1.Int_Comp;
    Next_Record->Ptr_Comp = Ptr_Val_Par->Ptr_Comp;
    Proc_3( &Next_Record->Ptr_Comp );
    if( Next_Record->Discr == Ident_1 ) {
        Next_Record->variant.var_1.Int_Comp = 6;
        Proc_6( Ptr_Val_Par->variant.var_1.Enum_Comp,
                &Next_Record->variant.var_1.Enum_Comp );
        Next_Record->Ptr_Comp = Ptr_Glob->Ptr_Comp;
        Proc_7( Next_Record->variant.var_1.Int_Comp, 10,
                &Next_Record->variant.var_1.Int_Comp );
    } else {
        *Ptr_Val_Par = *Ptr_Val_Par->Ptr_Comp;
    }
}

void Proc_2( One_Fifty *Int_Par_Ref )
{
    One_Fifty   Int_Loc;
    Enumeration Enum_Loc;

    Int_Loc = *Int_Par_Ref + 10;
    do {
        if( Ch_1_Glob == 'A' ) {
            Int_Loc -= 1;
            *Int_Par_Ref = Int_Loc - Int_Glob;
            Enum_Loc = Ident_1;
        }
    } while( Enum_Loc != Ident_1 );
}

void Proc_3( Rec_Pointer *Ptr_Ref_Par )
{
    if( Ptr_Glob != Null )
        *Ptr_Ref_Par = Ptr_Glob->Ptr_Comp;
    Proc_7( 10, Int_Glob, &Ptr_Glob->variant.var_1.Int_Comp );
}

void Proc_4( void )
{
    Boolean Bool_Loc;

    Bool_Loc  = Ch_1_Glob == 'A';
    Bool_Glob = Bool_Loc | Bool_Glob;
    Ch_2_Glob = 'B';
}

void Proc_5( void )
{
    Ch_1_Glob = 'A';
    Bool_Glob = false;
}

void Proc_6( Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par )
{
    *Enum_Ref_Par = Enum_Val_Par;
    if( !Func_3( Enum_Val_Par ) )
        *Enum_Ref_Par = Ident_4;
    switch( Enum_Val_Par ) {
    case Ident_1: *Enum_Ref_Par = Ident_1; break;
    case Ident_2: *Enum_Ref_Par = (Int_Glob > 100) ? Ident_1 : Ident_4; break;
    case Ident_3: *Enum_Ref_Par = Ident_2; break;
    case Ident_4: break;
    case Ident_5: *Enum_Ref_Par = Ident_3; break;
    }
}

void Proc_7( One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref )
{
    One_Fifty Int_Loc;

    Int_Loc = Int_1_Par_Val + 2;
    *Int_Par_Ref = Int_2_Par_Val + Int_Loc;
}

void Proc_8( Arr_1_Dim Arr_1_Par_Ref, Arr_2_Dim Arr_2_Par_Ref,
             int Int_1_Par_Val, int Int_2_Par_Val )
{
    One_Fifty Int_Index;
    One_Fifty Int_Loc;

    Int_Loc = Int_1_Par_Val + 5;
    Arr_1_Par_Ref[Int_Loc]      = Int_2_Par_Val;
    Arr_1_Par_Ref[Int_Loc + 1]  = Arr_1_Par_Ref[Int_Loc];
    Arr_1_Par_Ref[Int_Loc + 30] = Int_Loc;
    for( Int_Index = Int_Loc; Int_Index <= Int_Loc + 1; ++Int_Index )
        Arr_2_Par_Ref[Int_Loc][Int_Index] = Int_Loc;
    Arr_2_Par_Ref[Int_Loc][Int_Loc - 1] += 1;
    Arr_2_Par_Ref[Int_Loc + 20][Int_Loc] = Arr_1_Par_Ref[Int_Loc];
    Int_Glob = 5;
}

Enumeration Func_1( Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val )
{
    Capital_Letter Ch_1_Loc;
    Capital_Letter Ch_2_Loc;

    Ch_1_Loc = Ch_1_Par_Val;
    Ch_2_Loc = Ch_1_Loc;
    if( Ch_2_Loc != Ch_2_Par_Val )
        return Ident_1;
    Ch_1_Glob = Ch_1_Loc;
    return Ident_2;
}

Boolean Func_2( Str_30 Str_1_Par_Ref, Str_30 Str_2_Par_Ref )
{
    One_Thirty     Int_Loc;
    Capital_Letter Ch_Loc;

    Int_Loc = 2;
    Ch_Loc = 0;
    while( Int_Loc <= 2 ) {
        if( Func_1( Str_1_Par_Ref[Int_Loc], Str_2_Par_Ref[Int_Loc + 1] ) == Ident_1 ) {
            Ch_Loc = 'A';
            Int_Loc += 1;
        }
    }
    if( Ch_Loc >= 'W' && Ch_Loc < 'Z' )
        Int_Loc = 7;
    if( Ch_Loc == 'R' )
        return true;
    if( strcmp( Str_1_Par_Ref, Str_2_Par_Ref ) > 0 ) {
        Int_Loc += 7;
        Int_Glob = Int_Loc;
        return true;
    }
    return false;
}

Boolean Func_3( Enumeration Enum_Par_Val )
{
    Enumeration Enum_Loc;

    Enum_Loc = Enum_Par_Val;
    return Enum_Loc == Ident_3 ? true : false;
}

/* ---- entry point ------------------------------------------------------- */
/* Called by the cpmstart.asm startup stub (see build-cpm86.sh, C path). The
 * stub also issues BDOS P_TERMCPM when we return, so an explicit terminate is
 * not required, but we keep one so the program is correct on its own. */

void cpmmain( void )
{
    dhry_run();
    bdos( BDOS_TERMCPM, 0 );
}
