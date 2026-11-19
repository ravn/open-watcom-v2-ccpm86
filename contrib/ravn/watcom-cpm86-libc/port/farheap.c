/* farheap.c -- CP/M-86 far-heap OS seam for Open Watcom's retargeted clib
 * (Stage A, "compact model": tasks/plan-cpm86-big-model-2026-08-18.md).
 *
 * Replaces the OS-coupled primitives Watcom's genuine far-heap API
 * (_fmalloc/_ffree/_frealloc/_fcalloc/_fmsize/_fheap* in bld/clib/heap/c/,
 * all UNCHANGED, all OS-generic on top of these) depends on: __AllocSeg
 * (hand out a new heap segment), __GrowSeg (extend one), __FreeSeg (give one
 * back). Watcom's own fmalloc.c already redirects plain malloc()/free() to
 * _fmalloc()/_ffree() whenever __BIG_DATA__ is defined (i.e. whenever the
 * program is compiled -mc), so ordinary user C code gets far-heap-backed
 * memory transparently, with zero user-code involvement.
 *
 * WHY MULTIPLE SEGMENTS, NOT JUST ONE: a CP/M-86 "Compact Model" .CMD (wlink
 * `OPTION FARHEAP=<size>`, Phase A1) reserves ONE contiguous Extra region,
 * but each Watcom far-heap segment/list node (heapblk) is capped at 64 KB by
 * construction (a "tag" length field is 16-bit; this is universal across
 * every Watcom target, not a CP/M-86 restriction -- no huge-pointer
 * normalization exists anywhere in this API, see
 * tasks/memory/reference_cpm86_big_model.md's far-vs-huge section). So to
 * expose a >64 KB Extra reservation as usable far heap at all, __AllocSeg
 * must CARVE the one large reservation into successive <=64 KB slabs and
 * hand them out one at a time, exactly like DOS's __AllocSeg would be asked
 * to allocate several separate 64 KB blocks from the OS -- the difference is
 * we already own the whole thing at load time, so "asking the OS" becomes
 * "advance a paragraph offset".
 *
 * WHERE THE TOTAL SIZE COMES FROM: the CP/M-86 base page (CP/M-86 System
 * Guide Sec.2.6, Fig 2-4), NOT a compiled-in constant -- so this seam works
 * for any FARHEAP=<size> a program was linked with, unmodified. DS+000C..
 * 000E (LE0..LE2) hold the Extra group's "last byte position" (== byte size
 * - 1, a 24-bit value written by the loader from wlink's Extra descriptor);
 * DS+000F..0010 (BE0..BE1) hold its base paragraph (segment) -- the loader
 * fills both from the SAME descriptor that also sets ES for Compact Model
 * (System Guide Sec.2.5), but reading the base page directly (rather than
 * trusting ES) is what recovers the TOTAL size, which ES alone never
 * carries. See tasks/memory/reference_cpm86_cmd_header.md.
 *
 * This mirrors port/lowlevel.c's retarget of the DOS-trapping near-heap
 * __brk/sbrk (same "we already own this memory, no per-grow syscall is
 * needed" story, one level up: whole segments instead of a DGROUP bump).
 */

#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>     /* __segment, _NULLSEG */
#include "roundmac.h"   /* PARAS_IN_64K is in heap.h; roundmac kept for parity with allocseg.c */
#include "heap.h"        /* BHEAP, TAG_SIZE, SET_HEAP_END, heapblk, freelist, __heap_enabled */

#define FIRST_FRL(s)    ((freelist __based(s) *)(BHEAP(s) + 1))

/* DS-relative (near, small-model default) pointers straight at the base
 * page fields -- no assembly needed, unlike reading a register (port/
 * diskio.c's _getds/_getes pattern), since the base page IS the data
 * group's own offset 0. */
#define CPM86_BP_EXTRA_LEN  ( (unsigned char near *)0x000cU )   /* LE0,LE1,LE2 */
#define CPM86_BP_EXTRA_SEG  ( (unsigned near *)0x000fU )        /* BE0,BE1 (word) */

static unsigned __cpm86_fh_base_seg  = 0;   /* 0 == base page not read yet */
static unsigned __cpm86_fh_total_paras;     /* whole Extra group, in paragraphs */
static unsigned __cpm86_fh_used_paras;      /* paragraphs already carved out */

/* The linker merges program FAR_DATA and OPTION FARHEAP into one type-3 EXTRA
 * group.  This marker lives in FAR_DATA in farheap.obj, which is pulled from the
 * library after the application objects, so its far offset is the first byte
 * after the application's initialized far data.  Starting the heap at
 * ceil(marker+1) avoids overwriting that data.
 *
 * Example: ZIP.CMD's header has Extra G_LENGTH=G_MIN=423 paras.  With
 * carve-from-0 the first _fmalloc formats a heapblk at EXTRA:0000, corrupting
 * Zip's far string tables; a later allocation spins in __MemAllocator's
 * free-list walk.  The marker lands at the end of those 423 paras, so the first
 * heap slab starts at EXTRA+423 instead. */
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
    unsigned    data_paras;

    __cpm86_fh_base_seg = *CPM86_BP_EXTRA_SEG;
    data_paras = __cpm86_fh_marker_paras();

