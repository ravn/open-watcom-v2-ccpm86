/* Milestone 1 for ravn/open-watcom-v2#13: "hello world" printed by Aztec C's
 * stdlib puts() -- recompiled from Aztec source by Watcom wcc -- running on
 * CP/M-86 under emu2.  puts() is compiled from ../src/STDIO/puts.c (Aztec,
 * uncommitted); it calls putchar(), supplied by ../port/cpm86_glue.c (ours). */
extern int puts( char *s );     /* Aztec stdlib, recompiled by wcc */

int main( void )
{
    puts( "hello, world -- aztec puts() recompiled by watcom on cpm86" );
    return 0;
}
