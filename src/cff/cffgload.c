/****************************************************************************
 *
 * cffgload.c
 *
 *   OpenType Glyph Loader (body).
 *
 * Copyright (C) 1996-2021 by
 * David Turner, Robert Wilhelm, and Werner Lemberg.
 *
 * This file is part of the FreeType project, and may only be used,
 * modified, and distributed under the terms of the FreeType project
 * license, LICENSE.TXT.  By continuing to use, modify, or distribute
 * this file you indicate that you have read the license and
 * understand and accept it fully.
 *
 */


#include <freetype/internal/ftdebug.h>
#include <freetype/internal/ftstream.h>
#include <freetype/internal/sfnt.h>
#include <freetype/internal/ftcalc.h>
#include <freetype/internal/psaux.h>
#include <freetype/ftoutln.h>
#include <freetype/ftdriver.h>

#include "cffload.h"
#include "cffgload.h"

#include "cfferrs.h"


  /**************************************************************************
   *
   * The macro FT_TS_COMPONENT is used in trace mode.  It is an implicit
   * parameter of the FT_TS_TRACE() and FT_TS_ERROR() macros, used to print/log
   * messages during execution.
   */
#undef  FT_TS_COMPONENT
#define FT_TS_COMPONENT  cffgload


  FT_TS_LOCAL_DEF( FT_TS_Error )
  cff_get_glyph_data( TT_Face    face,
                      FT_TS_UInt    glyph_index,
                      FT_TS_Byte**  pointer,
                      FT_TS_ULong*  length )
  {
#ifdef FT_TS_CONFIG_OPTION_INCREMENTAL
    /* For incremental fonts get the character data using the */
    /* callback function.                                     */
    if ( face->root.internal->incremental_interface )
    {
      FT_TS_Data   data;
      FT_TS_Error  error =
                  face->root.internal->incremental_interface->funcs->get_glyph_data(
                    face->root.internal->incremental_interface->object,
                    glyph_index, &data );


      *pointer = (FT_TS_Byte*)data.pointer;
      *length  = data.length;

      return error;
    }
    else
#endif /* FT_TS_CONFIG_OPTION_INCREMENTAL */

    {
      CFF_Font  cff = (CFF_Font)(face->extra.data);


      return cff_index_access_element( &cff->charstrings_index, glyph_index,
                                       pointer, length );
    }
  }


  FT_TS_LOCAL_DEF( void )
  cff_free_glyph_data( TT_Face    face,
                       FT_TS_Byte**  pointer,
                       FT_TS_ULong   length )
  {
#ifndef FT_TS_CONFIG_OPTION_INCREMENTAL
    FT_TS_UNUSED( length );
#endif

#ifdef FT_TS_CONFIG_OPTION_INCREMENTAL
    /* For incremental fonts get the character data using the */
    /* callback function.                                     */
    if ( face->root.internal->incremental_interface )
    {
      FT_TS_Data  data;


      data.pointer = *pointer;
      data.length  = (FT_TS_UInt)length;

      face->root.internal->incremental_interface->funcs->free_glyph_data(
        face->root.internal->incremental_interface->object, &data );
    }
    else
#endif /* FT_TS_CONFIG_OPTION_INCREMENTAL */

    {
      CFF_Font  cff = (CFF_Font)(face->extra.data);


      cff_index_forget_element( &cff->charstrings_index, pointer );
    }
  }


  /*************************************************************************/
  /*************************************************************************/
  /*************************************************************************/
  /**********                                                      *********/
  /**********                                                      *********/
  /**********            COMPUTE THE MAXIMUM ADVANCE WIDTH         *********/
  /**********                                                      *********/
  /**********    The following code is in charge of computing      *********/
  /**********    the maximum advance width of the font.  It        *********/
  /**********    quickly processes each glyph charstring to        *********/
  /**********    extract the value from either a `sbw' or `seac'   *********/
  /**********    operator.                                         *********/
  /**********                                                      *********/
  /*************************************************************************/
  /*************************************************************************/
  /*************************************************************************/


