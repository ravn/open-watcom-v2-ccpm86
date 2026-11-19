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


#ifdef _CPM86

extern void SetCPM86Fmt( void );
extern void FreeCPM86Fmt( void );

extern bool ProcCPM86Format( void );
extern bool ProcCPM86FarHeap( void );

/* Stage A far heap (Extra group descriptor) size, in bytes, as requested by
 * `OPTION FARHEAP=<size>`.  0 (default) means no Extra group is emitted --
 * output stays small-model, byte-identical to phase 1.  Non-zero switches
 * FiniCPM86LoadFile() into "compact model" per the CP/M-86 System Guide's own
 * definition (Code+Data plus >=1 of Stack/Extra/Auxiliary): the model is
 * implicit in which group descriptors are present, not a separate flag. */
extern offset CPM86FarHeapSize;

#endif
