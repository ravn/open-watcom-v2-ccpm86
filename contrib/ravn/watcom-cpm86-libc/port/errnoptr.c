/* port/errnoptr.c -- rc7xx-work #9 (Whetstone) errno-pointer accessor.
 *
 * mathlib's _matherr.c fetches the errno cell via *__get_errno_ptr() rather
 * than touching the `errno` datum directly. Stock Watcom defines this accessor
 * in clib/startup/c/errno.c, but that same object ALSO defines the `errno`
 * global -- which our port/stubs.c already provides as the single-thread small
 * model global -- so linking errno.obj would duplicate it. Instead we supply
 * just the accessor here, returning the address of the shared port/stubs.c
 * `errno`. Compiled with the same register calling convention as the clib, so
 * the emitted symbol is __get_errno_ptr_ (the reference _matherr makes).
 *
 * _matherr is never actually executed by Whetstone (it stays inside every
 * transcendental's domain), but the symbol must resolve for a clean link.
 */

extern int errno;

int *__get_errno_ptr( void )
{
    return( &errno );
}
