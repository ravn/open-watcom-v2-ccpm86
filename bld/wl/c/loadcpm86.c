/****************************************************************************
*
*                            Open Watcom Project
*
* Copyright (c) 2026 The Open Watcom Contributors. All Rights Reserved.
*
*  ========================================================================
*
*    This file contains Original Code and/or Modifications of Original
*    Code as defined in and that are subject to the Sybase Open Watcom
*    Public License version 1.0 (the 'License'). You may not use this file
*    except in compliance with the License. BY USING THIS FILE YOU AGREE TO
*    ALL TERMS AND CONDITIONS OF THE LICENSE. A copy of the License is
*    provided with the Original Code and Modifications, and is also
*    available at www.sybase.com/developer/opensource.
*
*    The Original Code and all software distributed under the License are
*    distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
*    EXPRESS OR IMPLIED, AND SYBASE AND ALL CONTRIBUTORS HEREBY DISCLAIM
*    ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
*    NON-INFRINGEMENT. Please see the License for the specific language
*    governing rights and limitations under the License.
*
*  ========================================================================
*
* Description:  Routines for creation of CP/M-86 .CMD command files.
*
*   A .CMD file is a 128-byte header followed by group images.  The header is
*   up to eight 9-byte group descriptors:
*       db type   (1=CODE, 2=DATA, 3=EXTRA, 4=STACK, 9=pure code)
*       dw length (paragraphs of stored image)
*       dw base   (load paragraph; 0 => relocated by the loader)
*       dw min    (paragraphs to allocate, incl. BSS)
*       dw max    (maximum paragraphs to allocate)
*   Group images are packed at PARAGRAPH (16-byte) granularity: each group's
*   stored image is padded to a paragraph so its byte length matches the
*   paragraph "length" field in its descriptor, and the loader finds the next
*   group at base + length*16.  (Genuine DRI .CMD files pack this way; padding
*   an image to a 128-byte record while declaring only its paragraph length
*   left every following group's data at an offset the loader never read.)
*   Phase 1 emits
*   base=0 relocatable images with no fixup table (header 0x7F bit 7 clear),
*   which covers the small (CODE+DATA) and 8080 (single group) models.
*
****************************************************************************/


#include <string.h>
#include "linkstd.h"
#include "alloc.h"
#include "loadfile.h"
#include "dbgall.h"
#include "loadcpm86.h"
#include "cmdcpm86.h"


#ifdef _CPM86

#define CMD_HDR_SIZE    128
#define CMD_REC_SIZE    128
#define CMD_PARA_SIZE   16
#define CMD_MAX_GROUPS  8

#define CMD_TYPE_CODE   1
#define CMD_TYPE_DATA   2
#define CMD_TYPE_EXTRA  3
#define CMD_TYPE_STACK  4

/* number of 16-byte paragraphs needed to hold size bytes */
#define CMD_PARAS(size) ((unsigned_16)( __ROUND_UP_SIZE_PARA( size ) >> 4 ))

static unsigned_8 *putU16( unsigned_8 *p, unsigned_16 val )
/*********************************************************/
{
    *p++ = (unsigned_8)val;
    *p++ = (unsigned_8)( val >> 8 );
    return( p );
}

/****************************************************************************
 * Stage B (medium / big model) load-time relocation.
 *
 * A cross-group FAR reference (e.g. `jmp far ptr callee_` between two
 * `<func>_TEXT` segments under `-mm -zm`) needs the loader to fill in the
 * real segment at load time, because a relocatable `.CMD` has base=0 in every
 * group descriptor -- the loader, not the linker, picks each group's load
 * paragraph.  We replicate DR C / LINK-86's mechanism: write a GROUP-RELATIVE
 * paragraph into the far-segment word (target's offset within its own group
 * image) and emit a 4-byte P_LOAD fixup record telling the loader to add that
 * group's runtime load segment.  See reference_drc_cpm86_reloc_format.md.
 *
 * Worked example (the forced-split repro `main_` far-jmps to `callee_`):
 *   wlink lays out main__TEXT @ 0002:0 (image para 0), callee__TEXT @ 0003:0
 *   (image para 1), CODE base = 0002.  main_'s `EA off16 seg16` has its seg
 *   word at code offset 6.  We write the word = 0003-0002 = 1 (callee's
 *   group-relative paragraph) and a record { grp=0x11, para=0, offs=6 }:
 *   location in CODE (hi nibble 1), add CODE base (lo nibble 1).  At load the
 *   loader does word += code_load_seg, resolving the far jump.
 ****************************************************************************/

