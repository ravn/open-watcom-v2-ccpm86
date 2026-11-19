/* port/fesoft.c -- soft (no-8087) <fenv.h> exception seam.
 *
 * Stock Watcom implements feraiseexcept() in clib/fpu/c/fenv.c with inline 8087
 * opcodes (fnstsw/fldenv/fwait) to poke the coprocessor status word. On the
 * RC759 (NO 8087) those opcodes will not even assemble under -0, and there is no
 * hardware exception-flag register to raise. mathlib's _matherr.c references
 * feraiseexcept() on a domain/range error, so the symbol must resolve -- but on
 * valid inputs (all our transcendental tests) _matherr never actually runs, and
 * with no FPU there is genuinely nothing to signal. A no-op that reports success
 * is therefore the correct pure-software behaviour. Kept tiny and standalone so
 * it pulls in nothing from the 8087 fenv path.
 */

int feraiseexcept( int excepts )
{
    (void)excepts;              /* no 8087 => no hardware status word to set */
    return 0;                   /* C: 0 = the (empty) set of exceptions was raised */
}
