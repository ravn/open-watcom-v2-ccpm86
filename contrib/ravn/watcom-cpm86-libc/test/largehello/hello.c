/* hello.c -- minimal LARGE-model CP/M-86 smoke test.
 *
 * Exercises BOTH large-model traits in one image:
 *   - main() is FAR code, far-called from crt0lm_min (far-code fixups), and
 *   - the string literal is FAR data, walked through a `char far *` (far-data
 *     addressing + the loader's group relocation for the string's segment).
 *
 * Prints via the crt0's tiny BDOS-2 console helper -- no clib, so this is a
 * pure loader/codegen proof, independent of the (not-yet-built) large clib.
 */
extern void bdos_conout(int c);

int main(void)
{
    char far *p = "HELLO LARGE MODEL\r\n";
    while (*p)
        bdos_conout(*p++);
    return 0;
}
