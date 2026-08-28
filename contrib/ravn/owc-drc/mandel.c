/*
 * mandel.c -- 79x25 ASCII Mandelbrot set, fixed-point 8.8 arithmetic.
 *
 * Ported verbatim (computation untouched) from the llvm-z80 test-gen example
 * z80-utils/test-gen/examples/mandelbrot.c so the CP/M-86 result can be
 * compared against the Z80 backend's.  The only edits are C89 hygiene so the
 * genuine Digital Research C v1.11 compiler (April 1984, strict K&R/C89, no
 * mid-block declarations) accepts the SAME source Open Watcom compiles:
 *
 *   * every local is declared at the top of its block (DR C rejects a
 *     declaration that follows a statement, e.g. the original `int tmp`
 *     after the `if (...) break;`);
 *   * entry is cmain, not main -- owcrt.asm bridges DR C's "main" call to it
 *     (Open Watcom special-cases the name "main"; -Dmain=cmain avoids that).
 *
 * Output is deterministic (no timing, no input): 25 lines x 79 columns of the
 * escape-count glyphs, so DR C and every Open Watcom variant must produce
 * byte-identical output -- that identity is the correctness oracle.
 * 79 columns (not 80): the RC759 Piccoline CRT auto-wraps at column 80, so a
 * full 80-char line followed by \r\n would produce a spurious blank line.  79
 * clips the right edge of the Mandelbrot plot by one pixel (negligible for an
 * ASCII art render) but displays correctly on all terminals.
 */
int putchar();          /* K&R decl: DR C v1.11 predates ANSI prototypes */

/* Fixed-point 8.8: 1.0 == 256, fits in 16-bit int for the [-2,2] range. */
#define FP_SHIFT 8
#define FP_ONE   (1 << FP_SHIFT)              /* 256 */
#define FP_MUL(a, b) ((int)((long)(a) * (b) >> FP_SHIFT))

int main()              /* K&R definition: no (void) prototype in DR C v1.11.
                           OW build remaps main->cmain (-Dmain=cmain) and
                           bridges it via owcrt.asm; the genuine DR C build
                           uses this "main" directly as its run-time entry. */
{
    int py, px;
    for (py = 0; py < 25; py++) {
        for (px = 0; px < 79; px++) {
            /* Map pixel to the complex plane:
               x in [-2.0, +0.5], y in [-1.25, +1.25], both in 8.8.
               cr uses px*8 (== px*640/80): the literal px*640 reaches 50560 at
               px=79, which overflows 16-bit int (>32767 from px=52 on) and
               wraps negative, corrupting the right third of the picture -- the
               llvm-z80 test-gen port introduced this by dropping the RC700
               original's overflow-safe (long) mapping.  py*640 (max 15360 at
               py=24) does NOT overflow, and 640/25 isn't integral, so ci keeps
               the divide. */
            int cr = -512 + px * 8;
            int ci = -320 + (py * 640 / 25);
            int zr = 0, zi = 0;
            int iter;
            int zr2, zi2, tmp;                /* hoisted for DR C (C89) */
            for (iter = 0; iter < 30; iter++) {
                zr2 = FP_MUL(zr, zr);
                zi2 = FP_MUL(zi, zi);
                if (zr2 + zi2 > 4 * FP_ONE)
                    break;
                tmp = zr2 - zi2 + cr;
                zi = 2 * FP_MUL(zr, zi) + ci;
                zr = tmp;
            }
            putchar(iter >= 30 ? '#' : " .:-=+*%@#"[iter % 10]);
        }
        putchar('\n');
    }
    return 0;
}