typedef struct cpm86_fixup {
    struct cpm86_fixup  *next;
    segment             loc_seg;    /* frame paragraph of the word to patch  */
    offset              loc_off;    /* byte offset of the word in that frame  */
    segment             tgt_seg;    /* frame paragraph the word points at     */
} cpm86_fixup;

static cpm86_fixup      *CPM86Fixups;    /* captured cross-group far seg refs  */

void ResetCPM86Fixups( void )
/***************************/
{
    CPM86Fixups = NULL;
}

static bool cpm86GroupIsCode( group_entry *group )
/************************************************/
{
    return( group != NULL && (group->leaders->class->flags & CLASS_CODE) != 0 );
}


static int cpm86GroupCmdType( group_entry *target )
/**************************************************
 * CP/M-86 .CMD group-descriptor type for `target`.  Three coalesced buckets:
 *
 *   type-1 CODE  : ALL CLASS_CODE groups (coalesced into one descriptor).
 *   type-2 DATA  : the DGROUP group (near data: DS/SS), identified via the
 *                  linker's DataGroup pointer -- NOT by Groups-list position.
 *   type-3 EXTRA : ALL other non-code groups (far data) coalesced into one
 *                  descriptor -- one extra ~64K+ segment, NEVER the STACK group.
 *
 * A real CP/M-86 loader identifies groups by TYPE (at most one of each), and
 * the P_LOAD fixup mechanism resolves a group nibble through the base-page
 * descriptor slot for that type.  So a program's far data MUST live in the
 * single EXTRA type, not a second type-2 (indistinguishable from DGROUP) and
 * not type-4 STACK (that group is the stack).  Because each far object's
 * segment word is relocated individually (EXTRA_load_seg + the object's own
 * paragraph within the coalesced EXTRA image), the EXTRA image may even exceed
 * 64K in total as long as no single object does -- a genuine >64K far-data
 * arena without huge pointers.  See reference_cpm86_big_model.md (far data ->
 * G-Type=3 Extra) and reference_drc_cpm86_reloc_mechanism_VERIFIED.md.
 */
{
    if( target == NULL )
        return( 0 );
    if( cpm86GroupIsCode( target ) )
        return( CMD_TYPE_CODE );
    /* DGROUP (the near DS/SS auto-data group) is the ONE type-2 DATA group;
     * every other non-code group is far data -> the coalesced type-3 EXTRA.
     * Keying off DataGroup (not Groups-list position) is essential: OrderGroups
     * can place a FAR_DATA/AUTO group AHEAD of DGROUP, so "first non-code" is
     * NOT DGROUP -- an earlier position-based guess put far data in type-2 and
     * DGROUP in type-3, exactly inverted. */
    if( target == DataGroup )
        return( CMD_TYPE_DATA );
    return( CMD_TYPE_EXTRA );
}

