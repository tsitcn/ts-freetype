/****************************************************************************
 *
 * pfrload.h
 *
 *   FreeType PFR loader (specification).
 *
 * Copyright (C) 2002-2021 by
 * David Turner, Robert Wilhelm, and Werner Lemberg.
 *
 * This file is part of the FreeType project, and may only be used,
 * modified, and distributed under the terms of the FreeType project
 * license, LICENSE.TXT.  By continuing to use, modify, or distribute
 * this file you indicate that you have read the license and
 * understand and accept it fully.
 *
 */


#ifndef PFRLOAD_H_
#define PFRLOAD_H_

#include "pfrobjs.h"
#include <freetype/internal/ftstream.h>


FT_TS_BEGIN_HEADER

  /* some size checks should be always done (mainly to prevent */
  /* excessive allocation for malformed data), ...             */
#define PFR_CHECK_SIZE( x )  do                       \
                             {                        \
                               if ( p + (x) > limit ) \
                                 goto Too_Short;      \
                             } while ( 0 )

  /* ... and some only if intensive checking is explicitly requested */
#ifdef PFR_CONFIG_NO_CHECKS
#define PFR_CHECK( x )  do { } while ( 0 )
#else
#define PFR_CHECK  PFR_CHECK_SIZE
#endif

#define PFR_NEXT_BYTE( p )    FT_TS_NEXT_BYTE( p )
#define PFR_NEXT_INT8( p )    FT_TS_NEXT_CHAR( p )
#define PFR_NEXT_SHORT( p )   FT_TS_NEXT_SHORT( p )
#define PFR_NEXT_USHORT( p )  FT_TS_NEXT_USHORT( p )
#define PFR_NEXT_LONG( p )    FT_TS_NEXT_OFF3( p )
#define PFR_NEXT_ULONG( p )   FT_TS_NEXT_UOFF3( p )


 /* handling extra items */

  typedef FT_TS_Error
  (*PFR_ExtraItem_ParseFunc)( FT_TS_Byte*    p,
                              FT_TS_Byte*    limit,
                              FT_TS_Pointer  data );

  typedef struct  PFR_ExtraItemRec_
  {
    FT_TS_UInt                  type;
    PFR_ExtraItem_ParseFunc  parser;

  } PFR_ExtraItemRec;

  typedef const struct PFR_ExtraItemRec_*  PFR_ExtraItem;


  FT_TS_LOCAL( FT_TS_Error )
  pfr_extra_items_skip( FT_TS_Byte*  *pp,
                        FT_TS_Byte*  limit );

  FT_TS_LOCAL( FT_TS_Error )
  pfr_extra_items_parse( FT_TS_Byte*      *pp,
                         FT_TS_Byte*       limit,
                         PFR_ExtraItem  item_list,
                         FT_TS_Pointer     item_data );


  /* load a PFR header */
  FT_TS_LOCAL( FT_TS_Error )
  pfr_header_load( PFR_Header  header,
                   FT_TS_Stream   stream );

  /* check a PFR header */
  FT_TS_LOCAL( FT_TS_Bool )
  pfr_header_check( PFR_Header  header );


  /* return number of logical fonts in this file */
  FT_TS_LOCAL( FT_TS_Error )
  pfr_log_font_count( FT_TS_Stream   stream,
                      FT_TS_UInt32   log_section_offset,
                      FT_TS_Long    *acount );

  /* load a pfr logical font entry */
  FT_TS_LOCAL( FT_TS_Error )
  pfr_log_font_load( PFR_LogFont  log_font,
                     FT_TS_Stream    stream,
                     FT_TS_UInt      face_index,
                     FT_TS_UInt32    section_offset,
                     FT_TS_Bool      size_increment );


  /* load a physical font entry */
  FT_TS_LOCAL( FT_TS_Error )
  pfr_phy_font_load( PFR_PhyFont  phy_font,
                     FT_TS_Stream    stream,
                     FT_TS_UInt32    offset,
                     FT_TS_UInt32    size );

  /* finalize a physical font */
  FT_TS_LOCAL( void )
  pfr_phy_font_done( PFR_PhyFont  phy_font,
                     FT_TS_Memory    memory );

  /* */

FT_TS_END_HEADER

#endif /* PFRLOAD_H_ */


/* END */
