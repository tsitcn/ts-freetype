/****************************************************************************
 *
 * cidgload.c
 *
 *   CID-keyed Type1 Glyph Loader (body).
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


#include "cidload.h"
#include "cidgload.h"
#include <freetype/internal/ftdebug.h>
#include <freetype/internal/ftstream.h>
#include <freetype/ftoutln.h>
#include <freetype/internal/ftcalc.h>

#include <freetype/internal/psaux.h>
#include <freetype/internal/cfftypes.h>
#include <freetype/ftdriver.h>

#include "ciderrs.h"


  /**************************************************************************
   *
   * The macro FT_TS_COMPONENT is used in trace mode.  It is an implicit
   * parameter of the FT_TS_TRACE() and FT_TS_ERROR() macros, used to print/log
   * messages during execution.
   */
#undef  FT_TS_COMPONENT
#define FT_TS_COMPONENT  cidgload


  FT_TS_CALLBACK_DEF( FT_TS_Error )
  cid_load_glyph( T1_Decoder  decoder,
                  FT_TS_UInt     glyph_index )
  {
    CID_Face       face = (CID_Face)decoder->builder.face;
    CID_FaceInfo   cid  = &face->cid;
    FT_TS_Byte*       p;
    FT_TS_ULong       fd_select;
    FT_TS_Stream      stream       = face->cid_stream;
    FT_TS_Error       error        = FT_TS_Err_Ok;
    FT_TS_Byte*       charstring   = NULL;
    FT_TS_Memory      memory       = face->root.memory;
    FT_TS_ULong       glyph_length = 0;
    PSAux_Service  psaux        = (PSAux_Service)face->psaux;

    FT_TS_Bool  force_scaling = FALSE;

#ifdef FT_TS_CONFIG_OPTION_INCREMENTAL
    FT_TS_Incremental_InterfaceRec  *inc =
                                   face->root.internal->incremental_interface;
#endif


    FT_TS_TRACE1(( "cid_load_glyph: glyph index %u\n", glyph_index ));

#ifdef FT_TS_CONFIG_OPTION_INCREMENTAL

    /* For incremental fonts get the character data using */
    /* the callback function.                             */
    if ( inc )
    {
      FT_TS_Data  glyph_data;


      error = inc->funcs->get_glyph_data( inc->object,
                                          glyph_index, &glyph_data );
      if ( error || glyph_data.length < cid->fd_bytes )
        goto Exit;

      p         = (FT_TS_Byte*)glyph_data.pointer;
      fd_select = cid_get_offset( &p, cid->fd_bytes );

      glyph_length = glyph_data.length - cid->fd_bytes;

      if ( !FT_TS_QALLOC( charstring, glyph_length ) )
        FT_TS_MEM_COPY( charstring, glyph_data.pointer + cid->fd_bytes,
                     glyph_length );

      inc->funcs->free_glyph_data( inc->object, &glyph_data );

      if ( error )
        goto Exit;
    }

    else

#endif /* FT_TS_CONFIG_OPTION_INCREMENTAL */

    /* For ordinary fonts read the CID font dictionary index */
    /* and charstring offset from the CIDMap.                */
    {
      FT_TS_UInt   entry_len = cid->fd_bytes + cid->gd_bytes;
      FT_TS_ULong  off1, off2;


      if ( FT_TS_STREAM_SEEK( cid->data_offset + cid->cidmap_offset +
                           glyph_index * entry_len )               ||
           FT_TS_FRAME_ENTER( 2 * entry_len )                         )
        goto Exit;

      p         = (FT_TS_Byte*)stream->cursor;
      fd_select = cid_get_offset( &p, cid->fd_bytes );
      off1      = cid_get_offset( &p, cid->gd_bytes );
      p        += cid->fd_bytes;
      off2      = cid_get_offset( &p, cid->gd_bytes );
      FT_TS_FRAME_EXIT();

      if ( fd_select >= cid->num_dicts ||
           off2 > stream->size         ||
           off1 > off2                 )
      {
        FT_TS_TRACE0(( "cid_load_glyph: invalid glyph stream offsets\n" ));
        error = FT_TS_THROW( Invalid_Offset );
        goto Exit;
      }

      glyph_length = off2 - off1;

      if ( glyph_length == 0                             ||
           FT_TS_QALLOC( charstring, glyph_length )         ||
           FT_TS_STREAM_READ_AT( cid->data_offset + off1,
                              charstring, glyph_length ) )
        goto Exit;
    }

    /* Now set up the subrs array and parse the charstrings. */
    {
      CID_FaceDict  dict;
      CID_Subrs     cid_subrs = face->subrs + fd_select;
      FT_TS_UInt       cs_offset;


      /* Set up subrs */
      decoder->num_subrs  = cid_subrs->num_subrs;
      decoder->subrs      = cid_subrs->code;
      decoder->subrs_len  = 0;
      decoder->subrs_hash = NULL;

      /* Set up font matrix */
      dict                 = cid->font_dicts + fd_select;

      decoder->font_matrix = dict->font_matrix;
      decoder->font_offset = dict->font_offset;
      decoder->lenIV       = dict->private_dict.lenIV;

      /* Decode the charstring. */

      /* Adjustment for seed bytes. */
      cs_offset = decoder->lenIV >= 0 ? (FT_TS_UInt)decoder->lenIV : 0;
      if ( cs_offset > glyph_length )
      {
        FT_TS_TRACE0(( "cid_load_glyph: invalid glyph stream offsets\n" ));
        error = FT_TS_THROW( Invalid_Offset );
        goto Exit;
      }

      /* Decrypt only if lenIV >= 0. */
      if ( decoder->lenIV >= 0 )
        psaux->t1_decrypt( charstring, glyph_length, 4330 );

      /* choose which renderer to use */
#ifdef T1_CONFIG_OPTION_OLD_ENGINE
      if ( ( (PS_Driver)FT_TS_FACE_DRIVER( face ) )->hinting_engine ==
               FT_TS_HINTING_FREETYPE                                  ||
           decoder->builder.metrics_only                            )
        error = psaux->t1_decoder_funcs->parse_charstrings_old(
                  decoder,
                  charstring + cs_offset,
                  glyph_length - cs_offset );
#else
      if ( decoder->builder.metrics_only )
        error = psaux->t1_decoder_funcs->parse_metrics(
                  decoder,
                  charstring + cs_offset,
                  glyph_length - cs_offset );
#endif
      else
      {
        PS_Decoder      psdecoder;
        CFF_SubFontRec  subfont;


        psaux->ps_decoder_init( &psdecoder, decoder, TRUE );

        psaux->t1_make_subfont( FT_TS_FACE( face ),
                                &dict->private_dict,
                                &subfont );
        psdecoder.current_subfont = &subfont;

        error = psaux->t1_decoder_funcs->parse_charstrings(
                  &psdecoder,
                  charstring + cs_offset,
                  glyph_length - cs_offset );

        /* Adobe's engine uses 16.16 numbers everywhere;              */
        /* as a consequence, glyphs larger than 2000ppem get rejected */
        if ( FT_TS_ERR_EQ( error, Glyph_Too_Big ) )
        {
          /* this time, we retry unhinted and scale up the glyph later on */
          /* (the engine uses and sets the hardcoded value 0x10000 / 64 = */
          /* 0x400 for both `x_scale' and `y_scale' in this case)         */
          ((CID_GlyphSlot)decoder->builder.glyph)->hint = FALSE;

          force_scaling = TRUE;

          error = psaux->t1_decoder_funcs->parse_charstrings(
                    &psdecoder,
                    charstring + cs_offset,
                    glyph_length - cs_offset );
        }
      }
    }

#ifdef FT_TS_CONFIG_OPTION_INCREMENTAL

    /* Incremental fonts can optionally override the metrics. */
    if ( !error && inc && inc->funcs->get_glyph_metrics )
    {
      FT_TS_Incremental_MetricsRec  metrics;


      metrics.bearing_x = FIXED_TO_INT( decoder->builder.left_bearing.x );
      metrics.bearing_y = 0;
      metrics.advance   = FIXED_TO_INT( decoder->builder.advance.x );
      metrics.advance_v = FIXED_TO_INT( decoder->builder.advance.y );

      error = inc->funcs->get_glyph_metrics( inc->object,
                                             glyph_index, FALSE, &metrics );

      decoder->builder.left_bearing.x = INT_TO_FIXED( metrics.bearing_x );
      decoder->builder.advance.x      = INT_TO_FIXED( metrics.advance );
      decoder->builder.advance.y      = INT_TO_FIXED( metrics.advance_v );
    }

#endif /* FT_TS_CONFIG_OPTION_INCREMENTAL */

  Exit:
    FT_TS_FREE( charstring );

    ((CID_GlyphSlot)decoder->builder.glyph)->scaled = force_scaling;

    return error;
  }


