/* CP/M-86 console glue for the Aztec-libc bring-up (ravn/open-watcom-v2#13).
 *
 * Provides putchar() directly on top of the BDOS console-output call so that
 * Aztec's stdlib puts.c -- recompiled by Watcom wcc -- can run WITHOUT first
 * bringing up Aztec's full FILE/channel/buffered-stdio subsystem (that whole
 * subsystem is the later milestone; it was also the tarpit that broke the DR C
 * runtime, see #12).  This file is OUR glue, not Aztec source, so it is
 * committable; the Aztec puts.c stays uncommitted under ../src/.
 *
 * BDOS gateway: CP/M-86 enters BDOS via INT 0E0h, function in CL, argument in
 * DX, result in AL (same convention as cpm86-clib/cpmsys.h in the #10 work).
 */
/* Provenance stamp -- makes every linked .CMD self-identify its origin so that
 * herkomst can be proven with `strings foo.cmd | grep @PROV` instead of a
 * falsification test.  The two vendors differ here and both are recorded:
 *   - source vendor  = Aztec/Manx  (this libc is recompiled Aztec stdlib source)
 *   - compiler vendor = Open Watcom (__WATCOMC__ stringized at build time; the
 *     value is 1300 for OW 2.0, proving wcc -- not Aztec's own cc -- built it).
 * cpm86_glue.obj is always in the load image (it supplies putchar), and wcc
 * emits this const into the CONST segment of DGROUP, so the bytes land in the
 * .CMD data group and survive without any dead-strip guard.  Example: an OW-2.0
 * build yields the literal "@PROV:aztec-src+wcc1300" in the binary. */
#define PROV_STR2(x) #x
#define PROV_STR(x)  PROV_STR2(x)
const char provenance_stamp[] = "@PROV:aztec-src+wcc" PROV_STR(__WATCOMC__);

extern unsigned char _bdos( unsigned char func, unsigned param );
#pragma aux _bdos =             \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [al]                  \
    modify [ax bx cx dx es];

#define BDOS_CONOUT 2           /* DL = character to write */

int putchar( int c )
{
    if( c == '\n' )                     /* CP/M console needs CR before LF */
        _bdos( BDOS_CONOUT, '\r' );
    _bdos( BDOS_CONOUT, (unsigned char)c );
    return c;
}
