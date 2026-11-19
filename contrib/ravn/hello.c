/****************************************************************************
*
*   Freestanding CP/M-86 "hello world" for Open Watcom (wcc, tiny model).
*
*   CP/M-86 has no C runtime under Open Watcom, so this program does NOT use
*   the standard library. It talks to the operating system directly through
*   BDOS, which on CP/M-86 is reached via software interrupt 224 (0E0h):
*
*       CL = function number, DX = parameter, result in AL/AX.
*
*   Functions used here (Digital Research CP/M-86 System Guide, BDOS calls):
*       9  C_WRITESTR   print '$'-terminated string at DS:DX
*       0  P_TERMCPM    terminate program, return to CCP
*
*   Build with small model (-ms) so code+data share one segment, matching the
*   CP/M-86 "8080 model" (single relocatable group) emitted by wl format cpm86.
*
****************************************************************************/

/*
 * BDOS entry via INT 0E0h. wcc auxiliary pragma: pass function in CL and the
 * DX parameter, clobber the usual scratch registers. Value returned in AX.
 */
extern unsigned bdos( unsigned char func, unsigned dx );
#pragma aux bdos =              \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [ax]                  \
    modify [ax bx cx dx es];

#define BDOS_WRITESTR   9
#define BDOS_TERMCPM    0

static char message[] = "Hello, CP/M-86 from Open Watcom!\r\n$";

/*
 * C entry point. The cpmstart.asm startup stub (linked first by
 * build-cpm86.sh) is what actually sits at CS:0100H; it calls this function
 * (wcc __watcall name: cpmmain_) and terminates via BDOS when we return. In
 * the small/8080 model CS=DS holds for wcc-generated code, so 'message' is
 * reachable via DS:DX.
 */
void cpmmain( void )
{
    bdos( BDOS_WRITESTR, (unsigned)message );
    bdos( BDOS_TERMCPM, 0 );
}