#if 0


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


  FT_TS_LOCAL_DEF( FT_TS_Error )
  cid_face_compute_max_advance( CID_Face  face,
                                FT_TS_Int*   max_advance )
  {
    FT_TS_Error       error;
    T1_DecoderRec  decoder;
    FT_TS_Int         glyph_index;

    PSAux_Service  psaux = (PSAux_Service)face->psaux;


    *max_advance = 0;

    /* Initialize load decoder */
    error = psaux->t1_decoder_funcs->init( &decoder,
                                           (FT_TS_Face)face,
                                           0, /* size       */
                                           0, /* glyph slot */
                                           0, /* glyph names! XXX */
                                           0, /* blend == 0 */
                                           0, /* hinting == 0 */
                                           cid_load_glyph );
    if ( error )
      return error;

    /* TODO: initialize decoder.len_buildchar and decoder.buildchar */
    /*       if we ever support CID-keyed multiple master fonts     */

    decoder.builder.metrics_only = 1;
    decoder.builder.load_points  = 0;

    /* for each glyph, parse the glyph charstring and extract */
    /* the advance width                                      */
    for ( glyph_index = 0; glyph_index < face->root.num_glyphs;
          glyph_index++ )
    {
      /* now get load the unscaled outline */
      error = cid_load_glyph( &decoder, glyph_index );
      /* ignore the error if one occurred - skip to next glyph */
    }

    *max_advance = FIXED_TO_INT( decoder.builder.advance.x );

    psaux->t1_decoder_funcs->done( &decoder );

    return FT_TS_Err_Ok;
  }