#if 0 /* unused until we support pure CFF fonts */


  FT_TS_LOCAL_DEF( FT_TS_Error )
  cff_compute_max_advance( TT_Face  face,
                           FT_TS_Int*  max_advance )
  {
    FT_TS_Error     error = FT_TS_Err_Ok;
    CFF_Decoder  decoder;
    FT_TS_Int       glyph_index;
    CFF_Font     cff = (CFF_Font)face->other;

    PSAux_Service            psaux         = (PSAux_Service)face->psaux;
    const CFF_Decoder_Funcs  decoder_funcs = psaux->cff_decoder_funcs;


    *max_advance = 0;

    /* Initialize load decoder */
    decoder_funcs->init( &decoder, face, 0, 0, 0, 0, 0, 0 );

    decoder.builder.metrics_only = 1;
    decoder.builder.load_points  = 0;

    /* For each glyph, parse the glyph charstring and extract */
    /* the advance width.                                     */
    for ( glyph_index = 0; glyph_index < face->root.num_glyphs;
          glyph_index++ )
    {
      FT_TS_Byte*  charstring;
      FT_TS_ULong  charstring_len;


      /* now get load the unscaled outline */
      error = cff_get_glyph_data( face, glyph_index,
                                  &charstring, &charstring_len );
      if ( !error )
      {
        error = decoder_funcs->prepare( &decoder, size, glyph_index );
        if ( !error )
          error = decoder_funcs->parse_charstrings_old( &decoder,
                                                        charstring,
                                                        charstring_len,
                                                        0 );

        cff_free_glyph_data( face, &charstring, &charstring_len );
      }

      /* ignore the error if one has occurred -- skip to next glyph */
      error = FT_TS_Err_Ok;
    }

    *max_advance = decoder.builder.advance.x;

    return FT_TS_Err_Ok;
  }


