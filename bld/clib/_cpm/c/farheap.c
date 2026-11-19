/* farheap.c -- CP/M-86 far-heap OS seam for Open Watcom's retargeted clib.

   This is the far-heap sibling of lowlevel.c (the near-heap __brk/sbrk seam).
   It replaces the three OS-coupled primitives that Watcom's genuine far-heap
   API (_fmalloc/_ffree/_frealloc/_fcalloc/_fmsize/_fheap* in bld/clib/heap/c/,
   all UNCHANGED, all OS-generic on top of these) depends on:

     __AllocSeg  -- hand out a new heap segment,
     __GrowSeg   -- extend an existing one,
     __FreeSeg   -- give one back.

   The stock DOS implementations live in bld/clib/heap/c/{allocseg,growseg,
   freeseg}.c and trap to INT 21h (TinyAllocBlock = AH=48h, TinyFreeBlock =
   AH=49h). On CP/M-86 there is no INT 21h, so those would fault. Because the
   cpm86 clib is merged with `_cpm` listed FIRST (see library/cpm86.086/
   libs.mif), the __AllocSeg/__GrowSeg/__FreeSeg defined here own the merged
   archive dictionary and shadow the DOS trio -- exactly the same shadowing
   that makes lowlevel.c's __brk/sbrk win over heap/c/sbrk.c, and diskio.c's
   fd-layer win over the INT 21h handleio objects.

   Watcom's own fmalloc.c already redirects plain malloc()/free() to
   _fmalloc()/_ffree() whenever __BIG_DATA__ is defined (i.e. whenever the
   program is compiled -mc), so ordinary user C code gets far-heap-backed
   memory transparently, with zero user-code involvement.

   WHY MULTIPLE SEGMENTS, NOT JUST ONE: a CP/M-86 "Compact Model" .CMD (wlink
   `OPTION FARHEAP=<size>`) reserves ONE contiguous Extra region, but each
   Watcom far-heap segment/list node (heapblk) is capped at 64 KB by
   construction (a "tag" length field is 16-bit; this is universal across
   every Watcom target, not a CP/M-86 restriction -- no huge-pointer
   normalization exists anywhere in this API). So to expose a >64 KB Extra
   reservation as usable far heap at all, __AllocSeg must CARVE the one large
   reservation into successive <=64 KB slabs and hand them out one at a time,
   exactly like DOS's __AllocSeg would be asked to allocate several separate
   64 KB blocks from the OS -- the difference is we already own the whole thing
   at load time, so "asking the OS" becomes "advance a paragraph offset".

   WHERE THE TOTAL SIZE COMES FROM: the CP/M-86 base page (CP/M-86 System
   Guide Sec.2.6, Fig 2-4), NOT a compiled-in constant -- so this seam works
   for any FARHEAP=<size> a program was linked with, unmodified. DS+000C..
   000E (LE0..LE2) hold the Extra group's "last byte position" (== byte size
   - 1, a 24-bit value written by the loader from wlink's Extra descriptor);
   DS+000F..0010 (BE0..BE1) hold its base paragraph (segment) -- the loader
   fills both from the SAME descriptor that also sets ES for Compact Model
   (System Guide Sec.2.5), but reading the base page directly (rather than
   trusting ES) is what recovers the TOTAL size, which ES alone never carries.
*/

#include <stddef.h>
#include <malloc.h>     /* __segment, _NULLSEG */
#include "heap.h"        /* BHEAP, TAG_SIZE, SET_HEAP_END, PARAS_IN_64K,
                            heapblk, freelist, __heap_enabled, and the
                            __AllocSeg/__GrowSeg/__FreeSeg prototypes */

#define FIRST_FRL(s)    ((freelist __based(s) *)(BHEAP(s) + 1))

/* DS-relative (near, small-model default) pointers straight at the base
   page fields -- no assembly needed, unlike reading a register (diskio.c's
   _getds/_getes pattern), since the base page IS the data group's own
   offset 0. */
#define CPM86_BP_EXTRA_LEN  ( (unsigned char __near *)0x000cU )   /* LE0,LE1,LE2 */
#define CPM86_BP_EXTRA_SEG  ( (unsigned __near *)0x000fU )        /* BE0,BE1 (word) */

static unsigned __cpm86_fh_base_seg  = 0;   /* 0 == base page not read yet */
static unsigned __cpm86_fh_total_paras;     /* whole Extra group, in paragraphs */
static unsigned __cpm86_fh_used_paras;      /* paragraphs already carved out */

/* The linker merges program FAR_DATA and OPTION FARHEAP into one type-3 EXTRA
 * group. This marker lives in FAR_DATA in farheap.obj, which is pulled from the
 * library after the application objects, so its far offset is the first byte
 * after the application's initialized far data. Start heap slabs after it. */
