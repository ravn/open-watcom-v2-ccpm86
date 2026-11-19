/* ehsupp.c -- setjmp/longjmp + C++ exception-handling OS seams for the CP/M-86
 * OW-clib port (rc7xx-work#9, rebased onto the contrib port per #12).
 *
 * This program links Watcom's REAL 8086 small-model setjmp/longjmp
 * (setjmp86.obj, from clib/startup/a/stjmp086.asm) and the C++ EH runtime
 * (plbxs.lib, -xs). Both bottom out on a few platform hooks that Watcom's DOS
 * clib defines in RT-data modules we deliberately do NOT link (they drag _psp /
 * _LpCmdLine / DOS-only startup). We supply exactly those hooks here.
 *
 * NAMING (Watcom small-model, __watcall): a C *function* symbol gets a trailing
 * '_' (so C `__clib_exit` -> asm `__clib_exit_`, matching clibsupp.h's link
 * name). A C *global datum* gets ONE leading '_' prepended (so C
 * `__longjmp_handler` -> asm `___longjmp_handler`, and C `_get_ovl_stack` ->
 * asm `__get_ovl_stack`, exactly the symbols setjmp86.obj/plbxs reference).
 */
#include <stdlib.h>

/* longjmp's low-level hook. Small-model longjmp does a NEAR indirect
 *   call word ptr [___longjmp_handler]
 * passing the old SP per the handler's arg convention (__parm __caller [ax dx],
 * from clib's ljmphdl.h). The correct default is a NEAR no-op PROC.
 *
 * CRITICAL (bug fixed in the scratch port, carried forward): the handler must be
 * a NEAR pointer to a NEAR proc. An earlier __far version executed `retf`
 * against longjmp's NEAR `call`, popping one extra word and unbalancing the
 * stack so longjmp "returned" into the exit path -- plain C setjmp/longjmp
 * broke while C++ EH (which OVERWRITES this pointer with its own lj_handler in
 * plbxs ljmpinit.cpp at startup) still worked, a confusing split failure. This
 * near default governs the plain C setjmp/longjmp the test programs use directly. */
typedef void (*_ljfun)( void __far * );
#pragma aux _lj_conv __parm __caller [__ax __dx]
#pragma aux (_lj_conv) _ljfun
static void _lj_default( void __far *p ) { (void)p; }
#pragma aux (_lj_conv) _lj_default
_ljfun __longjmp_handler = _lj_default;

/* Overlay-stack hooks: NULL in a non-overlay .CMD. setjmp/longjmp test them by
 * value (`or ax, word ptr __get_ovl_stack`) and skip the indirect call when
 * zero, so a null datum is the whole implementation. */
void __far *_get_ovl_stack = 0;
void __far *_restore_ovl_stack = 0;

/* C++ EH terminate path (plbxs termnate.cpp) bottoms out here -> normal exit. */
void __clib_exit( int ret_code ) { exit( ret_code ); }

/* C++ EH fatal path (plbxs fatalerr.cpp: pure-virtual call, rethrow with no
 * active exception, etc.). Signature per lib_misc/h/clibsupp.h -- a far message
 * pointer we do not surface on the console (no diagnostic path here) + an exit
 * code. Terminate the program; this is unreachable on a clean PASS run but the
 * symbol must resolve for the EH runtime to link. */
void __clib_fatal( char __far *msg, int code ) { (void)msg; exit( code ); }
