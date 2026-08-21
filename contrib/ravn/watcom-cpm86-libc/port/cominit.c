/* __CommonInit -- runtime initialization called by port/crt0sm.asm AFTER
 * wc_heap_init and BEFORE main(), so no program has to remember to attach the
 * stdout FILE buffer itself.
 *
 * ow#16: our minimal CP/M-86 crt0 does NOT walk Watcom's XI init table (the
 * AXIN()-registered priority chain that the real cstart runs via __InitRtns).
 * So the two library initializers the stock startup would run had to be called
 * BY HAND from every main() -- a silent-failure papercut that bit both
 * whetstone.c and owtdrv.c:
 *   __InitFiles() attaches the stdout/stderr FILE buffers from the near heap.
 *                 Without it, printf() writes to a FILE with no buffer and
 *                 SILENTLY emits nothing -- no crash, no diagnostic, just empty
 *                 output. This is the dangerous one.
 *   __setEFGfmt() repoints printf's %e/%f/%g formatter at Watcom's genuine
 *                 _EFG_Format (default is the noefgfmt.obj stub). Only needed by
 *                 builds that actually print real floats.
 * Concentrating them here means a program's main() is now just its own logic.
 *
 * crt0sm.asm is assembled ONCE and shared by all seven build targets, so it is
 * THIS translation unit (compiled per build with the target's USER flags) that
 * varies, not the startup asm. Two compile-time gates keep the minimal,
 * direct-BDOS builds from dragging in stdio they never use:
 *   -DCOMMONINIT_NOSTDIO : cprintf-only demos (test/main.c, test/heaptest.c)
 *                          that never touch FILE* stdio -> emit an empty
 *                          __CommonInit and do NOT reference __InitFiles.
 *   -DCOMMONINIT_EFG     : builds that print real floats (whetstone) -> also
 *                          install the genuine EFG formatter.
 */

#include "variety.h"        /* _WCNEAR (near/far attribute macros per model) */

#ifndef COMMONINIT_NOSTDIO
extern void _WCNEAR __InitFiles( void );    /* attach stdout/stderr FILE buffers */
#endif
#ifdef COMMONINIT_EFG
extern void _WCNEAR __setEFGfmt( void );    /* install real %e/%f/%g formatter */
#endif
#ifdef COMMONINIT_REDIRECT
extern int  __apply_redirection( int argc, char **argv ); /* port/diskio.c */
extern void __close_redirection( void );                  /* port/diskio.c */
#endif

/* Tail-call barrier. __CommonInit is FAR (crt0 does `call far ptr
   __CommonInit_`), but its callees __InitFiles/__setEFGfmt are _WCNEAR (near
   RET). With the near-code far-data models (large/medium) the optimizer would
   tail-call the LAST callee as a bare `jmp` -- then that callee's NEAR ret pops
   only IP and performs __CommonInit's far return with a stale CS, sending crt0
   to <curCS>:<retIP> (garbage) instead of BEGTEXT:<retIP>. Concretely this hung
   ZIP.CMD before main(): __InitFiles' `ret` at seg2:8D3A jumped to seg2:0011
   (mid-ask_for_split_write_path) which then fgets()'d the console forever.
   A store to this escaping volatile after the last call keeps __CommonInit from
   ending on the near callee, so it emits its own `retf`. No-op cost on the
   near-code models; harmless (a single word store) on the near-data ones. */
static volatile int __ci_tail_barrier;

void __CommonInit( void )
{
#ifndef COMMONINIT_NOSTDIO
    __InitFiles();
#endif
#ifdef COMMONINIT_EFG
    __setEFGfmt();
#endif
    __ci_tail_barrier = 0;      /* defeat far-fn -> near-fn tail-call (see above) */
}

/* __CommonRedirect -- crt0 calls this AFTER the command-tail argv parser and
   BEFORE main(). Only the disk-file builds (-DCOMMONINIT_REDIRECT, the ones that
   link port/diskio.c) actually honour shell-style < / > / >> on the command
   tail; every other build compiles this as an argc-preserving no-op so the crt0
   call resolves without dragging the FCB disk layer into a console-only .CMD. */
int __CommonRedirect( int argc, char **argv )
{
#ifdef COMMONINIT_REDIRECT
    return( __apply_redirection( argc, argv ) );
#else
    (void)argv;
    return( argc );
#endif
}

/* __CommonRedirectClose -- crt0 calls this AFTER main() returns, to flush and
   commit any redirected stdout file. No-op unless redirection is compiled in. */
void __CommonRedirectClose( void )
{
#ifdef COMMONINIT_REDIRECT
    __close_redirection();
#endif
}