static unsigned char __far __cpm86_fh_data_end_marker;

static unsigned __cpm86_fh_marker_paras( void )
{
    union {
        unsigned char __far *p;
        unsigned             w[2];       /* w[0]=offset, w[1]=segment */
    } u;
    unsigned off;

    u.p = &__cpm86_fh_data_end_marker;
    off = u.w[0] + 1;
    return( ( off + 15 ) >> 4 );
}

static void __cpm86_fh_init( void )
{
    unsigned char __near  *le = CPM86_BP_EXTRA_LEN;
    unsigned long        last_off;
    unsigned             data_paras;

    last_off = (unsigned long)le[0]
             | ( (unsigned long)le[1] << 8 )
             | ( (unsigned long)le[2] << 16 );
    __cpm86_fh_total_paras = (unsigned)( ( last_off + 1 ) >> 4 );
    __cpm86_fh_base_seg = *CPM86_BP_EXTRA_SEG;
    data_paras = __cpm86_fh_marker_paras();
    __cpm86_fh_used_paras = data_paras < __cpm86_fh_total_paras
                          ? data_paras
                          : __cpm86_fh_total_paras;
}

__segment __AllocSeg( unsigned int amount )
{
    unsigned    chunk_paras;
    unsigned    heaplen;
    __segment   seg;

    ( void )amount;    /* slabs come from the fixed Extra reservation, sized independently of any one request */

    if( !__heap_enabled )
        return( _NULLSEG );
    if( __cpm86_fh_base_seg == 0 )
        __cpm86_fh_init();
    if( __cpm86_fh_used_paras >= __cpm86_fh_total_paras )
        return( _NULLSEG );     /* whole Extra reservation already carved out */

    chunk_paras = __cpm86_fh_total_paras - __cpm86_fh_used_paras;
    if( chunk_paras > PARAS_IN_64K )
        chunk_paras = PARAS_IN_64K;

    seg = (__segment)( __cpm86_fh_base_seg + __cpm86_fh_used_paras );
    __cpm86_fh_used_paras += chunk_paras;

    /* Format the new slab as a fresh one-block heap -- identical to the
       common (OS-independent) tail of Watcom's own allocseg.c. Duplicated
       here because this seam replaces the whole function (same status as
       lowlevel.c fully replacing sbrk.c), not just one OS branch inside it. */
    heaplen = chunk_paras << 4;
    BHEAP( seg )->len = heaplen;
    BHEAP( seg )->prev.segm = _NULLSEG;
    BHEAP( seg )->next.segm = _NULLSEG;
    BHEAP( seg )->rover.offs = sizeof( heapblk );
    BHEAP( seg )->b4rover = 0;
    BHEAP( seg )->numalloc = 0;
    BHEAP( seg )->numfree = 1;
    BHEAP( seg )->freehead.len = 0;
    BHEAP( seg )->freehead.prev.offs = sizeof( heapblk );
    BHEAP( seg )->freehead.next.offs = sizeof( heapblk );
    BHEAP( seg )->largest_blk = heaplen - sizeof( heapblk ) - 2 * TAG_SIZE;
    FIRST_FRL( seg )->len = heaplen - sizeof( heapblk ) - 2 * TAG_SIZE;
    FIRST_FRL( seg )->prev.offs = offsetof( heapblk, freehead );
    FIRST_FRL( seg )->next.offs = offsetof( heapblk, freehead );
    SET_HEAP_END( seg, heaplen - 2 * TAG_SIZE );
    return( seg );
}

int __GrowSeg( __segment seg, unsigned int amount )
{
    ( void )seg;
    ( void )amount;
    return( 0 );        /* each slab's size is fixed at carve time; it never grows */
}

/* __FreeSeg -- always fails. There is no CP/M-86 syscall to hand a segment
   back to any OS (the loader gave the program its whole TPA for the run,
   full stop), so "freeing" one is meaningless here -- unlike DOS's
   TinyFreeBlock. NOTE this is only reachable via _fheapshrink()/
   heapshrink() (fheapmin.c), which unlinks its internal bookkeeping for an
   empty segment BEFORE looking at __FreeSeg's return value -- i.e. calling
   heapshrink() while one particular slab is fully empty permanently forgets
   that slab even though __FreeSeg itself refused. This is an existing quirk
   in Watcom's OS-generic fheapmin.c (unmodified), not something introduced
   here; avoid heapshrink()/_fheapshrink() in CP/M-86 programs that still
   need the far heap afterwards. */
int __FreeSeg( __segment seg )
{
    ( void )seg;
    return( -1 );
}
