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
#include "loadfile.h"
#include "dbgall.h"
#include "loadcpm86.h"


#ifdef _CPM86

#define CMD_HDR_SIZE    128
#define CMD_REC_SIZE    128
#define CMD_PARA_SIZE   16
#define CMD_MAX_GROUPS  8

#define CMD_TYPE_CODE   1
#define CMD_TYPE_DATA   2

/* number of 16-byte paragraphs needed to hold size bytes */
#define CMD_PARAS(size) ((unsigned_16)( __ROUND_UP_SIZE_PARA( size ) >> 4 ))

static unsigned_8 *putU16( unsigned_8 *p, unsigned_16 val )
/*********************************************************/
{
    *p++ = (unsigned_8)val;
    *p++ = (unsigned_8)( val >> 8 );
    return( p );
}

void FiniCPM86LoadFile( void )
/*****************************
 * write the group images and the 128-byte descriptor header
 */
{
    group_entry     *group;
    unsigned_8      header[CMD_HDR_SIZE];
    unsigned_8      *desc;
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
    for( group = Groups; group != NULL; group = group->next ) {
        if( ndesc >= CMD_MAX_GROUPS )
            break;
        if( CalcGroupSize( group ) == 0 )
            continue;
        CurrSect = group->section;
        img_len = WriteGroupLoad( group, false );   /* stored image bytes */
        /* Pad the stored image to a PARAGRAPH (16-byte) boundary so its byte
         * length equals the paragraph "length" written into the descriptor
         * below.  The loader locates each following group at base + length*16,
         * so padding to a 128-byte record here while declaring only the
         * paragraph length would leave the next group's image 0..112 bytes
         * past where the loader reads it -- making all of its data come back
         * zero.  Genuine DRI .CMD files pack groups at paragraph granularity. */
        pad = (size_t)( -(long)img_len & ( CMD_PARA_SIZE - 1 ) );
        if( pad != 0 )
            PadLoad( pad );
        *desc++ = ( group->leaders->class->flags & CLASS_CODE ) ?
                    CMD_TYPE_CODE : CMD_TYPE_DATA;
        desc = putU16( desc, CMD_PARAS( img_len ) );            /* length */
        desc = putU16( desc, 0 );                               /* base   */
        desc = putU16( desc, CMD_PARAS( CalcGroupSize( group ) ) ); /* min */
        desc = putU16( desc, CMD_PARAS( CalcGroupSize( group ) ) ); /* max */
        ndesc++;
    }

    /* Append any debug info AFTER the group images (the CMD loader ignores
     * trailing bytes) -- must happen before we seek back to write the header,
     * else it would overwrite the images at offset 128. */
    DBIWrite();

    CurrSect = Root;
    SeekLoad( 0 );                      /* rewrite the reserved 128-byte header last */
    WriteLoad( header, sizeof( header ) );
}

#endif