#ifdef CPM86_FARHEAP_PARAS
    /* Real CCP/M-86 (Concurrent 3.1) writes only G_MIN (the initialized far-data
     * size) to DS:0x0C, NOT G_MAX (initialized + farheap reservation). The loader
     * reserves G_MAX paragraphs at load time (that is why the farheap option size
     * affects the "For lidt lager" load check), but init_base copies ldt_min --
     * which is G_MIN -- into the base-page Extra length field.
     *
     * Consequence: the DS:0x0C fallback below gives total_paras = G_MIN =
     * data_paras, so available_paras = 0 and every __AllocSeg call returns
     * _NULLSEG. This macro is the fix: pass -DCPM86_FARHEAP_PARAS=N at
     * compile time (N = farheap_bytes/16) so __AllocSeg can carve from the
     * full reserved Extra region. N must match `op farheap=<farheap_bytes>`
     * in the wlink command (build-zip-cpm86.sh does this via the FARHEAP
     * shell variable). */
    __cpm86_fh_total_paras = data_paras + (unsigned)CPM86_FARHEAP_PARAS;
#else
    /* Fallback: read from the base-page Extra descriptor. Works under emu2
     * (which writes G_MAX) and unicorn; fails on real CCP/M-86 as noted above.
     * Programs linked without -DCPM86_FARHEAP_PARAS use this path. */
    {
        unsigned char near  *le = CPM86_BP_EXTRA_LEN;
        unsigned long        last_off;
        last_off = (unsigned long)le[0]
                 | ( (unsigned long)le[1] << 8 )
                 | ( (unsigned long)le[2] << 16 );
        __cpm86_fh_total_paras = (unsigned)( ( last_off + 1 ) >> 4 );
    }
#endif

    __cpm86_fh_used_paras = data_paras < __cpm86_fh_total_paras
                          ? data_paras
                          : __cpm86_fh_total_paras;
}

/* Concurrent CP/M-86 dynamic memory allocation (BDOS function 128, M_ALLOC).
 * This is the CORRECT source of far-heap segments on the real RC759 CCP/M-86:
 * the OS hands out a segment from its free list and REPORTS THE ACTUAL GRANTED
 * SIZE back in the MPB, so we can never over-commit past what really exists --
 * unlike the compile-time OPTION FARHEAP reservation, whose spread grant the
 * loader does not tell the program (base-page 0x0C carries only G_MIN).  See
 * scratch/ccpm86-src/kern/memory.mem (malloc_entry) + mpb.def + modfunc.def
 * (f_malloc = 128), confirmed on real MAME rc759, 2026-08-25.
 *
 *   in:  CL=128, DX=near &MPB.   MPB = {start,min,max,pdadr,flags} (paras).
 *   out: BX=0 ok / 0xFFFF fail;  MPB.start=granted seg, MPB.max=ACTUAL paras. */
struct __cpm86_mpb {
    unsigned start;   /* 0 == relocatable request; out: granted base seg */
    unsigned min;     /* least acceptable paragraphs                     */
    unsigned max;     /* wanted paragraphs; out: ACTUAL granted paras    */
    unsigned pdadr;   /* 0 == calling process                            */
    unsigned flags;   /* 0 == plain unused memory                        */
};

/* fn in CL, near &MPB (DS-relative offset) in DX; BDOS reads DS:DX.  Passing the
 * bare offset (not a void*) keeps this correct in the large model too, where a
 * void* would be a 4-byte far pointer that will not fit DX. */
extern unsigned __cpm86_bdos_alloc( unsigned char fn, unsigned mpb_ofs );
#pragma aux __cpm86_bdos_alloc =    \
    "int 0E0h"                      \
    __parm [__cl] [__dx]            \
    __value [__bx]                  \
    __modify [__ax __bx __cx __dx __es];

/* 0 = untried, 1 = BDOS 128 works, -1 = unavailable -> fall back to carving */
static int      __cpm86_fh_dynamic = 0;
static unsigned __cpm86_fh_last_paras;   /* paras granted by the last fn-128 call */
/* Set once fn 128 (M_ALLOC) has returned its documented OFFFFH failure code
 * (Concurrent CP/M Programmer's Reference Guide, 6.2.6): proof the call EXISTS
 * and merely ran out of memory.  Lets __AllocSeg tell a genuine CCP/M
 * out-of-memory from fn 128 being absent (plain CP/M-86), so a tight-memory
 * failure returns _NULLSEG cleanly instead of carving memory that overlaps
 * live data/stack (which corrupts the deflate tables -> bad/aborting output). */
static int      __cpm86_fh_fn128_seen = 0;

/* Ask CCP/M (BDOS 128) for a fresh far-heap segment big enough for `amount`.
 * On success returns the segment and stores the ACTUAL granted paragraphs in
 * __cpm86_fh_last_paras; on failure returns _NULLSEG. */