static offset cpm86GroupImgPara( group_entry *target )
/*****************************************************
 * Paragraph offset of `target`'s stored image within its CP/M-86 group
 * descriptor (the coalesced CODE descriptor, or the DATA descriptor),
 * i.e. the value the loader must add its runtime load segment to.
 *
 * This MUST mirror exactly how FiniCPM86LoadFile() lays the images out: it
 * walks the (already OrderGroups-sorted) Groups list, skips empty groups,
 * writes each non-empty group's image PARAGRAPH-PADDED, code-class groups
 * first into one descriptor and the rest into their own -- so a group's image
 * paragraph offset is the running sum of CMD_PARAS(CalcGroupSize()) over the
 * preceding non-empty groups of the SAME class category.
 *
 * NB: wlink's own frame numbers (group->grp_addr.seg) are NOT this value --
 * for `-mm -zm` they increment by 1 per segment regardless of size, so a
 * 54-byte cpmmain_TEXT (4 paragraphs) is followed by add7_TEXT at frame+1 yet
 * its image sits 4 paragraphs later.  Deriving the paragraph from grp_addr.seg
 * (the original approach) mislocated every multi-paragraph function's far
 * target; walking the layout is the authoritative source. */
{
    group_entry     *group;
    offset          para = 0;

    if( target == NULL )
        return( 0 );
    if( cpm86GroupIsCode( target ) ) {
        /* CODE groups are coalesced into one type-1 descriptor: sum preceding
         * non-empty code groups. */
        for( group = Groups; group != NULL; group = group->next ) {
            if( group == target )
                break;
            if( CalcGroupSize( group ) == 0 )
                continue;
            if( !cpm86GroupIsCode( group ) )
                continue;
            para += CMD_PARAS( CalcGroupSize( group ) );
        }
        return( para );
    }
    /* DGROUP (the first non-code group, type-2 DATA) is its own descriptor,
     * image base paragraph 0 -- the word's position is fix->loc_off alone. */
    if( cpm86GroupCmdType( target ) == CMD_TYPE_DATA )
        return( 0 );
    /* A far-data group (type-3 EXTRA): all far groups are coalesced into ONE
     * EXTRA descriptor, so this group's image base is the running paragraph sum
     * of the preceding non-empty EXTRA groups.  The fixup then relocates the
     * far object's segment word to EXTRA_load_seg + this paragraph, giving each
     * far object its own segment base within the shared EXTRA image. */
    for( group = Groups; group != NULL; group = group->next ) {
        if( group == target )
            break;
        if( CalcGroupSize( group ) == 0 )
            continue;
        if( cpm86GroupCmdType( group ) != CMD_TYPE_EXTRA )
            continue;
        para += CMD_PARAS( CalcGroupSize( group ) );
    }
    return( para );
}

segment CPM86GroupRelPara( segment seg )
/***************************************
 * value to store in a far-segment word: the target paragraph's offset within
 * its own group descriptor image (the loader then adds the runtime load base)
 */
{
    return( (segment)cpm86GroupImgPara( FindGroup( seg ) ) );
}

void AddCPM86Fixup( segment loc_seg, offset loc_off, segment tgt_seg )
/********************************************************************/
{
    cpm86_fixup     *fix;

    fix = _PermAlloc( sizeof( *fix ) );
    fix->next = CPM86Fixups;
    fix->loc_seg = loc_seg;
    fix->loc_off = loc_off;
    fix->tgt_seg = tgt_seg;
    CPM86Fixups = fix;
}

static void cpm86WriteFixups( unsigned_8 *header )
/*************************************************
 * Emit the P_LOAD fixup table (if any cross-group far refs were captured):
 * a packed array of 4-byte records at a 128-byte record boundary, terminated
 * by an all-zero record, then set header byte 127 (ch_lbyte) bit 7 and header
 * word 0x7D (ch_fixrec) to the table's start record.  Must run BEFORE
 * DBIWrite() so the record number named by ch_fixrec is stable.
 */
{
    cpm86_fixup     *fix;
    unsigned long   table_pos;
    unsigned_16     fixrec;
    unsigned_8      rec[4];

    if( CPM86Fixups == NULL )
        return;
    table_pos = NullAlign( CMD_REC_SIZE );
    fixrec = (unsigned_16)( table_pos / CMD_REC_SIZE );
    for( fix = CPM86Fixups; fix != NULL; fix = fix->next ) {
        group_entry     *loc_grp;
        group_entry     *tgt_grp;
        unsigned long   loc_flat;
        unsigned_16     para;

        loc_grp = FindGroup( fix->loc_seg );
        tgt_grp = FindGroup( fix->tgt_seg );
        /* byte offset of the far-segment word within its descriptor image:
         * (location group's image paragraph) * 16 + its offset in the group.
         * The loader reads para (>>4) and offs (&15) and computes the address
         * as (loc_group_load_seg + para)*16 + offs. */
        loc_flat = (unsigned long)cpm86GroupImgPara( loc_grp ) * CMD_PARA_SIZE + fix->loc_off;
        para = (unsigned_16)( loc_flat >> 4 );
        /* hi nibble = LOCATION group type, lo nibble = TARGET group type.
         * Encode each group's ACTUAL assigned CMD type (cpm86GroupCmdType), not
         * a code/data binary: a far-data target lives in a type-3 EXTRA group,
         * so its fixups must name type 3 -- the loader resolves the nibble via
         * the base-page descriptor slot for that type. */
        rec[0] = (unsigned_8)( ( cpm86GroupCmdType( loc_grp ) << 4 )
                             | ( cpm86GroupCmdType( tgt_grp ) & 0x0F ) );
        rec[1] = (unsigned_8)para;
        rec[2] = (unsigned_8)( para >> 8 );
        rec[3] = (unsigned_8)( loc_flat & 0x0F );
        WriteLoad( rec, sizeof( rec ) );
    }
    memset( rec, 0, sizeof( rec ) );        /* terminating all-zero record */
    WriteLoad( rec, sizeof( rec ) );
    NullAlign( CMD_REC_SIZE );              /* pad the table to a full record */
    header[0x7D] = (unsigned_8)fixrec;      /* ch_fixrec (LE word)            */
    header[0x7E] = (unsigned_8)( fixrec >> 8 );
    header[0x7F] |= 0x80;                   /* ch_lbyte bit 7 = fixups present */
}

