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
****************************************************************************/


#ifdef _CPM86

extern void FiniCPM86LoadFile( void );

/* Stage B (medium/big model) load-time relocation support.
 * The generic relocation walk calls these while patching cross-group far
 * segment references so wlink emits genuine CP/M-86 P_LOAD fixup records
 * instead of baking in a link-time-zeroed segment (see loadcpm86.c). */
extern void     ResetCPM86Fixups( void );
extern void     AddCPM86Fixup( segment loc_seg, offset loc_off, segment tgt_seg );
extern segment  CPM86GroupRelPara( segment seg );

#endif
