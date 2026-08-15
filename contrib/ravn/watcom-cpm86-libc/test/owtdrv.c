/* PASS/FAIL driver for running Open Watcom's OWN ctest float0x.c regression
 * tests UNCHANGED on CP/M-86 through the retargeted clib.
 *
 * Each floatNN.c is Watcom's own self-checking float regression test: it calls
 * fail(__LINE__) on any wrong result and its main() (renamed to owtest_main via
 * -Dmain=owtest_main on the compile, so the source stays byte-for-byte the
 * upstream original) returns errors!=0 via the fail.h _PASS macro. This driver
 * supplies the real main(): it runs the test, reads the shared `errors` counter
 * (defined by fail.h inside the test's TU), and prints a single machine-checkable
 * OWTEST verdict line the harness greps for. The tests ARE the oracle -- a PASS
 * here is upstream Watcom's own float suite validating our no-8087 soft-float.
 */
#include <stdio.h>

extern int owtest_main(void);   /* the test's own main(), renamed at compile */
extern unsigned errors;         /* defined in fail.h (test TU); ++ per failure */
/* stdout is attached by crt0 via __CommonInit (port/cominit.c, ow#16) before
 * main() runs, so no __InitFiles() call is needed here. */

#ifdef MAME_DONE
#include "mamedone.h"           /* -i=<mame-tests> supplies this (owt-mame.sh) */
#endif

int main(void)
{
    int rc;
#ifdef MAME_DONE
    mame_done(0xB000);          /* START edge for external MAME timing */
#endif
    rc = owtest_main();
    if (rc == 0 && errors == 0)
        printf("OWTEST: PASS\n");
    else
        printf("OWTEST: FAIL rc=%d errors=%u\n", rc, errors);
    fflush(stdout);
#ifdef MAME_DONE
    mame_done(0xE000);          /* END edge */
#endif
    return rc;
}
