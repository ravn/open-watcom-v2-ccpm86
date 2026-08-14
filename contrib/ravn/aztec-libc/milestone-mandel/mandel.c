/* Milestone 2 for ravn/open-watcom-v2#13: the canonical integer Mandelbrot,
 * compiled by Watcom, with its line output routed through Aztec C's stdlib
 * puts() -- recompiled from Aztec source by wcc -- running on CP/M-86 under emu2.
 *
 * The COMPUTE KERNEL is copied VERBATIM (not re-authored) from the project's
 * canonical fixed-point oracle:
 *     open-watcom-v2/contrib/ravn/owc-drc/mandel-ow.c
 *   = scratch/rc759-cmd-toolchain/mandel_cpm86.c
 * 8.8 fixed-point, FP_MUL lowered to one 16x16->32 signed IMUL + byte extract
 * (#pragma aux fpmul) so the link needs NO Watcom 32-bit __I4M helper. Width is
 * 78 (not the oracle's 80) so the row fits the RC759 / MAME 78-column console.
 *
 * Only the GLUE differs from the oracle: entry is a plain int main() over our
 * crt0 (port/crt0sm.asm), and each rendered row is buffered and emitted with
 * Aztec puts() (port/cpm86_glue.c supplies putchar over BDOS C_WRITE). Routing
 * whole rows through the recompiled Aztec puts() is what makes this a #13
 * (Aztec-libc) milestone rather than a bare codegen test.
 *
 * CORRECTNESS ORACLE (independent, 16-bit, does NOT share our link path): the
 * genuine Digital Research C v1.11 build owc-drc/MANDEL-DRC.CMD run under the
 * same emu2. Our 78-wide rows must equal its first 78 columns of all 25 rows
 * (see scripts/build-mandel.sh). The MAME screenshot MANDEL_mame_rc759.png is
 * the additional visual oracle. Using the genuine 16-bit DR C build as oracle
 * -- rather than a 32-bit-int host clang build -- keeps the comparison
 * bit-exact and avoids the int-width mismatch that would make a host compare
 * an equivalence, not a correctness, check.
 */
extern int puts( char *s );     /* Aztec stdlib, recompiled by wcc */

/* fpmul(a,b) == (int)((long)a * b >> 8), via one 16x16 IMUL + byte extract.
 *   imul cx    -> DX:AX = a*b (signed 32-bit product); AX=parm a, CX=parm b
 *   mov al,ah  -> result low  byte = product bits [8..15]
 *   mov ah,dl  -> result high byte = product bits [16..23]
 * AX now holds bits [8..23] = the 8.8 fixed-point product, as an int. */
extern int fpmul( int a, int b );
#pragma aux fpmul =     \
    "imul cx"           \
    "mov al,ah"         \
    "mov ah,dl"         \
    parm [ax] [cx]      \
    value [ax]          \
    modify [dx];

#define FP_SHIFT 8
#define FP_ONE   (1 << FP_SHIFT)              /* 256 */
#define FP_MUL(a, b) fpmul((a), (b))
#define WIDTH  78                             /* 80 in the oracle; 78 to fit RC759 */

int main( void )
{
    char row[WIDTH + 1];
    int py, px;
    for (py = 0; py < 25; py++) {
        for (px = 0; px < WIDTH; px++) {
            int cr = -512 + px * 8;              /* px*8 == px*640/80, overflow-free */
            int ci = -320 + (py * 640 / 25);     /* py*640 (<=15360) doesn't overflow */
            int zr = 0, zi = 0;
            int iter;
            int zr2, zi2, tmp;
            for (iter = 0; iter < 30; iter++) {
                zr2 = FP_MUL(zr, zr);
                zi2 = FP_MUL(zi, zi);
                if (zr2 + zi2 > 4 * FP_ONE)
                    break;
                tmp = zr2 - zi2 + cr;
                zi = 2 * FP_MUL(zr, zi) + ci;
                zr = tmp;
            }
            row[px] = (iter >= 30) ? '#' : " .:-=+*%@#"[iter % 10];
        }
        row[WIDTH] = '\0';
        puts( row );                             /* Aztec puts(): row + '\n' */
    }
    return 0;
}
