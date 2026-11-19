/* mediumtest.c -- end-to-end MEDIUM-model ("far code, near data") smoke test
 * for the CP/M-86 C library built by `MODEL=m build-lib.sh` (clibm.lib +
 * cstartmm.obj / crt0mm.asm).
 *
 * What it proves that the small-model tests cannot:
 *   (1) crt0mm's FAR-call entry contract works: the loader enters _cstart_,
 *       which FAR-calls wc_heap_init / __CommonInit / main -- if any of those
 *       calls used the wrong (near) form the program would crash immediately,
 *       so reaching main() at all already exercises it.
 *   (2) the FAR clib links and runs: printf() here goes through the whole
 *       -mm-compiled stdio path (fprtf/__prtf/fputc/__InitFiles), every step a
 *       far call/return -- empty or garbled output would mean a broken far clib.
 *   (3) cross-module FAR calls relocate: magicO..magicCR live in a SEPARATE
 *       translation unit (mediumtest_b.c), each its own *_TEXT segment under
 *       -zm, coalesced by wlink into one Code Group Descriptor. A direct call
 *       returning the right constant means the far CALL's segment was
 *       loader-relocated correctly.
 *   (4) DATA->CODE far function POINTERS relocate: fns[] holds 4-byte far
 *       pointers to those callees; calling through each and getting the right
 *       constant means the SEGMENT word stored in the data table was fixed up
 *       by the loader (the exact wlink Stage B fixup-record path).
 *
 * Deliberately checks by RETURN VALUE, not by inspecting prologue opcodes: at
 * -O0 `-mm -zm` prefixes each function with `call far ptr __STK`, so the first
 * bytes are NOT the `mov ax,imm16` the small-model byte-oracle assumed.
 *
 * Runs on genuine CP/M-86 in MAME rc759 (emu2 does NOT apply P_LOAD fixups, so
 * a medium .CMD only runs on a relocating loader). Result on console + the
 * MAME host via OUT 0x2FE: DONE-SIGNAL word 0x0008 = pass, 0x00FF = fail.
 */
#include <stdio.h>
#include "mamedone.h"

extern int magicO( void );      /* -> 0x114F, in mediumtest_b.c */
extern int magicK( void );      /* -> 0x224B */
extern int magicBang( void );   /* -> 0x3321 */
extern int magicCR( void );     /* -> 0x440D */

static int (* const fns[4])( void ) = { magicO, magicK, magicBang, magicCR };
static const int magic[4] = { 0x114F, 0x224B, 0x3321, 0x440D };

int main( void )
{
    int i;
    int fail = 0;

    /* (3) direct cross-module far calls */
    if( magicO() != 0x114F ) ++fail;
    if( magicK() != 0x224B ) ++fail;

    /* (4) far function POINTERS from a data table (DATA->CODE relocation) */
    for( i = 0; i < 4; ++i )
        if( fns[i]() != magic[i] )
            ++fail;

    /* (2) the far clib stdio path */
    printf( "medium clib: 6 far calls, %d fail\r\n", fail );
    printf( fail == 0 ? "PASS\r\n" : "FAIL\r\n" );

    mame_done( fail == 0 ? 0x0008 : 0x00FF );   /* MAME host signal (last) */
    return 0;
}
