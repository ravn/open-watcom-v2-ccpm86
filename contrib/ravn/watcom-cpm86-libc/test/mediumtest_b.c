/* mediumtest_b.c -- second translation unit for mediumtest.c. Compiled with
 * -mm -zm, so each function becomes its OWN `<name>_TEXT` code segment,
 * distinct from mediumtest.c's segments. wlink coalesces all of them into one
 * CP/M-86 Code Group Descriptor; every call from mediumtest.c into here is a
 * cross-segment FAR call whose segment must be loader-relocated. Keeping these
 * in a separate .obj is what makes the far-call path real rather than a
 * same-segment call the linker could optimize to near.
 */
int magicO( void )    { return 0x114F; }
int magicK( void )    { return 0x224B; }
int magicBang( void ) { return 0x3321; }
int magicCR( void )   { return 0x440D; }