static __segment __cpm86_fh_bdos_alloc( unsigned int amount )
{
    static struct __cpm86_mpb __near mpb;  /* __near -> DGROUP (BDOS reads DS:DX) */
    unsigned min_paras;
    unsigned bx;

    /* paragraphs needed to hold the request plus this slab's heap bookkeeping */
    min_paras = (unsigned)( ( amount + sizeof( heapblk ) + 2 * TAG_SIZE + 15 ) >> 4 );
    if( min_paras == 0 )
        min_paras = 1;
    if( min_paras >= PARAS_IN_64K )     /* a single slab must fit one 64K segment */
        return( _NULLSEG );

    mpb.start = 0;
    mpb.min   = min_paras;
    mpb.max   = PARAS_IN_64K - 1;       /* ask for a big slab; OS clamps to free */
    mpb.pdadr = 0;
    mpb.flags = 0;

    bx = __cpm86_bdos_alloc( 128, (unsigned)(void __near *)&mpb );
    if( bx == 0xFFFF )
        __cpm86_fh_fn128_seen = 1;      /* fn 128 exists, just out of memory */
    if( bx == 0xFFFF || mpb.start == 0 )
        return( _NULLSEG );
    if( mpb.max >= PARAS_IN_64K )       /* one slab addresses at most 64K */
        mpb.max = PARAS_IN_64K - 1;
    __cpm86_fh_last_paras = mpb.max;    /* the ACTUAL grant -- no over-commit */
    return( (__segment)mpb.start );
}

__segment __AllocSeg( unsigned int amount )
{
    unsigned    chunk_paras = 0;
    unsigned    heaplen;
    __segment   seg;

    if( !__heap_enabled )
        return( _NULLSEG );

    /* Preferred path: ask CCP/M for real memory (fn 128), which reports the
     * actual grant.  Fall back to carving the loader's OPTION FARHEAP Extra
     * reservation only where fn 128 is unavailable (plain CP/M-86 / an emulator
     * that lacks it). */
    if( __cpm86_fh_dynamic >= 0 ) {
        seg = __cpm86_fh_bdos_alloc( amount );
        if( seg != _NULLSEG ) {
            __cpm86_fh_dynamic = 1;
            chunk_paras = __cpm86_fh_last_paras;
        } else if( __cpm86_fh_fn128_seen ) {
            /* fn 128 is present and simply out of memory: NEVER carve the Extra
             * reservation here.  With OPTION FARHEAP 0 the carve would hand out
             * memory overlapping live data/stack and corrupt the deflate tables
             * (observed: bad-CRC archives, then a CPU trap).  Mark dynamic so the
             * fallback below returns _NULLSEG cleanly -> honest ZE_MEM. */
            __cpm86_fh_dynamic = 1;
        } else if( __cpm86_fh_dynamic == 0 ) {
            __cpm86_fh_dynamic = -1;    /* first call failed & no fn 128 -> absent */
        }
    }

    if( chunk_paras == 0 ) {
        /* --- fallback: carve the OPTION FARHEAP Extra reservation --- */
        if( __cpm86_fh_dynamic == 1 )
            return( _NULLSEG );         /* fn 128 in use but out of memory */
        if( __cpm86_fh_base_seg == 0 )
            __cpm86_fh_init();
        if( __cpm86_fh_used_paras >= __cpm86_fh_total_paras )
            return( _NULLSEG );         /* whole Extra reservation already carved */

        chunk_paras = __cpm86_fh_total_paras - __cpm86_fh_used_paras;
        if( chunk_paras > PARAS_IN_64K )
            chunk_paras = PARAS_IN_64K;

        seg = (__segment)( __cpm86_fh_base_seg + __cpm86_fh_used_paras );
        __cpm86_fh_used_paras += chunk_paras;
    }

    /* Format the new slab as a fresh one-block heap -- identical to the
     * common (OS-independent) tail of Watcom's own allocseg.c. Duplicated
     * here because this seam replaces the whole function (same status as
     * port/lowlevel.c fully replacing sbrk.c), not just one OS branch
     * inside it. */
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
 * back to any OS (the loader gave the program its whole TPA for the run,
 * full stop), so "freeing" one is meaningless here -- unlike DOS's
 * TinyFreeBlock. NOTE this is only reachable via _fheapshrink()/
 * heapshrink() (fheapmin.c), which unlinks its internal bookkeeping for an
 * empty segment BEFORE looking at __FreeSeg's return value -- i.e. calling
 * heapshrink() while one particular slab is fully empty permanently forgets
 * that slab even though __FreeSeg itself refused. This is an existing quirk
 * in Watcom's OS-generic fheapmin.c (unmodified per Phase A2's "zero
 * changes" scope), not something introduced here; avoid heapshrink()/
 * _fheapshrink() in CP/M-86 programs that still need the far heap
 * afterwards. */
int __FreeSeg( __segment seg )
{
    ( void )seg;
    return( -1 );
}
