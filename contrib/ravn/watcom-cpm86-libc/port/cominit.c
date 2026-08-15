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

#ifndef COMMONINIT_NOSTDIO
extern void __InitFiles( void );    /* attach stdout/stderr FILE buffers */
#endif
#ifdef COMMONINIT_EFG
extern void __setEFGfmt( void );    /* install real %e/%f/%g formatter */
#endif

void __CommonInit( void )
{
#ifndef COMMONINIT_NOSTDIO
    __InitFiles();
#endif
#ifdef COMMONINIT_EFG
    __setEFGfmt();
#endif
}
