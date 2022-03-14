/****************************************************************************
 *
 * ftrend1.c
 *
 *   The FreeType glyph rasterizer interface (body).
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
#include <freetype/internal/ftobjs.h>
#include <freetype/ftoutln.h>
#include "ftrend1.h"
#include "ftraster.h"

#include "rasterrs.h"


  /* initialize renderer -- init its raster */
  static FT_TS_Error
  ft_raster1_init( FT_TS_Renderer  render )
  {
    render->clazz->raster_class->raster_reset( render->raster, NULL, 0 );

    return FT_TS_Err_Ok;
  }


  /* set render-specific mode */
  static FT_TS_Error
  ft_raster1_set_mode( FT_TS_Renderer  render,
                       FT_TS_ULong     mode_tag,
                       FT_TS_Pointer   data )
  {
    /* we simply pass it to the raster */
    return render->clazz->raster_class->raster_set_mode( render->raster,
                                                         mode_tag,
                                                         data );
  }


  /* transform a given glyph image */
  static FT_TS_Error
  ft_raster1_transform( FT_TS_Renderer       render,
                        FT_TS_GlyphSlot      slot,
                        const FT_TS_Matrix*  matrix,
                        const FT_TS_Vector*  delta )
  {
    FT_TS_Error error = FT_TS_Err_Ok;


    if ( slot->format != render->glyph_format )
    {
      error = FT_TS_THROW( Invalid_Argument );
      goto Exit;
    }

    if ( matrix )
      FT_TS_Outline_Transform( &slot->outline, matrix );

    if ( delta )
      FT_TS_Outline_Translate( &slot->outline, delta->x, delta->y );

  Exit:
    return error;
  }


  /* return the glyph's control box */
  static void
  ft_raster1_get_cbox( FT_TS_Renderer   render,
                       FT_TS_GlyphSlot  slot,
                       FT_TS_BBox*      cbox )
  {
    FT_TS_ZERO( cbox );

    if ( slot->format == render->glyph_format )
      FT_TS_Outline_Get_CBox( &slot->outline, cbox );
  }


  /* convert a slot's glyph image into a bitmap */
  static FT_TS_Error
  ft_raster1_render( FT_TS_Renderer       render,
                     FT_TS_GlyphSlot      slot,
                     FT_TS_Render_Mode    mode,
                     const FT_TS_Vector*  origin )
  {
    FT_TS_Error     error   = FT_TS_Err_Ok;
    FT_TS_Outline*  outline = &slot->outline;
    FT_TS_Bitmap*   bitmap  = &slot->bitmap;
    FT_TS_Memory    memory  = render->root.memory;
    FT_TS_Pos       x_shift = 0;
    FT_TS_Pos       y_shift = 0;

    FT_TS_Raster_Params  params;


    /* check glyph image format */
    if ( slot->format != render->glyph_format )
    {
      error = FT_TS_THROW( Invalid_Argument );
      goto Exit;
    }

    /* check rendering mode */
    if ( mode != FT_TS_RENDER_MODE_MONO )
    {
      /* raster1 is only capable of producing monochrome bitmaps */
      return FT_TS_THROW( Cannot_Render_Glyph );
    }

    /* release old bitmap buffer */
    if ( slot->internal->flags & FT_TS_GLYPH_OWN_BITMAP )
    {
      FT_TS_FREE( bitmap->buffer );
      slot->internal->flags &= ~FT_TS_GLYPH_OWN_BITMAP;
    }

    if ( ft_glyphslot_preset_bitmap( slot, mode, origin ) )
    {
      error = FT_TS_THROW( Raster_Overflow );
      goto Exit;
    }

    /* allocate new one */
    if ( FT_TS_ALLOC_MULT( bitmap->buffer, bitmap->rows, bitmap->pitch ) )
      goto Exit;

    slot->internal->flags |= FT_TS_GLYPH_OWN_BITMAP;

    x_shift = -slot->bitmap_left * 64;
    y_shift = ( (FT_TS_Int)bitmap->rows - slot->bitmap_top ) * 64;

    if ( origin )
    {
      x_shift += origin->x;
      y_shift += origin->y;
    }

    /* translate outline to render it into the bitmap */
    if ( x_shift || y_shift )
      FT_TS_Outline_Translate( outline, x_shift, y_shift );

    /* set up parameters */
    params.target = bitmap;
    params.source = outline;
    params.flags  = FT_TS_RASTER_FLAG_DEFAULT;

    /* render outline into the bitmap */
    error = render->raster_render( render->raster, &params );

  Exit:
    if ( !error )
      /* everything is fine; the glyph is now officially a bitmap */
      slot->format = FT_TS_GLYPH_FORMAT_BITMAP;
    else if ( slot->internal->flags & FT_TS_GLYPH_OWN_BITMAP )
    {
      FT_TS_FREE( bitmap->buffer );
      slot->internal->flags &= ~FT_TS_GLYPH_OWN_BITMAP;
    }

    if ( x_shift || y_shift )
      FT_TS_Outline_Translate( outline, -x_shift, -y_shift );

    return error;
  }


  FT_TS_DEFINE_RENDERER(
    ft_raster1_renderer_class,

      FT_TS_MODULE_RENDERER,
      sizeof ( FT_TS_RendererRec ),

      "raster1",
      0x10000L,
      0x20000L,

      NULL,    /* module specific interface */

      (FT_TS_Module_Constructor)ft_raster1_init,  /* module_init   */
      (FT_TS_Module_Destructor) NULL,             /* module_done   */
      (FT_TS_Module_Requester)  NULL,             /* get_interface */

    FT_TS_GLYPH_FORMAT_OUTLINE,

    (FT_TS_Renderer_RenderFunc)   ft_raster1_render,     /* render_glyph    */
    (FT_TS_Renderer_TransformFunc)ft_raster1_transform,  /* transform_glyph */
    (FT_TS_Renderer_GetCBoxFunc)  ft_raster1_get_cbox,   /* get_glyph_cbox  */
    (FT_TS_Renderer_SetModeFunc)  ft_raster1_set_mode,   /* set_mode        */

    (FT_TS_Raster_Funcs*)&ft_standard_raster             /* raster_class    */
  )


/* END */
