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
* Description:  Command line parsing for the CP/M-86 .CMD load file format.
*
****************************************************************************/


#include <string.h>
#include "linkstd.h"
#include "cmdutils.h"
#include "cmdall.h"
#include "cmdline.h"
#include "cmdcpm86.h"
#include "loadcpm86.h"


#ifdef _CPM86

offset  CPM86FarHeapSize = 0;

void SetCPM86Fmt( void )
/***********************
 * Small / 8080 model (base=0, no fixups) needs no format-specific state.
 * Enable relocation collection so the generic relocation walk hands us
 * cross-group far SEGMENT references (Stage B medium/big model): with no
 * such references (small model) the captured list stays empty and the
 * output is byte-identical to phase 1 (header 0x7F bit 7 stays clear). */
{
    ResetCPM86Fixups();
    LinkState |= LS_MAKE_RELOCS;
}

void FreeCPM86Fmt( void )
/***********************/
{
}


/****************************************************************
 * "Format" Directive
 ****************************************************************/

static bool ProcSmall( void )
/****************************
 * small model: separate CODE (type 1) + DATA (type 2) groups.
 * This is the only validated (MAME-verified on RC759) memory model.
 */
{
    return( true );
}

static bool Proc8080( void )
/***************************
 * 8080 model: single group, loader sets CS=DS=SS=ES.  Not yet implemented
 * or validated, so reject it -- only validated models are accepted.
 */
{
    LnkMsg( FTL+LOC+LINE+MSG_FORMAT_BAD_OPTION, "s", "8080" );
    return( true );
}

static parse_entry  CPM86SubFormats[] = {
    "SMall",        ProcSmall,      MK_CPM86, 0,
    "8080",         Proc8080,       MK_CPM86, 0,
    NULL
};

bool ProcCPM86Format( void )
/**************************/
{
    ProcOne( CPM86SubFormats, SEP_NO );
    FmtData.def_ext = E_CMD;
    return( true );
}

/****************************************************************
 * "OPTION FARheap=<size>" -- Stage A compact model
 ****************************************************************/

bool ProcCPM86FarHeap( void )
/****************************
 * process FARHEAP option: requests an Extra (G-Type 3) group descriptor
 * of <size> bytes, rounded up to a paragraph.  Real LINK-86 precedent is
 * `EXTRA[MAXIMUM[hex-paragraphs]]` (see reference_cpm86_big_model.md);
 * this is the wlink equivalent, byte- rather than paragraph-sized to match
 * every other wlink size option (e.g. STACK).  0 (the default, i.e. this
 * option never given) keeps output identical to phase 1: no Extra group,
 * pure small model.
 */
{
    unsigned_32     value;
    bool            ret;

    ret = GetLong( &value );
    if( ret ) {
        CPM86FarHeapSize = value;
    }
    return( ret );
}

#endif
