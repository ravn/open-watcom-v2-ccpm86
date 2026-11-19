/****************************************************************************
*
*                            Open Watcom Project
*
* Copyright (c) 2026      The Open Watcom Contributors. All Rights Reserved.
*    Portions Copyright (c) 1983-2002 Sybase, Inc. All Rights Reserved.
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
* Description:  WHEN YOU FIGURE OUT WHAT THIS FILE DOES, PLEASE
*               DESCRIBE IT HERE!
*
****************************************************************************/


#include <string.h>
#include "layer0.h"
#include "resfont.h"
#include "reserr.h"
#include "wresrtns.h"


bool ResReadFontInfo( FontInfo *info, FILE *fp )
/**********************************************/
{
    size_t      numread;

    if( (numread = WRESREAD( fp, info, sizeof( FontInfo ) )) != sizeof( FontInfo ) )
        return( WRES_ERROR( WRESIOERR( fp, numread ) ? WRS_READ_FAILED : WRS_READ_INCOMPLETE ) );
    return( false );
}

bool ResWriteFontInfo( FontInfo *info, FILE *fp )
/***********************************************/
{
    if( WRESWRITE( fp, info, sizeof( FontInfo ) ) != sizeof( FontInfo ) )
        return( WRES_ERROR( WRS_WRITE_FAILED ) );
    return( false );
}

bool ResWriteFontDirEntry( FontDirEntry *entry, FILE *fp )
/********************************************************/
{
    /*
     * set dfDevice and dfFace to be the offset of the strings from the start
     * of the FontInfo structure (entry->Entry.Info)
     */
    entry->Info.dfDevice = sizeof( FontInfo );
    entry->Info.dfFace = entry->Info.dfDevice + strlen( entry->DevAndFaceName ) + 1;

    if( WRESWRITE( fp, &(entry->Info), entry->StructSize ) != entry->StructSize )
        return( WRES_ERROR( WRS_WRITE_FAILED ) );
    return( false );
}