#endif /* 0 */


  FT_TS_LOCAL_DEF( FT_TS_Error )
  cid_slot_load_glyph( FT_TS_GlyphSlot  cidglyph,      /* CID_GlyphSlot */
                       FT_TS_Size       cidsize,       /* CID_Size      */
                       FT_TS_UInt       glyph_index,
                       FT_TS_Int32      load_flags )
  {
    CID_GlyphSlot  glyph = (CID_GlyphSlot)cidglyph;
    FT_TS_Error       error;
    T1_DecoderRec  decoder;
    CID_Face       face = (CID_Face)cidglyph->face;
    FT_TS_Bool        hinting;
    FT_TS_Bool        scaled;

    PSAux_Service  psaux = (PSAux_Service)face->psaux;
    FT_TS_Matrix      font_matrix;
    FT_TS_Vector      font_offset;
    FT_TS_Bool        must_finish_decoder = FALSE;


    if ( glyph_index >= (FT_TS_UInt)face->root.num_glyphs )
    {
      error = FT_TS_THROW( Invalid_Argument );
      goto Exit;
    }

    if ( load_flags & FT_TS_LOAD_NO_RECURSE )
      load_flags |= FT_TS_LOAD_NO_SCALE | FT_TS_LOAD_NO_HINTING;

    glyph->x_scale = cidsize->metrics.x_scale;
    glyph->y_scale = cidsize->metrics.y_scale;

    cidglyph->outline.n_points   = 0;
    cidglyph->outline.n_contours = 0;

    hinting = FT_TS_BOOL( ( load_flags & FT_TS_LOAD_NO_SCALE   ) == 0 &&
                       ( load_flags & FT_TS_LOAD_NO_HINTING ) == 0 );
    scaled  = FT_TS_BOOL( ( load_flags & FT_TS_LOAD_NO_SCALE   ) == 0 );

    glyph->hint      = hinting;
    glyph->scaled    = scaled;
    cidglyph->format = FT_TS_GLYPH_FORMAT_OUTLINE;

    error = psaux->t1_decoder_funcs->init( &decoder,
                                           cidglyph->face,
                                           cidsize,
                                           cidglyph,
                                           0, /* glyph names -- XXX */
                                           0, /* blend == 0 */
                                           hinting,
                                           FT_TS_LOAD_TARGET_MODE( load_flags ),
                                           cid_load_glyph );
    if ( error )
      goto Exit;

    /* TODO: initialize decoder.len_buildchar and decoder.buildchar */
    /*       if we ever support CID-keyed multiple master fonts     */

    must_finish_decoder = TRUE;

    /* set up the decoder */
    decoder.builder.no_recurse = FT_TS_BOOL( load_flags & FT_TS_LOAD_NO_RECURSE );

    error = cid_load_glyph( &decoder, glyph_index );
    if ( error )
      goto Exit;

    /* copy flags back for forced scaling */
    hinting = glyph->hint;
    scaled  = glyph->scaled;

    font_matrix = decoder.font_matrix;
    font_offset = decoder.font_offset;

    /* save new glyph tables */
    psaux->t1_decoder_funcs->done( &decoder );

    must_finish_decoder = FALSE;

    /* now set the metrics -- this is rather simple, as    */
    /* the left side bearing is the xMin, and the top side */
    /* bearing the yMax                                    */
    cidglyph->outline.flags &= FT_TS_OUTLINE_OWNER;
    cidglyph->outline.flags |= FT_TS_OUTLINE_REVERSE_FILL;

    /* for composite glyphs, return only left side bearing and */
    /* advance width                                           */
    if ( load_flags & FT_TS_LOAD_NO_RECURSE )
    {
      FT_TS_Slot_Internal  internal = cidglyph->internal;


      cidglyph->metrics.horiBearingX =
        FIXED_TO_INT( decoder.builder.left_bearing.x );
      cidglyph->metrics.horiAdvance =
        FIXED_TO_INT( decoder.builder.advance.x );

      internal->glyph_matrix      = font_matrix;
      internal->glyph_delta       = font_offset;
      internal->glyph_transformed = 1;
    }
    else
    {
      FT_TS_BBox            cbox;
      FT_TS_Glyph_Metrics*  metrics = &cidglyph->metrics;


      /* copy the _unscaled_ advance width */
      metrics->horiAdvance =
        FIXED_TO_INT( decoder.builder.advance.x );
      cidglyph->linearHoriAdvance =
        FIXED_TO_INT( decoder.builder.advance.x );
      cidglyph->internal->glyph_transformed = 0;

      /* make up vertical ones */
      metrics->vertAdvance        = ( face->cid.font_bbox.yMax -
                                      face->cid.font_bbox.yMin ) >> 16;
      cidglyph->linearVertAdvance = metrics->vertAdvance;

      cidglyph->format            = FT_TS_GLYPH_FORMAT_OUTLINE;

      if ( cidsize->metrics.y_ppem < 24 )
        cidglyph->outline.flags |= FT_TS_OUTLINE_HIGH_PRECISION;

      /* apply the font matrix, if any */
      if ( font_matrix.xx != 0x10000L || font_matrix.yy != 0x10000L ||
           font_matrix.xy != 0        || font_matrix.yx != 0        )
      {
        FT_TS_Outline_Transform( &cidglyph->outline, &font_matrix );

        metrics->horiAdvance = FT_TS_MulFix( metrics->horiAdvance,
                                          font_matrix.xx );
        metrics->vertAdvance = FT_TS_MulFix( metrics->vertAdvance,
                                          font_matrix.yy );
      }

      if ( font_offset.x || font_offset.y )
      {
        FT_TS_Outline_Translate( &cidglyph->outline,
                              font_offset.x,
                              font_offset.y );

        metrics->horiAdvance += font_offset.x;
        metrics->vertAdvance += font_offset.y;
      }

      if ( ( load_flags & FT_TS_LOAD_NO_SCALE ) == 0 || scaled )
      {
        /* scale the outline and the metrics */
        FT_TS_Int       n;
        FT_TS_Outline*  cur = decoder.builder.base;
        FT_TS_Vector*   vec = cur->points;
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
      FT_TS_Outline_Get_CBox( &cidglyph->outline, &cbox );

      metrics->width  = cbox.xMax - cbox.xMin;
      metrics->height = cbox.yMax - cbox.yMin;

      metrics->horiBearingX = cbox.xMin;
      metrics->horiBearingY = cbox.yMax;

      if ( load_flags & FT_TS_LOAD_VERTICAL_LAYOUT )
      {
        /* make up vertical ones */
        ft_synthesize_vertical_metrics( metrics,
                                        metrics->vertAdvance );
      }
    }

  Exit:

    if ( must_finish_decoder )
      psaux->t1_decoder_funcs->done( &decoder );

    return error;
  }


/* END */