#endif /* 0 */


  FT_TS_LOCAL_DEF( FT_TS_Error )
  cff_slot_load( CFF_GlyphSlot  glyph,
                 CFF_Size       size,
                 FT_TS_UInt        glyph_index,
                 FT_TS_Int32       load_flags )
  {
    FT_TS_Error     error;
    CFF_Decoder  decoder;
    PS_Decoder   psdecoder;
    TT_Face      face = (TT_Face)glyph->root.face;
    FT_TS_Bool      hinting, scaled, force_scaling;
    CFF_Font     cff  = (CFF_Font)face->extra.data;

    PSAux_Service            psaux         = (PSAux_Service)face->psaux;
    const CFF_Decoder_Funcs  decoder_funcs = psaux->cff_decoder_funcs;

    FT_TS_Matrix  font_matrix;
    FT_TS_Vector  font_offset;


    force_scaling = FALSE;

    /* in a CID-keyed font, consider `glyph_index' as a CID and map */
    /* it immediately to the real glyph_index -- if it isn't a      */
    /* subsetted font, glyph_indices and CIDs are identical, though */
    if ( cff->top_font.font_dict.cid_registry != 0xFFFFU &&
         cff->charset.cids                               )
    {
      /* don't handle CID 0 (.notdef) which is directly mapped to GID 0 */
      if ( glyph_index != 0 )
      {
        glyph_index = cff_charset_cid_to_gindex( &cff->charset,
                                                 glyph_index );
        if ( glyph_index == 0 )
          return FT_TS_THROW( Invalid_Argument );
      }
    }
    else if ( glyph_index >= cff->num_glyphs )
      return FT_TS_THROW( Invalid_Argument );

    if ( load_flags & FT_TS_LOAD_NO_RECURSE )
      load_flags |= FT_TS_LOAD_NO_SCALE | FT_TS_LOAD_NO_HINTING;

    glyph->x_scale = 0x10000L;
    glyph->y_scale = 0x10000L;
    if ( size )
    {
      glyph->x_scale = size->root.metrics.x_scale;
      glyph->y_scale = size->root.metrics.y_scale;
    }

#ifdef TT_CONFIG_OPTION_EMBEDDED_BITMAPS

    /* try to load embedded bitmap if any              */
    /*                                                 */
    /* XXX: The convention should be emphasized in     */
    /*      the documents because it can be confusing. */
    if ( size )
    {
      CFF_Face      cff_face = (CFF_Face)size->root.face;
      SFNT_Service  sfnt     = (SFNT_Service)cff_face->sfnt;
      FT_TS_Stream     stream   = cff_face->root.stream;


      if ( size->strike_index != 0xFFFFFFFFUL      &&
           sfnt->load_eblc                         &&
           ( load_flags & FT_TS_LOAD_NO_BITMAP ) == 0 )
      {
        TT_SBit_MetricsRec  metrics;


        error = sfnt->load_sbit_image( face,
                                       size->strike_index,
                                       glyph_index,
                                       (FT_TS_UInt)load_flags,
                                       stream,
                                       &glyph->root.bitmap,
                                       &metrics );

        if ( !error )
        {
          FT_TS_Bool    has_vertical_info;
          FT_TS_UShort  advance;
          FT_TS_Short   dummy;


          glyph->root.outline.n_points   = 0;
          glyph->root.outline.n_contours = 0;

          glyph->root.metrics.width  = (FT_TS_Pos)metrics.width  * 64;
          glyph->root.metrics.height = (FT_TS_Pos)metrics.height * 64;

          glyph->root.metrics.horiBearingX = (FT_TS_Pos)metrics.horiBearingX * 64;
          glyph->root.metrics.horiBearingY = (FT_TS_Pos)metrics.horiBearingY * 64;
          glyph->root.metrics.horiAdvance  = (FT_TS_Pos)metrics.horiAdvance  * 64;

          glyph->root.metrics.vertBearingX = (FT_TS_Pos)metrics.vertBearingX * 64;
          glyph->root.metrics.vertBearingY = (FT_TS_Pos)metrics.vertBearingY * 64;
          glyph->root.metrics.vertAdvance  = (FT_TS_Pos)metrics.vertAdvance  * 64;

          glyph->root.format = FT_TS_GLYPH_FORMAT_BITMAP;

          if ( load_flags & FT_TS_LOAD_VERTICAL_LAYOUT )
          {
            glyph->root.bitmap_left = metrics.vertBearingX;
            glyph->root.bitmap_top  = metrics.vertBearingY;
          }
          else
          {
            glyph->root.bitmap_left = metrics.horiBearingX;
            glyph->root.bitmap_top  = metrics.horiBearingY;
          }

          /* compute linear advance widths */

          (void)( (SFNT_Service)face->sfnt )->get_metrics( face, 0,
                                                           glyph_index,
                                                           &dummy,
                                                           &advance );
          glyph->root.linearHoriAdvance = advance;

          has_vertical_info = FT_TS_BOOL(
                                face->vertical_info                   &&
                                face->vertical.number_Of_VMetrics > 0 );

          /* get the vertical metrics from the vmtx table if we have one */
          if ( has_vertical_info )
          {
            (void)( (SFNT_Service)face->sfnt )->get_metrics( face, 1,
                                                             glyph_index,
                                                             &dummy,
                                                             &advance );
            glyph->root.linearVertAdvance = advance;
          }
          else
          {
            /* make up vertical ones */
            if ( face->os2.version != 0xFFFFU )
              glyph->root.linearVertAdvance = (FT_TS_Pos)
                ( face->os2.sTypoAscender - face->os2.sTypoDescender );
            else
              glyph->root.linearVertAdvance = (FT_TS_Pos)
                ( face->horizontal.Ascender - face->horizontal.Descender );
          }

          return error;
        }
      }
    }

#endif /* TT_CONFIG_OPTION_EMBEDDED_BITMAPS */

    /* return immediately if we only want the embedded bitmaps */
    if ( load_flags & FT_TS_LOAD_SBITS_ONLY )
      return FT_TS_THROW( Invalid_Argument );

    /* if we have a CID subfont, use its matrix (which has already */
    /* been multiplied with the root matrix)                       */

    /* this scaling is only relevant if the PS hinter isn't active */
    if ( cff->num_subfonts )
    {
      FT_TS_Long  top_upm, sub_upm;
      FT_TS_Byte  fd_index = cff_fd_select_get( &cff->fd_select,
                                             glyph_index );


      if ( fd_index >= cff->num_subfonts )
        fd_index = (FT_TS_Byte)( cff->num_subfonts - 1 );

      top_upm = (FT_TS_Long)cff->top_font.font_dict.units_per_em;
      sub_upm = (FT_TS_Long)cff->subfonts[fd_index]->font_dict.units_per_em;

      font_matrix = cff->subfonts[fd_index]->font_dict.font_matrix;
      font_offset = cff->subfonts[fd_index]->font_dict.font_offset;

      if ( top_upm != sub_upm )
      {
        glyph->x_scale = FT_TS_MulDiv( glyph->x_scale, top_upm, sub_upm );
        glyph->y_scale = FT_TS_MulDiv( glyph->y_scale, top_upm, sub_upm );

        force_scaling = TRUE;
      }
    }
    else
    {
      font_matrix = cff->top_font.font_dict.font_matrix;
      font_offset = cff->top_font.font_dict.font_offset;
    }

    glyph->root.outline.n_points   = 0;
    glyph->root.outline.n_contours = 0;

    /* top-level code ensures that FT_TS_LOAD_NO_HINTING is set */
    /* if FT_TS_LOAD_NO_SCALE is active                         */
    hinting = FT_TS_BOOL( ( load_flags & FT_TS_LOAD_NO_HINTING ) == 0 );
    scaled  = FT_TS_BOOL( ( load_flags & FT_TS_LOAD_NO_SCALE   ) == 0 );

    glyph->hint        = hinting;
    glyph->scaled      = scaled;
    glyph->root.format = FT_TS_GLYPH_FORMAT_OUTLINE;  /* by default */

    {
#ifdef CFF_CONFIG_OPTION_OLD_ENGINE
      PS_Driver  driver = (PS_Driver)FT_TS_FACE_DRIVER( face );
#endif

      FT_TS_Byte*  charstring;
      FT_TS_ULong  charstring_len;


      decoder_funcs->init( &decoder, face, size, glyph, hinting,
                           FT_TS_LOAD_TARGET_MODE( load_flags ),
                           cff_get_glyph_data,
                           cff_free_glyph_data );

      /* this is for pure CFFs */
      if ( load_flags & FT_TS_LOAD_ADVANCE_ONLY )
        decoder.width_only = TRUE;

      decoder.builder.no_recurse =
        FT_TS_BOOL( load_flags & FT_TS_LOAD_NO_RECURSE );

      /* now load the unscaled outline */
      error = cff_get_glyph_data( face, glyph_index,
                                  &charstring, &charstring_len );
      if ( error )
        goto Glyph_Build_Finished;

      error = decoder_funcs->prepare( &decoder, size, glyph_index );
      if ( error )
        goto Glyph_Build_Finished;

#ifdef CFF_CONFIG_OPTION_OLD_ENGINE
      /* choose which CFF renderer to use */
      if ( driver->hinting_engine == FT_TS_HINTING_FREETYPE )
        error = decoder_funcs->parse_charstrings_old( &decoder,
                                                      charstring,
                                                      charstring_len,
                                                      0 );
      else
#endif
      {
        psaux->ps_decoder_init( &psdecoder, &decoder, FALSE );

        error = decoder_funcs->parse_charstrings( &psdecoder,
                                                  charstring,
                                                  charstring_len );

        /* Adobe's engine uses 16.16 numbers everywhere;              */
        /* as a consequence, glyphs larger than 2000ppem get rejected */
        if ( FT_TS_ERR_EQ( error, Glyph_Too_Big ) )
        {
          /* this time, we retry unhinted and scale up the glyph later on */
          /* (the engine uses and sets the hardcoded value 0x10000 / 64 = */
          /* 0x400 for both `x_scale' and `y_scale' in this case)         */
          hinting       = FALSE;
          force_scaling = TRUE;
          glyph->hint   = hinting;

          error = decoder_funcs->parse_charstrings( &psdecoder,
                                                    charstring,
                                                    charstring_len );
        }
      }

      cff_free_glyph_data( face, &charstring, charstring_len );

      if ( error )
        goto Glyph_Build_Finished;

#ifdef FT_TS_CONFIG_OPTION_INCREMENTAL
      /* Control data and length may not be available for incremental */
      /* fonts.                                                       */
      if ( face->root.internal->incremental_interface )
      {
        glyph->root.control_data = NULL;
        glyph->root.control_len = 0;
      }
      else
#endif /* FT_TS_CONFIG_OPTION_INCREMENTAL */

      /* We set control_data and control_len if charstrings is loaded. */
      /* See how charstring loads at cff_index_access_element() in     */
      /* cffload.c.                                                    */
      {
        CFF_Index  csindex = &cff->charstrings_index;


        if ( csindex->offsets )
        {
          glyph->root.control_data = csindex->bytes +
                                     csindex->offsets[glyph_index] - 1;
          glyph->root.control_len  = (FT_TS_Long)charstring_len;
        }
      }

  Glyph_Build_Finished:
      /* save new glyph tables, if no error */
      if ( !error )
        decoder.builder.funcs.done( &decoder.builder );
      /* XXX: anything to do for broken glyph entry? */
    }

#ifdef FT_TS_CONFIG_OPTION_INCREMENTAL

    /* Incremental fonts can optionally override the metrics. */
    if ( !error                                                               &&
         face->root.internal->incremental_interface                           &&
         face->root.internal->incremental_interface->funcs->get_glyph_metrics )
    {
      FT_TS_Incremental_MetricsRec  metrics;


      metrics.bearing_x = decoder.builder.left_bearing.x;
      metrics.bearing_y = 0;
      metrics.advance   = decoder.builder.advance.x;
      metrics.advance_v = decoder.builder.advance.y;

      error = face->root.internal->incremental_interface->funcs->get_glyph_metrics(
                face->root.internal->incremental_interface->object,
                glyph_index, FALSE, &metrics );

      decoder.builder.left_bearing.x = metrics.bearing_x;
      decoder.builder.advance.x      = metrics.advance;
      decoder.builder.advance.y      = metrics.advance_v;
    }

#endif /* FT_TS_CONFIG_OPTION_INCREMENTAL */

    if ( !error )
    {
      /* Now, set the metrics -- this is rather simple, as   */
      /* the left side bearing is the xMin, and the top side */
      /* bearing the yMax.                                   */

      /* For composite glyphs, return only left side bearing and */
      /* advance width.                                          */
      if ( load_flags & FT_TS_LOAD_NO_RECURSE )
      {
        FT_TS_Slot_Internal  internal = glyph->root.internal;


        glyph->root.metrics.horiBearingX = decoder.builder.left_bearing.x;
        glyph->root.metrics.horiAdvance  = decoder.glyph_width;
        internal->glyph_matrix           = font_matrix;
        internal->glyph_delta            = font_offset;
        internal->glyph_transformed      = 1;
      }
      else
      {
        FT_TS_BBox            cbox;
        FT_TS_Glyph_Metrics*  metrics = &glyph->root.metrics;
        FT_TS_Bool            has_vertical_info;


        if ( face->horizontal.number_Of_HMetrics )
        {
          FT_TS_Short   horiBearingX = 0;
          FT_TS_UShort  horiAdvance  = 0;


          ( (SFNT_Service)face->sfnt )->get_metrics( face, 0,
                                                     glyph_index,
                                                     &horiBearingX,
                                                     &horiAdvance );
          metrics->horiAdvance          = horiAdvance;
          metrics->horiBearingX         = horiBearingX;
          glyph->root.linearHoriAdvance = horiAdvance;
        }
        else
        {
          /* copy the _unscaled_ advance width */
          metrics->horiAdvance          = decoder.glyph_width;
          glyph->root.linearHoriAdvance = decoder.glyph_width;
        }

        glyph->root.internal->glyph_transformed = 0;

        has_vertical_info = FT_TS_BOOL( face->vertical_info                   &&
                                     face->vertical.number_Of_VMetrics > 0 );

        /* get the vertical metrics from the vmtx table if we have one */
        if ( has_vertical_info )
        {
          FT_TS_Short   vertBearingY = 0;
          FT_TS_UShort  vertAdvance  = 0;


          ( (SFNT_Service)face->sfnt )->get_metrics( face, 1,
                                                     glyph_index,
                                                     &vertBearingY,
                                                     &vertAdvance );
          metrics->vertBearingY = vertBearingY;
          metrics->vertAdvance  = vertAdvance;
        }
        else
        {
          /* make up vertical ones */
          if ( face->os2.version != 0xFFFFU )
            metrics->vertAdvance = (FT_TS_Pos)( face->os2.sTypoAscender -
                                             face->os2.sTypoDescender );
          else
            metrics->vertAdvance = (FT_TS_Pos)( face->horizontal.Ascender -
                                             face->horizontal.Descender );
        }

        glyph->root.linearVertAdvance = metrics->vertAdvance;

        glyph->root.format = FT_TS_GLYPH_FORMAT_OUTLINE;

        glyph->root.outline.flags = 0;
        if ( size && size->root.metrics.y_ppem < 24 )
          glyph->root.outline.flags |= FT_TS_OUTLINE_HIGH_PRECISION;

        glyph->root.outline.flags |= FT_TS_OUTLINE_REVERSE_FILL;

        /* apply the font matrix, if any */
        if ( font_matrix.xx != 0x10000L || font_matrix.yy != 0x10000L ||
             font_matrix.xy != 0        || font_matrix.yx != 0        )
        {
          FT_TS_Outline_Transform( &glyph->root.outline, &font_matrix );

          metrics->horiAdvance = FT_TS_MulFix( metrics->horiAdvance,
                                            font_matrix.xx );
          metrics->vertAdvance = FT_TS_MulFix( metrics->vertAdvance,
                                            font_matrix.yy );
        }

        if ( font_offset.x || font_offset.y )
        {
          FT_TS_Outline_Translate( &glyph->root.outline,
                                font_offset.x,
                                font_offset.y );

          metrics->horiAdvance += font_offset.x;
          metrics->vertAdvance += font_offset.y;
        }

        if ( ( load_flags & FT_TS_LOAD_NO_SCALE ) == 0 || force_scaling )
        {
          /* scale the outline and the metrics */
          FT_TS_Int       n;
          FT_TS_Outline*  cur     = &glyph->root.outline;
          FT_TS_Vector*   vec     = cur->points;
          FT_TS_Fixed     x_scale = glyph->x_scale;
          FT_TS_Fixed     y_scale = glyph->y_scale;


          /* First of all, scale the points */
          if ( !hinting || !decoder.builder.hints_funcs )
            for ( n = cur->n_points; n > 0; n--, vec++ )
            {
              vec->x = FT_TS_MulFix( vec->x, x_scale );
              vec->y = FT_TS_MulFix( vec->y, y_scale );
            }

          /* Then scale the metrics */
          metrics->horiAdvance = FT_TS_MulFix( metrics->horiAdvance, x_scale );
          metrics->vertAdvance = FT_TS_MulFix( metrics->vertAdvance, y_scale );
        }

        /* compute the other metrics */
        FT_TS_Outline_Get_CBox( &glyph->root.outline, &cbox );

        metrics->width  = cbox.xMax - cbox.xMin;
        metrics->height = cbox.yMax - cbox.yMin;

        metrics->horiBearingX = cbox.xMin;
        metrics->horiBearingY = cbox.yMax;

        if ( has_vertical_info )
        {
          metrics->vertBearingX = metrics->horiBearingX -
                                    metrics->horiAdvance / 2;
          metrics->vertBearingY = FT_TS_MulFix( metrics->vertBearingY,
                                             glyph->y_scale );
        }
        else
        {
          if ( load_flags & FT_TS_LOAD_VERTICAL_LAYOUT )
            ft_synthesize_vertical_metrics( metrics,
                                            metrics->vertAdvance );
        }
      }
    }

    return error;
  }


/* END */