void FiniCPM86LoadFile( void )
/*****************************
 * write the group images and the 128-byte descriptor header
 */
{
    group_entry     *group;
    unsigned_8      header[CMD_HDR_SIZE];
    unsigned_8      *desc;
    unsigned_8      *extra_max_field = NULL; /* -> EXTRA descriptor's G_Max word */
    int             ndesc;
    offset          img_len;
    size_t          pad;

    memset( header, 0, sizeof( header ) );
    OrderGroups( CompareDosSegments );
    CurrSect = Root;
    Root->outfile->file_loc = 0;
    SeekLoad( CMD_HDR_SIZE );           /* reserve the header record */

    desc = header;
    ndesc = 0;

    /* Coalesce ALL CLASS_CODE groups into ONE type-1 CODE group descriptor
     * (Stage B step 1).  The medium model (`-mm -zm`) emits one `<func>_TEXT`
     * CODE group per function; a genuine CP/M-86 .CMD may hold at most eight
     * groups and expects a SINGLE type-1 CODE group whose far references are
     * resolved by loader fixups, not one type-1 descriptor per source segment
     * (which is what the old per-group loop wrote, overflowing CMD_MAX_GROUPS
     * and giving each func its own load base).  We write every code group's
     * image contiguously, paragraph-packed, and sum their paragraph lengths
     * into one descriptor.  In the small/8080 models there is exactly one
     * CLASS_CODE group, so this is byte-identical to the old output.
     *
     * OrderGroups(CompareDosSegments) has already placed all CODE-class groups
     * ahead of DATA, so writing code in this first pass and data in the second
     * keeps the file image code-first / data-second regardless. */
    {
        offset          code_img_paras = 0;     /* stored image, paragraphs */
        offset          code_alloc_paras = 0;   /* min==max alloc, paras    */
        bool            have_code = false;

        for( group = Groups; group != NULL; group = group->next ) {
            if( CalcGroupSize( group ) == 0 )
                continue;
            if( ( group->leaders->class->flags & CLASS_CODE ) == 0 )
                continue;
            CurrSect = group->section;
            img_len = WriteGroupLoad( group, false );   /* stored image bytes */
            /* Pad each code group's stored image to a PARAGRAPH (16-byte)
             * boundary so the concatenated code image stays paragraph-aligned
             * and the summed paragraph length matches the bytes on disk. */
            pad = (size_t)( -(long)img_len & ( CMD_PARA_SIZE - 1 ) );
            if( pad != 0 )
                PadLoad( pad );
            code_img_paras += CMD_PARAS( img_len );
            code_alloc_paras += CMD_PARAS( CalcGroupSize( group ) );
            have_code = true;
        }
        if( have_code ) {
            *desc++ = CMD_TYPE_CODE;
            desc = putU16( desc, (unsigned_16)code_img_paras );    /* length */
            desc = putU16( desc, 0 );                              /* base   */
            desc = putU16( desc, (unsigned_16)code_alloc_paras );  /* min    */
            desc = putU16( desc, (unsigned_16)code_alloc_paras );  /* max    */
            ndesc++;
        }
    }

    /* Non-CODE groups, written after the coalesced code image so the loader
     * reads code then data contiguously.  Two buckets (see cpm86GroupCmdType):
     *   - the FIRST non-code group is DGROUP -> its own type-2 DATA descriptor;
     *   - ALL remaining non-code (far data) groups are COALESCED, exactly like
     *     the code groups above, into ONE type-3 EXTRA descriptor -- one extra
     *     segment, never STACK, and (since each far object's segment is fixed
     *     up individually) allowed to span >64K in total.
     * Their images are written contiguously and paragraph-packed; the two
     * paragraph sums (DGROUP, EXTRA) go into their respective descriptors. */
    {
        offset      data_img_paras = 0, data_alloc_paras = 0;
        offset      extra_img_paras = 0, extra_alloc_paras = 0;
        bool        have_data = false, have_extra = false;

        /* Pass 1: DGROUP (type-2 DATA) image FIRST, so the on-disk image order
         * matches the descriptor order below (CODE, DATA, EXTRA).  CP/M-86
         * concatenates group images in descriptor order with no per-descriptor
         * file offset, so a group's image must be written in the same order its
         * descriptor appears -- and OrderGroups can list a FAR_DATA group ahead
         * of DGROUP, so we cannot rely on Groups-list order here. */
        for( group = Groups; group != NULL; group = group->next ) {
            if( CalcGroupSize( group ) == 0 )
                continue;
            if( group->leaders->class->flags & CLASS_CODE )
                continue;
            if( cpm86GroupCmdType( group ) != CMD_TYPE_DATA )
                continue;
            CurrSect = group->section;
            /* Write only the initialised portion (stops before BSS/STACK).
             * The CP/M-86 loader zero-fills from G_Length up to G_Min. */
            img_len = WriteGroupLoadInitOnly( group );
            pad = (size_t)( -(long)img_len & ( CMD_PARA_SIZE - 1 ) );
            if( pad != 0 )
                PadLoad( pad );
            data_img_paras += CMD_PARAS( img_len );
            data_alloc_paras += CMD_PARAS( CalcGroupSize( group ) );
            have_data = true;
        }
        /* Pass 2: all far-data groups (type-3 EXTRA) coalesced, in Groups order
         * -- matching cpm86GroupImgPara()'s running paragraph sum so each far
         * object's fixup lands at EXTRA_load_seg + its own paragraph. */
        for( group = Groups; group != NULL; group = group->next ) {
            if( CalcGroupSize( group ) == 0 )
                continue;
            if( group->leaders->class->flags & CLASS_CODE )
                continue;
            if( cpm86GroupCmdType( group ) != CMD_TYPE_EXTRA )
                continue;
            CurrSect = group->section;
            img_len = WriteGroupLoad( group, false );   /* stored image bytes */
            pad = (size_t)( -(long)img_len & ( CMD_PARA_SIZE - 1 ) );
            if( pad != 0 )
                PadLoad( pad );
            extra_img_paras += CMD_PARAS( img_len );
            extra_alloc_paras += CMD_PARAS( CalcGroupSize( group ) );
            have_extra = true;
        }
        if( have_data && ndesc < CMD_MAX_GROUPS ) {
            *desc++ = CMD_TYPE_DATA;
            desc = putU16( desc, (unsigned_16)data_img_paras );     /* length */
            desc = putU16( desc, 0 );                               /* base   */
            desc = putU16( desc, (unsigned_16)data_alloc_paras );   /* min    */
            desc = putU16( desc, (unsigned_16)data_alloc_paras );   /* max    */
            ndesc++;
        }
        if( have_extra && ndesc < CMD_MAX_GROUPS ) {
            *desc++ = CMD_TYPE_EXTRA;
            desc = putU16( desc, (unsigned_16)extra_img_paras );    /* length */
            desc = putU16( desc, 0 );                               /* base   */
            desc = putU16( desc, (unsigned_16)extra_alloc_paras );  /* min    */
            extra_max_field = desc;                                 /* G_Max  */
            desc = putU16( desc, (unsigned_16)extra_alloc_paras );  /* max    */
            ndesc++;
        }
    }

    /* Stage A compact model (`OPTION FARHEAP=<size>`): append a type-3
     * Extra group descriptor with NO stored image -- its memory is never
     * written by the linker, only reserved by the loader at load time
     * (G_Length==0).  Per the CP/M-86 System Guide Sec.2.5, the loader sets
     * ES to this group's base automatically at program entry -- crt0 needs
     * no code for this at all (see reference_cpm86_cmd_header.md's Sec.2.5
     * note).  This descriptor, being present at all, is what makes the
     * loader treat the whole file as "Compact Model" instead of "Small
     * Model" -- the model is implicit in which group types exist, not a
     * separate header flag.
     *
     * G_Min is deliberately just ONE paragraph, not the same value as
     * G_Max: G_Min is what the loader must satisfy to run the program at
     * all (a large G_Min made an early 300K test outright REFUSE to load
     * under real Concurrent CP/M-86, "For lidt lager", even though less
     * memory would have been fine) while G_Max=FARHEAP size is only a
     * ceiling -- CP/M-86's loader grants whatever is actually free between
     * G_Min and G_Max, and reports the ACTUAL grant back via the base
     * page's Extra-group length field. port/farheap.c's __AllocSeg already
     * reads that actual (not requested) size, so this makes `OPTION
     * FARHEAP=<size>` mean "use up to this much of whatever RAM is really
     * available", not "fail unless exactly this much is free". */
    if( CPM86FarHeapSize != 0 ) {
        unsigned_16     paras;

        paras = CMD_PARAS( CPM86FarHeapSize );
        if( extra_max_field != NULL ) {
            /* A far-DATA EXTRA descriptor already exists (initialised far data).
             * Do NOT emit a second type-3 -- CP/M-86 keys groups by type and
             * cannot place two.  Instead give that same EXTRA segment its heap
             * headroom by raising its G_Max to cover the far-data image PLUS the
             * requested heap, so one Extra group holds both (and can exceed 64K).
             * port/farheap.c carves the heap from ES beyond the loaded image. */
            unsigned_16 cur = (unsigned_16)( extra_max_field[0] | ( extra_max_field[1] << 8 ) );
            unsigned_16 want = (unsigned_16)( cur + paras );
            if( want < cur )                    /* 16-bit paragraph overflow */
                want = 0xFFFF;
            extra_max_field[0] = (unsigned_8)want;
            extra_max_field[1] = (unsigned_8)( want >> 8 );
        } else if( ndesc < CMD_MAX_GROUPS ) {
            *desc++ = CMD_TYPE_EXTRA;
            desc = putU16( desc, 0 );          /* length: no stored image   */
            desc = putU16( desc, 0 );          /* base: relocatable         */
            desc = putU16( desc, 1 );          /* min: just enough to load  */
            desc = putU16( desc, paras );      /* max: ceiling              */
            ndesc++;
        }
    }

    /* Stage B: emit the P_LOAD fixup table for cross-group far segment refs
     * captured during the relocation walk (empty in small/8080 model, so this
     * is a no-op there and the output stays byte-identical to phase 1).  This
     * sets header byte 127 (ch_lbyte) bit 7 + header word 0x7D (ch_fixrec).
     * Stage B model (LOCKED 2026-08-19): PURE loader-relocation -- fixup table
     * + bit 7 + ch_fixrec, NO crt0 self-reloc and NO type-8 AUX4 copy of the
     * table.  (DR C ships a guard-coordinated dual path AND an AUX4 duplicate
     * so it also runs on emu2; we take ONLY the loader half.  @ravn 2026-08-19:
     * emu2 not applying P_LOAD fixups is an emu2 BUG to fix later, tracked
     * ravn/emu2-cpm86#1 -- not worked around with AUX4 here; medium-model
     * .CMDs verify on real CP/M-86 (MAME) and the Unicorn runner per
     * ravn/rc7xx-work#15.  See reference_drc_cpm86_reloc_mechanism_VERIFIED.md
     * and reference_drc_cpm86_reloc_format.md.) */
    cpm86WriteFixups( header );

    /* Append any debug info AFTER the group images (and after the fixup table
     * above).  The genuine CP/M-86 loader reads the fixup records named by
     * ch_fixrec and stops at the first all-zero record, so trailing debug info
     * is invisible to it; with bit 7 clear (small model) it never reads past
     * the images at all.  Must happen before we seek back to write the header,
     * else it would overwrite the images at offset 128. */
    DBIWrite();

    CurrSect = Root;
    SeekLoad( 0 );                      /* rewrite the reserved 128-byte header last */
    WriteLoad( header, sizeof( header ) );
}

#endif
