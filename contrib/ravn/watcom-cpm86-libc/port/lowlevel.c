/* lowlevel.c -- CP/M-86 low-level OS seam for Open Watcom's retargeted clib.

   This is the thin bottom of the retarget: it replaces the ONE OS-coupled
   primitive that Watcom's genuine near-heap depends on -- the DOS __brk (in
   bld/clib/heap/c/sbrk.c, which grows DGROUP by calling INT 21h AH=4Ah
   TinySetBlock) -- with a pure in-DGROUP bump over a static arena. Everything
   above it (malloc / _nmalloc / free / _nfree / calloc / realloc / grownear's
   __ExpandDGROUP) is Watcom's UNCHANGED clib. No DOS trap is emitted.

   Why no OS call is needed: a CP/M-86 .CMD owns its TPA outright. The loader
   hands the program its whole memory region; there is no per-grow "resize my
   segment" syscall to make (unlike DOS, where AH=4Ah must extend the block).
   So growing the near heap is a pure pointer bump inside DGROUP, bounded by a
   static arena we reserve here.

   Worked example (the heap-test oracle): _nmalloc(40) with an empty heap
   (__nheapbeg == NULL, _curbrk == &wc_arena[0]) -> grownear.c __ExpandDGROUP
   computes new_brk = _amblksiz-rounded amount + _curbrk, calls __brk(new_brk);
   we return the OLD _curbrk (== &wc_arena[0]) as the address of the freshly
   exposed block and advance _curbrk. grownear links that block as the first
   miniheap; the 40-byte request is carved from it. */

#include <stddef.h>

/* _curbrk -- current top of the near heap, a DGROUP (near) offset. Normally
   defined in Watcom's crwd086.asm RT-data block; we define it here instead so
   the linker does NOT pull crwd086 (which also drags _psp / _LpCmdLine /
   _osmajor ... that a freestanding CP/M-86 .CMD neither has nor needs). The
   only heap module that touches it is grownear.c. Seeded by wc_heap_init()
   from crt0 before the first allocation. */
unsigned _curbrk = 0;

/* The near-heap arena: a static array that MUST live in DGROUP so that
   `_curbrk = &wc_arena[0]` is a valid DS-relative near offset and the near
   heap hands out DGROUP addresses. Watcom links heap blocks by DGROUP-near
   pointers, so an in-DGROUP array is exactly the right backing store.

   CRITICAL -- the `__near` qualifier is load-bearing in COMPACT model (-mc):
   there module-level data defaults to FAR, so a plain `static char[]` would
   land in a FAR_DATA/AUTO segment OUTSIDE DGROUP. Then `&wc_arena[0]` taken as
   a near offset is 0, `_curbrk` becomes 0, and the near heap hands out
   DS:0x0000.. -- writing straight over the CP/M-86 base page (DS:0x00..0xFF),
   destroying the EXTRA group descriptor at DS:0x0C/0x0F that port/farheap.c
   reads to locate the far heap (bug found 2026-08-19; regression guard
   test/compact_farheap_test.c + build-compact-farheap.sh). `__near` pins it in
   DGROUP in every model, matching this comment's original intent.

   Size is model-aware: in compact/large/huge (far-data models) plain malloc() routes to the
   FAR heap (_fmalloc, port/farheap.c), so the near arena is only a small
   fallback for any explicit _nmalloc -- keep it tiny to leave DGROUP headroom
   (the whole point of compact model). In small/medium it is the ONE heap, so
   size it up to the 64K DGROUP ceiling. */
#ifndef WC_ARENA_BYTES
#if defined(__COMPACT__) || defined(__LARGE__) || defined(__HUGE__)
#define WC_ARENA_BYTES  4096u    /* compact/large/huge: near heap is a fallback; malloc -> far */
#else
#define WC_ARENA_BYTES  36352u   /* 0x8E00: maxed against 64K DGROUP ceiling (leaves ~70B headroom) */
#endif
#endif
static char __near wc_arena[WC_ARENA_BYTES];


/* Seed _curbrk to the arena base. MUST run before the first malloc, because
   grownear.c reads _curbrk (new_brk = amount + _curbrk) BEFORE it calls
   __brk(); a zero _curbrk would make new_brk a raw size, not an address.
   Called from crt0sm.asm (wc_heap_init_) ahead of main.

   Also shrink _amblksiz (Watcom's heap-grab granularity, default 4-8 KB) to
   one paragraph. grownear.c rounds every miniheap grab UP to _amblksiz; with
   the default 8 KB, UnZip's single 32 KB request (the zcalloc(8192,4) inflate
   window / STORED copy buffer) rounds to 40 KB and overruns our ~35 KB arena,
   so the alloc fails. A 16-byte granularity keeps the roundup waste negligible
   so the 32 KB window fits alongside the I/O and CRC buffers in the arena. */
extern unsigned _amblksiz;   /* Watcom clib heap-grab granularity (amblksiz.c) */

void wc_heap_init( void )
{
    _curbrk = (unsigned)&wc_arena[0];
    _amblksiz = 16u;
}

/* __brk -- set the heap top to brk_value and return the PREVIOUS top (the base
   of the freshly exposed region), matching the contract grownear.c expects
   from Watcom's own sbrk.c __brk. Failure -> ~0U (grownear tests for it). The
   only behavioural change vs. DOS is the OS call: we bump within wc_arena and
   fail past its end, instead of asking DOS to resize the program's block. */
void *__brk( unsigned brk_value )
{
    unsigned arena_end = (unsigned)&wc_arena[0] + WC_ARENA_BYTES;
    unsigned old = _curbrk;

    if( brk_value > arena_end )         /* would run past the arena */
        return( (void *)(unsigned)~0U );
    _curbrk = brk_value;
    return( (void *)old );
}

/* sbrk -- POSIX-style increment on top of __brk, for callers (and any future
   stdio path) that want it. Same signature as Watcom's sbrk.c. */
void *sbrk( int increment )
{
    return( __brk( _curbrk + (unsigned)increment ) );
}
