/* scbport.c -- stdcbench platform glue for the WATCOM-clib CP/M-86 retarget.

   This is the Watcom-clib twin of owc-drc/stdcbench/portme.c (which glued the
   SAME upstream stdcbench 0.8 sources onto Digital Research C's clears.l86).
   Here the ENTIRE C library under stdcbench -- printf/sprintf, the string and
   ctype families, malloc/free/realloc, qsort -- is Open Watcom's OWN, UNCHANGED
   clib, resolved on CP/M-86 only by our thin Layer-2 seams (stdioshim.c,
   lowlevel.c, stubs.c) over BDOS INT 0E0h. No DR C runtime, no clears.l86.

   Two things differ from portme.c:

   1. HEAP.  DR C's portme repaired an uninitialised DR heap pointer with its
      own brk(&static_arena).  We need none of that: crt0sm.asm already calls
      wc_heap_init_ (lowlevel.c) before main, seeding Watcom's genuine near-heap
      over our in-DGROUP arena.  malloc()/free()/realloc() are Watcom's own.

   2. CLOCK.  stdcbench self-times: each module loops until 8 measured seconds
      elapse (SECONDS = CLOCKS_PER_SEC*8, see c90base.c:18).  DR C's portme read
      Concurrent-CP/M BDOS T_SECONDS (fn 155); emu2 does NOT implement fn 155
      (it returns 0xFF and never fills the TOD), so under emu2 that clock never
      advances and the timed loop spins forever.  We read BDOS T_GET (fn 105 /
      0x69 "Get Date and Time") instead, which emu2 DOES implement (from the
      host wall clock) AND the real RC759 XIOS maintains -- so the same binary
      times correctly on emu2 (functional proof) and on MAME rc759 (the
      cycle-accurate score comparable to the DR C reference).

   NOTE ON SCORES.  Under emu2 the clock is the host Mac's wall clock, so the
   score reflects the Mac's speed, not the RC759 -- the emu2 run proves the
   whole benchmark EXECUTES correctly through the retargeted clib (all modules,
   correct score arithmetic via Watcom %lu).  The RC759-comparable score (vs the
   DR C reference 13) is produced by running the SAME SCB.CMD on MAME rc759,
   where the emulated CPU speed is fixed. */

#include <stdio.h>
#include "stdcbench.h"

/* Watcom's genuine stdio std streams are dead until __InitFiles attaches a
   __stream_link to each __iob entry (the static FILE has _link == NULL, and
   _FP_BASE(fp) == fp->_link->_base).  __InitFiles is DOS-free (only near-heap
   malloc), so we call it once at the top of main -- exactly as the stdio
   milestone (test/stdiotest.c) proved.  See tasks/memory/
   reference_watcom_cpm86_startup_initfini.md. */
extern void __InitFiles( void );

/* Concurrent CP/M-86 T_GET (BDOS fn 105 / 0x69): entry CL = 105, DX = DAT
   offset, DS = DAT segment; fills the DAT structure {word day since 1978-01-01;
   byte hour BCD; byte minute BCD} and returns the seconds field (BCD) in AL.
   DS already addresses DGROUP (the struct is static), the required segment in
   both memory models. */
struct dat { unsigned day; unsigned char hour, min; };

extern unsigned char t_get( struct dat *d );
#pragma aux t_get =             \
    "mov cl,105"                \
    "mov dx,bx"                 \
    "int 0E0h"                  \
    parm [bx]                   \
    value [al]                  \
    modify [ax bx cx dx];

/* 2 BCD digits packed in one byte -> binary (e.g. 0x59 -> 59). */
#define BCD2BIN(b) ((unsigned)(((b) >> 4) * 10u + ((b) & 0x0Fu)))

#ifdef MAME_DONE
/* Built with -DMAME_DONE: signal the MAME rc759 host that the run finished by
   writing the score to undecoded I/O port 0x2FE (a MAME write-tap stops the
   emulator exactly when the benchmark ends).  The port is not decoded by the
   rc759 driver, so the OUT is side-effect-free on hardware.  Identical to the
   DR C portme's mechanism, so the same scb_mame.lua harness reads it. */
extern void mame_done( unsigned status );
#pragma aux mame_done =         \
    "mov dx,02FEh"              \
    "out dx,ax"                 \
    parm [ax]                   \
    modify [dx];
#endif

stdcbench_clock_t stdcbench_clock( void )
{
    /* MUST be static (DGROUP/DS): t_get passes DS as the DAT segment and the
       BDOS writes there; a stack local would live in SS, which differs from DS
       in the large model, so the fill would land elsewhere and the loop would
       never see time advance (spin forever). */
    static struct dat d;
    unsigned char     sec;
    unsigned long     secs;

    sec  = t_get( &d );
    /* Fold day:hour:min:sec into a single monotonically increasing second
       count.  Only differences are ever used, so the large day-dominated
       magnitude is harmless under unsigned-long modular subtraction. */
    secs = ((((unsigned long)d.day * 24ul + BCD2BIN(d.hour)) * 60ul)
            + BCD2BIN(d.min)) * 60ul + BCD2BIN(sec);

    return secs * 1000ul;
}

void stdcbench_error( const char *message )
{
    printf( "ERROR: %s\n", message );
}

/* stdcbench calls abort() on internal self-check failure (never on a valid
   run); the linker still needs the symbol.  Terminate cleanly via BDOS System
   Reset (fn 0), the same exit crt0sm.asm uses. */
void abort( void )
{
    printf( "ABORT\n" );
    for( ;; ) {
        /* BDOS P_TERMCPM (fn 0): CL = 0, INT 0E0h. */
        #pragma aux bdos_exit = "xor dx,dx" "mov cl,0" "int 0E0h" modify [ax cx dx];
        extern void bdos_exit( void );
        bdos_exit();
    }
}

int main( void )
{
    unsigned long score;

    __InitFiles();

    printf( "\n%s\n", stdcbench_name_version_string );
    score = stdcbench();
#ifdef C90BASE
    printf( "stdcbench c90base score: %lu\n", c90base_score );
#endif
#ifdef C90LIB
    printf( "stdcbench c90lib score: %lu\n", c90lib_score );
#endif
    printf( "stdcbench final score: %lu\n", score );
#ifdef MAME_DONE
    mame_done( (unsigned)score );
#endif
    return 0;
}
