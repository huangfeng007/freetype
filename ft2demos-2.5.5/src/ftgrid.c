/****************************************************************************/
/*                                                                          */
/*  The FreeType project -- a free and portable quality TrueType renderer.  */
/*                                                                          */
/*  Copyright 1996-2000, 2003-2007, 2009-2014 by                            */
/*  D. Turner, R.Wilhelm, and W. Lemberg                                    */
/*                                                                          */
/*                                                                          */
/*  FTGrid - a simple viewer to show glyph outlines on a grid               */
/*                                                                          */
/*  Press F1 when running this program to have a list of key-bindings       */
/*                                                                          */
/****************************************************************************/


#include "ftcommon.h"
#include "common.h"
#include <math.h>

  /* the following header shouldn't be used in normal programs */
#include FT_INTERNAL_DEBUG_H

  /* showing driver name */
#include FT_MODULE_H
#include FT_INTERNAL_OBJECTS_H
#include FT_INTERNAL_DRIVER_H

#include FT_STROKER_H
#include FT_SYNTHESIS_H
#include FT_CFF_DRIVER_H
#include FT_TRUETYPE_DRIVER_H

#define MAXPTSIZE      500                 /* dtp */

#undef  CEIL
#define CEIL( x )  ( ( (x) + 63 ) >> 6 )

#define X_TOO_LONG( x, size, display ) \
          ( (x) + ( (size)->metrics.max_advance >> 6 ) > (display)->bitmap->width )
#define Y_TOO_LONG( y, size, display ) \
          ( (y) >= (display)->bitmap->rows )

#ifdef _WIN32
#define snprintf  _snprintf
#endif

#define N_CFF_HINTING_ENGINES  2


#ifdef FT_DEBUG_AUTOFIT
  /* these variables, structures and declarations are for   */
  /* communication with the debugger in the autofit module; */
  /* normal programs don't need this                        */
  struct  AF_GlyphHintsRec_;
  typedef struct AF_GlyphHintsRec_*  AF_GlyphHints;

  extern int            _af_debug_disable_horz_hints;
  extern int            _af_debug_disable_vert_hints;
  extern int            _af_debug_disable_blue_hints;
  extern AF_GlyphHints  _af_debug_hints;

#ifdef __cplusplus
  extern "C" {
#endif
  extern void
  af_glyph_hints_dump_segments( AF_GlyphHints  hints,
                                FT_Bool        to_stdout );
  extern void
  af_glyph_hints_dump_points( AF_GlyphHints  hints,
                              FT_Bool        to_stdout );
  extern void
  af_glyph_hints_dump_edges( AF_GlyphHints  hints,
                             FT_Bool        to_stdout );
  extern FT_Error
  af_glyph_hints_get_num_segments( AF_GlyphHints  hints,
                                   FT_Int         dimension,
                                   FT_Int*        num_segments );
  extern FT_Error
  af_glyph_hints_get_segment_offset( AF_GlyphHints  hints,
                                     FT_Int         dimension,
                                     FT_Int         idx,
                                     FT_Pos        *offset,
                                     FT_Bool       *is_blue,
                                     FT_Pos        *blue_offset );
#ifdef __cplusplus
  }
#endif

#endif /* FT_DEBUG_AUTOFIT */

  typedef struct  GridStatusRec_
  {
    int          width;
    int          height;

    int          ptsize;
    int          res;
    int          Num;  /* glyph index */
    int          font_index;

    double       scale;
    double       x_origin;
    double       y_origin;
    double       margin;

    double       scale_0;
    double       x_origin_0;
    double       y_origin_0;

    int          disp_width;
    int          disp_height;
    grBitmap*    disp_bitmap;

    grColor      axis_color;
    grColor      grid_color;
    grColor      outline_color;
    grColor      on_color;
    grColor      off_color;
    grColor      segment_color;
    grColor      blue_color;

    int          do_horz_hints;
    int          do_vert_hints;
    int          do_blue_hints;
    int          do_outline;
    int          do_dots;
    int          do_segment;

    double       gamma;
    const char*  header;
    char         header_buffer[256];

    int          cff_hinting_engine;
    int          tt_interpreter_version;

  } GridStatusRec, *GridStatus;

  static GridStatusRec  status;


  static void
  grid_status_init( GridStatus  st )
  {
    st->width         = DIM_X;
    st->height        = DIM_Y;

    st->scale         = 1.0;
    st->x_origin      = 0;
    st->y_origin      = 0;
    st->margin        = 0.05;

    st->do_horz_hints = 1;
    st->do_vert_hints = 1;
    st->do_blue_hints = 1;
    st->do_dots       = 1;
    st->do_outline    = 1;
    st->do_segment    = 0;

    st->Num           = 0;
    st->gamma         = 1.0;
    st->header        = "";
  }


  static void
  grid_status_display( GridStatus       st,
                       FTDemo_Display*  display )
  {
    st->disp_width    = display->bitmap->width;
    st->disp_height   = display->bitmap->rows;
    st->disp_bitmap   = display->bitmap;

    st->axis_color    = grFindColor( display->bitmap,   0,   0,   0, 255 ); /* black       */
    st->grid_color    = grFindColor( display->bitmap, 192, 192, 192, 255 ); /* gray        */
    st->outline_color = grFindColor( display->bitmap, 255,   0,   0, 255 ); /* red         */
    st->on_color      = grFindColor( display->bitmap, 255,   0,   0, 255 ); /* red         */
    st->off_color     = grFindColor( display->bitmap,   0, 128,   0, 255 ); /* dark green  */
    st->segment_color = grFindColor( display->bitmap,  64, 255, 128,  64 ); /* light green */
    st->blue_color    = grFindColor( display->bitmap,  64,  64, 255,  64 ); /* light blue  */
  }


  static void
  grid_status_rescale_initial( GridStatus      st,
                               FTDemo_Handle*  handle )
  {
    FT_Size   size;
    FT_Error  err = FTDemo_Get_Size( handle, &size );


    if ( !err )
    {
      FT_Face  face = size->face;

      int  xmin = FT_MulFix( face->bbox.xMin, size->metrics.x_scale );
      int  ymin = FT_MulFix( face->bbox.yMin, size->metrics.y_scale );
      int  xmax = FT_MulFix( face->bbox.xMax, size->metrics.x_scale );
      int  ymax = FT_MulFix( face->bbox.yMax, size->metrics.y_scale );

      double  x_scale, y_scale;


      xmin &= ~63;
      ymin &= ~63;
      xmax  = ( xmax + 63 ) & ~63;
      ymax  = ( ymax + 63 ) & ~63;

      if ( xmax - xmin )
        x_scale = st->disp_width  * ( 1.0 - 2 * st->margin ) / ( xmax - xmin );
      else
        x_scale = 1.0;

      if ( ymax - ymin )
        y_scale = st->disp_height * ( 1.0 - 2 * st->margin ) / ( ymax - ymin );
      else
        y_scale = 1.0;

      if ( x_scale <= y_scale )
        st->scale = x_scale;
      else
        st->scale = y_scale;

      st->x_origin = st->disp_width  * st->margin         - xmin * st->scale;
      st->y_origin = st->disp_height * ( 1 - st->margin ) + ymin * st->scale;
    }
    else
    {
      st->scale    = 1.;
      st->x_origin = st->disp_width  * st->margin;
      st->y_origin = st->disp_height * st->margin;
    }

    st->scale_0    = st->scale;
    st->x_origin_0 = st->x_origin;
    st->y_origin_0 = st->y_origin;
  }


  static void
  grid_status_draw_grid( GridStatus  st )
  {
    int     x_org   = (int)st->x_origin;
    int     y_org   = (int)st->y_origin;
    double  xy_incr = 64.0 * st->scale;


    if ( xy_incr >= 2. )
    {
      double  x2 = x_org;
      double  y2 = y_org;


      for ( ; x2 < st->disp_width; x2 += xy_incr )
        grFillVLine( st->disp_bitmap, (int)x2, 0,
                     st->disp_height, st->grid_color );

      for ( x2 = x_org - xy_incr; (int)x2 >= 0; x2 -= xy_incr )
        grFillVLine( st->disp_bitmap, (int)x2, 0,
                     st->disp_height, st->grid_color );

      for ( ; y2 < st->disp_height; y2 += xy_incr )
        grFillHLine( st->disp_bitmap, 0, (int)y2,
                     st->disp_width, st->grid_color );

      for ( y2 = y_org - xy_incr; (int)y2 >= 0; y2 -= xy_incr )
        grFillHLine( st->disp_bitmap, 0, (int)y2,
                     st->disp_width, st->grid_color );
    }

    grFillVLine( st->disp_bitmap, x_org, 0,
                 st->disp_height, st->axis_color );
    grFillHLine( st->disp_bitmap, 0, y_org,
                 st->disp_width,  st->axis_color );
  }


#ifdef FT_DEBUG_AUTOFIT

  static void
  grid_hint_draw_segment( GridStatus     st,
                          AF_GlyphHints  hints )
  {
    FT_Int  dimension;
    int     x_org = (int)st->x_origin;
    int     y_org = (int)st->y_origin;


    for ( dimension = 1; dimension >= 0; dimension-- )
    {
      FT_Int  num_seg;
      FT_Int  count;


      af_glyph_hints_get_num_segments( hints, dimension, &num_seg );

      for ( count = 0; count < num_seg; count++ )
      {
        int      pos;
        FT_Pos   offset;
        FT_Bool  is_blue;
        FT_Pos   blue_offset;


        af_glyph_hints_get_segment_offset( hints, dimension,
                                           count, &offset,
                                           &is_blue, &blue_offset);

        if ( dimension == 0 ) /* AF_DIMENSION_HORZ is 0 */
        {
          pos = x_org + (int)offset * st->scale;
          grFillVLine( st->disp_bitmap, pos, 0,
                       st->disp_height, st->segment_color );
        }
        else
        {
          pos = y_org - (int)offset * st->scale;

          if ( is_blue )
          {
            int  blue_pos = y_org - (int)blue_offset * st->scale;


            if ( blue_pos == pos )
              grFillHLine( st->disp_bitmap, 0, blue_pos,
                           st->disp_width, st->blue_color );
            else
            {
              grFillHLine( st->disp_bitmap, 0, blue_pos,
                           st->disp_width, st->blue_color );
              grFillHLine( st->disp_bitmap, 0, pos,
                           st->disp_width, st->segment_color );
            }
          }
          else
            grFillHLine( st->disp_bitmap, 0, pos,
                         st->disp_width, st->segment_color );
        }
      }
    }
  }

#endif /* FT_DEBUG_AUTOFIT */


  static void
  ft_bitmap_draw( FT_Bitmap*       bitmap,
                  int              x,
                  int              y,
                  FTDemo_Display*  display,
                  grColor          color )
  {
    grBitmap  gbit;


    gbit.width  = bitmap->width;
    gbit.rows   = bitmap->rows;
    gbit.pitch  = bitmap->pitch;
    gbit.buffer = bitmap->buffer;

    switch ( bitmap->pixel_mode )
    {
    case FT_PIXEL_MODE_GRAY:
      gbit.mode  = gr_pixel_mode_gray;
      gbit.grays = 256;
      break;

    case FT_PIXEL_MODE_MONO:
      gbit.mode  = gr_pixel_mode_mono;
      gbit.grays = 2;
      break;

    case FT_PIXEL_MODE_LCD:
      gbit.mode  = gr_pixel_mode_lcd;
      gbit.grays = 256;
      break;

    case FT_PIXEL_MODE_LCD_V:
      gbit.mode  = gr_pixel_mode_lcdv;
      gbit.grays = 256;
      break;

    default:
      return;
    }

    grBlitGlyphToBitmap( display->bitmap, &gbit, x, y, color );
  }


  static void
  ft_outline_draw( FT_Outline*      outline,
                   double           scale,
                   int              pen_x,
                   int              pen_y,
                   FTDemo_Handle*   handle,
                   FTDemo_Display*  display,
                   grColor          color )
  {
    FT_Outline  transformed;
    FT_BBox     cbox;
    FT_Bitmap   bitm;


    FT_Outline_New( handle->library,
                    outline->n_points,
                    outline->n_contours,
                    &transformed );

    FT_Outline_Copy( outline, &transformed );

    if ( scale != 1. )
    {
      int  nn;


      for ( nn = 0; nn < transformed.n_points; nn++ )
      {
        FT_Vector*  vec = &transformed.points[nn];


        vec->x = (FT_F26Dot6)( vec->x * scale );
        vec->y = (FT_F26Dot6)( vec->y * scale );
      }
    }

    FT_Outline_Get_CBox( &transformed, &cbox );
    cbox.xMin &= ~63;
    cbox.yMin &= ~63;
    cbox.xMax  = ( cbox.xMax + 63 ) & ~63;
    cbox.yMax  = ( cbox.yMax + 63 ) & ~63;

    bitm.width      = ( cbox.xMax - cbox.xMin ) >> 6;
    bitm.rows       = ( cbox.yMax - cbox.yMin ) >> 6;
    bitm.pitch      = bitm.width;
    bitm.num_grays  = 256;
    bitm.pixel_mode = FT_PIXEL_MODE_GRAY;
    bitm.buffer     = (unsigned char*)calloc( bitm.pitch, bitm.rows );

    FT_Outline_Translate( &transformed, -cbox.xMin, -cbox.yMin );
    FT_Outline_Get_Bitmap( handle->library, &transformed, &bitm );

    ft_bitmap_draw( &bitm,
                    pen_x + ( cbox.xMin >> 6 ),
                    pen_y - ( cbox.yMax >> 6 ),
                    display,
                    color );

    free( bitm.buffer );
    FT_Outline_Done( handle->library, &transformed );
  }


  static void
  ft_outline_new_circle( FT_Outline*     outline,
                         FT_F26Dot6      radius,
                         FTDemo_Handle*  handle )
  {
    char*       tag;
    FT_Vector*  vec;
    FT_F26Dot6  disp = (FT_F26Dot6)( radius * 0.6781 );


    FT_Outline_New( handle->library, 12, 1, outline );
    outline->n_points    = 12;
    outline->n_contours  = 1;
    outline->contours[0] = outline->n_points - 1;

    vec = outline->points;
    tag = outline->tags;

    vec->x =  radius; vec->y =       0; vec++; *tag++ = FT_CURVE_TAG_ON;
    vec->x =  radius; vec->y =    disp; vec++; *tag++ = FT_CURVE_TAG_CUBIC;
    vec->x =    disp; vec->y =  radius; vec++; *tag++ = FT_CURVE_TAG_CUBIC;
    vec->x =       0; vec->y =  radius; vec++; *tag++ = FT_CURVE_TAG_ON;
    vec->x =   -disp; vec->y =  radius; vec++; *tag++ = FT_CURVE_TAG_CUBIC;
    vec->x = -radius; vec->y =    disp; vec++; *tag++ = FT_CURVE_TAG_CUBIC;
    vec->x = -radius; vec->y =       0; vec++; *tag++ = FT_CURVE_TAG_ON;
    vec->x = -radius; vec->y =   -disp; vec++; *tag++ = FT_CURVE_TAG_CUBIC;
    vec->x =   -disp; vec->y = -radius; vec++; *tag++ = FT_CURVE_TAG_CUBIC;
    vec->x =       0; vec->y = -radius; vec++; *tag++ = FT_CURVE_TAG_ON;
    vec->x =    disp; vec->y = -radius; vec++; *tag++ = FT_CURVE_TAG_CUBIC;
    vec->x =  radius; vec->y =   -disp; vec++; *tag++ = FT_CURVE_TAG_CUBIC;
  }


  static void
  circle_draw( FT_F26Dot6       center_x,
               FT_F26Dot6       center_y,
               FT_F26Dot6       radius,
               FTDemo_Handle*   handle,
               FTDemo_Display*  display,
               grColor          color )
  {
    FT_Outline  outline;


    ft_outline_new_circle( &outline, radius, handle );
    FT_Outline_Translate( &outline, center_x & 63, center_y & 63 );

    ft_outline_draw( &outline, 1., ( center_x >> 6 ), ( center_y >> 6 ),
                     handle, display, color );

    FT_Outline_Done( handle->library, &outline );
  }


  static void
  grid_status_draw_outline( GridStatus       st,
                            FTDemo_Handle*   handle,
                            FTDemo_Display*  display )
  {
    static FT_Stroker  stroker;
    FT_Size            size;
    FT_GlyphSlot       slot;
    double             scale = 64.0 * st->scale;
    int                ox    = (int)st->x_origin;
    int                oy    = (int)st->y_origin;


    if ( stroker == NULL )
    {
      FT_Stroker_New( handle->library, &stroker );

      FT_Stroker_Set( stroker, 32, FT_STROKER_LINECAP_BUTT,
                      FT_STROKER_LINEJOIN_ROUND, 0x20000 );
    }

    FTDemo_Get_Size( handle, &size );

#ifdef FT_DEBUG_AUTOFIT
    /* Draw segment before drawing glyph. */
    if ( status.do_segment )
    {
      /* Force hinting first in order to collect segment info. */
      _af_debug_disable_horz_hints = 0;
      _af_debug_disable_vert_hints = 0;

      if ( !FT_Load_Glyph( size->face, st->Num,
                           FT_LOAD_DEFAULT        |
                           FT_LOAD_NO_BITMAP      |
                           FT_LOAD_FORCE_AUTOHINT |
                           FT_LOAD_TARGET_NORMAL ) )
        grid_hint_draw_segment( &status, _af_debug_hints );
    }

    _af_debug_disable_horz_hints = !st->do_horz_hints;
    _af_debug_disable_vert_hints = !st->do_vert_hints;
#endif

    if ( FT_Load_Glyph( size->face, st->Num,
                        handle->load_flags | FT_LOAD_NO_BITMAP ) )
      return;

    slot = size->face->glyph;
    if ( slot->format == FT_GLYPH_FORMAT_OUTLINE )
    {
      FT_Glyph     glyph;
      FT_Outline*  gimage = &slot->outline;
      int          nn;


      /* scale the outline */
      for ( nn = 0; nn < gimage->n_points; nn++ )
      {
        FT_Vector*  vec = &gimage->points[nn];


        vec->x = (FT_F26Dot6)( vec->x * scale );
        vec->y = (FT_F26Dot6)( vec->y * scale );
      }

      /* stroke then draw it */
      if ( st->do_outline )
      {
        FT_Get_Glyph( slot, &glyph );
        FT_Glyph_Stroke( &glyph, stroker, 1 );

        error = FTDemo_Draw_Glyph_Color( handle, display, glyph, &ox, &oy,
                                         st->outline_color );
        if ( !error )
          FT_Done_Glyph( glyph );
      }

      /* now draw the points */
      if ( st->do_dots )
      {
        for ( nn = 0; nn < gimage->n_points; nn++ )
          circle_draw(
            (FT_F26Dot6)( st->x_origin * 64 + gimage->points[nn].x ),
            (FT_F26Dot6)( st->y_origin * 64 - gimage->points[nn].y ),
            128,
            handle,
            display,
            ( gimage->tags[nn] & FT_CURVE_TAG_ON ) ? st->on_color
                                                   : st->off_color );
      }
    }
  }


  static FTDemo_Display*  display;
  static FTDemo_Handle*   handle;

#if 0
  static const unsigned char*  Text = (unsigned char*)
    "The quick brown fox jumps over the lazy dog 0123456789 "
    "\342\352\356\373\364\344\353\357\366\374\377\340\371\351\350\347 "
    "&#~\"\'(-`_^@)=+\260 ABCDEFGHIJKLMNOPQRSTUVWXYZ "
    "$\243^\250*\265\371%!\247:/;.,?<>";
#endif


  static void
  Fatal( const char*  message )
  {
    FTDemo_Display_Done( display );
    FTDemo_Done( handle );
    PanicZ( message );
  }


  /*************************************************************************/
  /*************************************************************************/
  /*****                                                               *****/
  /*****                REST OF THE APPLICATION/PROGRAM                *****/
  /*****                                                               *****/
  /*************************************************************************/
  /*************************************************************************/

  static void
  event_help( void )
  {
    char  buf[256];
    char  version[64];

    const char*  format;
    FT_Int       major, minor, patch;

    grEvent  dummy_event;


    FT_Library_Version( handle->library, &major, &minor, &patch );

    format = patch ? "%d.%d.%d" : "%d.%d";
    sprintf( version, format, major, minor, patch );

    FTDemo_Display_Clear( display );
    grSetLineHeight( 10 );
    grGotoxy( 0, 0 );
    grSetMargin( 2, 1 );
    grGotobitmap( display->bitmap );

    sprintf( buf,
            "FreeType Glyph Grid Viewer - part of the FreeType %s test suite",
             version );

    grWriteln( buf );
    grLn();
    grWriteln( "Use the following keys:" );
    grLn();
    /*          |----------------------------------|    |----------------------------------| */
#ifdef FT_DEBUG_AUTOFIT
    grWriteln( "F1, ?       display this help screen    if autohinting:                     " );
    grWriteln( "                                          H         toggle horiz. hinting   " );
    grWriteln( "i, k        move grid up/down             V         toggle vert. hinting    " );
    grWriteln( "j, l        move grid left/right          B         toggle blue zone hinting" );
    grWriteln( "PgUp, PgDn  zoom in/out grid              s         toggle segment drawing  " );
    grWriteln( "SPC         reset zoom and position                 (unfitted, with blues)  " );
    grWriteln( "                                          1         dump edge hints         " );
    grWriteln( "p, n        previous/next font            2         dump segment hints      " );
    grWriteln( "                                          3         dump point hints        " );
#else
    grWriteln( "F1, ?       display this help screen    i, k        move grid up/down       " );
    grWriteln( "                                        j, l        move grid left/right    " );
    grWriteln( "p, n        previous/next font          PgUp, PgDn  zoom in/out grid        " );
    grWriteln( "                                        SPC         reset zoom and position " );
#endif /* FT_DEBUG_AUTOFIT */
    grWriteln( "Up, Down    adjust size by 0.5pt                                            " );
    grWriteln( "                                        if not autohinting:                 " );
    grWriteln( "Left, Right adjust index by 1             H         cycle through hinting   " );
    grWriteln( "F7, F8      adjust index by 10                        engines (if available)" );
    grWriteln( "F9, F10     adjust index by 100                                             " );
    grWriteln( "F11, F12    adjust index by 1000        d           toggle dots display     " );
    grWriteln( "                                        o           toggle outline display  " );
    grWriteln( "h           toggle hinting                                                  " );
    grWriteln( "f           toggle forced auto-                                             " );
    grWriteln( "             hinting (if hinting)       g, v        adjust gamma value      " );
    grWriteln( "                                                                            " );
    grWriteln( "a           toggle anti-aliasing        q, ESC      quit ftgrid             " );
    /*          |----------------------------------|    |----------------------------------| */
    grLn();
    grLn();
    grWriteln( "press any key to exit this help screen" );

    grRefreshSurface( display->surface );
    grListenSurface( display->surface, gr_event_key, &dummy_event );
  }


  static void
  event_cff_hinting_engine_change( int  delta )
  {
    int  new_cff_hinting_engine;


    if ( delta )
      new_cff_hinting_engine =
        ( status.cff_hinting_engine +
          delta                     +
          N_CFF_HINTING_ENGINES     ) % N_CFF_HINTING_ENGINES;

    error = FT_Property_Set( handle->library,
                             "cff",
                             "hinting-engine",
                             &new_cff_hinting_engine );

    if ( !error )
    {
      /* Resetting the cache is perhaps a bit harsh, but I'm too  */
      /* lazy to walk over all loaded fonts to check whether they */
      /* are of type CFF, then unloading them explicitly.         */
      FTC_Manager_Reset( handle->cache_manager );
      status.cff_hinting_engine = new_cff_hinting_engine;
    }

    sprintf( status.header_buffer, "CFF engine changed to %s",
             status.cff_hinting_engine == FT_CFF_HINTING_FREETYPE
               ? "FreeType" : "Adobe" );

    status.header = (const char *)status.header_buffer;
  }


  static void
  event_tt_interpreter_version_change( void )
  {
    FT_UInt  new_interpreter_version;


    if ( status.tt_interpreter_version == TT_INTERPRETER_VERSION_35 )
      new_interpreter_version = TT_INTERPRETER_VERSION_38;
    else
      new_interpreter_version = TT_INTERPRETER_VERSION_35;

    error = FT_Property_Set( handle->library,
                             "truetype",
                             "interpreter-version",
                             &new_interpreter_version );

    if ( !error )
    {
      /* Resetting the cache is perhaps a bit harsh, but I'm too  */
      /* lazy to walk over all loaded fonts to check whether they */
      /* are of type TTF, then unloading them explicitly.         */
      FTC_Manager_Reset( handle->cache_manager );
      status.tt_interpreter_version = new_interpreter_version;
    }

    sprintf( status.header_buffer, "TrueType engine changed to version %s",
             status.tt_interpreter_version == TT_INTERPRETER_VERSION_35
               ? "35" : "38" );

    status.header = (const char *)status.header_buffer;
  }


  static void
  event_gamma_change( double  delta )
  {
    status.gamma += delta;

    if ( status.gamma > 3.0 )
      status.gamma = 3.0;
    else if ( status.gamma < 0.0 )
      status.gamma = 0.0;

    grSetGlyphGamma( status.gamma );

    sprintf( status.header_buffer, "gamma changed to %.1f%s",
             status.gamma, status.gamma == 0.0 ? " (sRGB mode)" : "" );

    status.header = (const char *)status.header_buffer;
  }


  static void
  event_grid_reset( GridStatus  st )
  {
    st->x_origin = st->x_origin_0;
    st->y_origin = st->y_origin_0;
    st->scale    = st->scale_0;
  }


  static void
  event_grid_translate( int  dx,
                        int  dy )
  {
    status.x_origin += 32 * dx;
    status.y_origin += 32 * dy;
  }


  static void
  event_grid_zoom( double  zoom )
  {
    status.scale *= zoom;

    sprintf( status.header_buffer, "zoom level %.0f%%",
             status.scale * 100.0 / status.scale_0 );

    status.header = (const char *)status.header_buffer;
  }


  static void
  event_size_change( int  delta )
  {
    status.ptsize += delta;

    if ( status.ptsize < 1 * 64 )
      status.ptsize = 1 * 64;
    else if ( status.ptsize > MAXPTSIZE * 64 )
      status.ptsize = MAXPTSIZE * 64;

    FTDemo_Set_Current_Charsize( handle, status.ptsize, status.res );
  }


  static void
  event_index_change( int  delta )
  {
    int  num_indices = handle->current_font->num_indices;


    status.Num += delta;

    if ( status.Num < 0 )
      status.Num = 0;
    else if ( status.Num >= num_indices )
      status.Num = num_indices - 1;
  }


  static void
  event_font_change( int  delta )
  {
    int  num_indices;


    if ( status.font_index + delta >= handle->num_fonts ||
         status.font_index + delta < 0                  )
      return;

    status.font_index += delta;

    FTDemo_Set_Current_Font( handle, handle->fonts[status.font_index] );
    FTDemo_Set_Current_Charsize( handle, status.ptsize, status.res );
    FTDemo_Update_Current_Flags( handle );

    num_indices = handle->current_font->num_indices;

    if ( status.Num >= num_indices )
      status.Num = num_indices - 1;
  }


  static int
  Process_Event( grEvent*  event )
  {
    int  ret = 0;


    status.header = NULL;

    switch ( event->key )
    {
    case grKeyEsc:
    case grKEY( 'q' ):
      ret = 1;
      break;

    case grKeyF1:
    case grKEY( '?' ):
      event_help();
      break;

    case grKEY( 'a' ):
      handle->antialias = !handle->antialias;
      status.header     = handle->antialias ? "anti-aliasing is now on"
                                            : "anti-aliasing is now off";

      FTDemo_Update_Current_Flags( handle );
      break;

    case grKEY( 'f' ):
      handle->autohint = !handle->autohint;
      status.header    = handle->autohint ? "forced auto-hinting is now on"
                                          : "forced auto-hinting is now off";

      FTDemo_Update_Current_Flags( handle );
      break;

#ifdef FT_DEBUG_AUTOFIT
    case grKEY( '1' ):
      if ( handle->hinted && handle->autohint )
      {
        status.header = "dumping glyph edges to stdout";
        af_glyph_hints_dump_edges( _af_debug_hints, 1 );
      }
      break;

    case grKEY( '2' ):
      if ( handle->hinted && handle->autohint )
      {
        status.header = "dumping glyph segments to stdout";
        af_glyph_hints_dump_segments( _af_debug_hints, 1 );
      }
      break;

    case grKEY( '3' ):
      if ( handle->hinted && handle->autohint )
      {
        status.header = "dumping glyph points to stdout";
        af_glyph_hints_dump_points( _af_debug_hints, 1 );
      }
      break;
#endif /* FT_DEBUG_AUTOFIT */

    case grKEY( 'g' ):
      event_gamma_change( 0.1 );
      break;

    case grKEY( 'v' ):
      event_gamma_change( -0.1 );
      break;

    case grKEY( 'n' ):
      event_font_change( 1 );
      break;

    case grKEY( 'h' ):
      handle->hinted = !handle->hinted;
      status.header  = handle->hinted ? "glyph hinting is now active"
                                      : "glyph hinting is now ignored";

      FTDemo_Update_Current_Flags( handle );
      break;

    case grKEY( 'd' ):
      status.do_dots = !status.do_dots;
      break;

    case grKEY( 'o' ):
      status.do_outline = !status.do_outline;
      break;

    case grKEY( 'p' ):
      event_font_change( -1 );
      break;

    case grKEY( 'H' ):
      if ( !handle->autohint )
      {
        FT_Face    face;
        FT_Module  module;


        error = FTC_Manager_LookupFace( handle->cache_manager,
                                        handle->scaler.face_id, &face );
        if ( !error )
        {
          module = &face->driver->root;

          if ( !strcmp( module->clazz->module_name, "cff" ) )
            event_cff_hinting_engine_change( 1 );
          else if ( !strcmp( module->clazz->module_name, "truetype" ) )
            event_tt_interpreter_version_change();
        }
      }
#ifdef FT_DEBUG_AUTOFIT
      else
      {
        status.do_horz_hints = !status.do_horz_hints;
        status.header = status.do_horz_hints ? "horizontal hinting enabled"
                                             : "horizontal hinting disabled";
      }
#endif
      break;

#ifdef FT_DEBUG_AUTOFIT
    case grKEY( 'V' ):
      if ( handle->autohint )
      {
        status.do_vert_hints = !status.do_vert_hints;
        status.header = status.do_vert_hints ? "vertical hinting enabled"
                                             : "vertical hinting disabled";
      }
      else
        status.header = "need autofit mode to toggle vertical hinting";
      break;

    case grKEY( 'B' ):
      if ( handle->autohint )
      {
        status.do_blue_hints = !status.do_blue_hints;
        status.header = status.do_blue_hints ? "blue zone hinting enabled"
                                             : "blue zone hinting disabled";
      }
      else
        status.header = "need autofit mode to toggle blue zone hinting";
      break;

    case grKEY( 's' ):
      status.do_segment = !status.do_segment;
      status.header = status.do_segment ? "segment drawing enabled"
                                        : "segment drawing disabled";
      break;
#endif /* FT_DEBUG_AUTOFIT */

    case grKeyLeft:     event_index_change(    -1 ); break;
    case grKeyRight:    event_index_change(     1 ); break;
    case grKeyF7:       event_index_change(   -10 ); break;
    case grKeyF8:       event_index_change(    10 ); break;
    case grKeyF9:       event_index_change(  -100 ); break;
    case grKeyF10:      event_index_change(   100 ); break;
    case grKeyF11:      event_index_change( -1000 ); break;
    case grKeyF12:      event_index_change(  1000 ); break;

    case grKeyUp:       event_size_change(  32 ); break;
    case grKeyDown:     event_size_change( -32 ); break;

    case grKEY( ' ' ):  event_grid_reset( &status );
                        status.do_horz_hints = 1;
                        status.do_vert_hints = 1;
                        break;

    case grKEY( 'i' ):  event_grid_translate(  0,  1 ); break;
    case grKEY( 'k' ):  event_grid_translate(  0, -1 ); break;
    case grKEY( 'l' ):  event_grid_translate(  1,  0 ); break;
    case grKEY( 'j' ):  event_grid_translate( -1,  0 ); break;

    case grKeyPageUp:   event_grid_zoom( 1.25     ); break;
    case grKeyPageDown: event_grid_zoom( 1 / 1.25 ); break;

    default:
      ;
    }

    return ret;
  }


  static void
  write_header( FT_Error  error_code )
  {
    FT_Face      face;
    const char*  basename;
    const char*  format;


    error = FTC_Manager_LookupFace( handle->cache_manager,
                                    handle->scaler.face_id, &face );
    if ( error )
      Fatal( "can't access font file" );

    if ( !status.header )
    {
      basename = ft_basename( handle->current_font->filepathname );

      switch ( error_code )
      {
      case FT_Err_Ok:
        sprintf( status.header_buffer, "%.50s %.50s (file `%.100s')",
                 face->family_name, face->style_name, basename );
        break;

      case FT_Err_Invalid_Pixel_Size:
        sprintf( status.header_buffer, "Invalid pixel size (file `%.100s')",
                 basename );
        break;

      case FT_Err_Invalid_PPem:
        sprintf( status.header_buffer, "Invalid ppem value (file `%.100s')",
                 basename );
        break;

      default:
        sprintf( status.header_buffer, "File `%.100s': error 0x%04x",
                 basename, (FT_UShort)error_code );
        break;
      }

      status.header = (const char *)status.header_buffer;
    }

    grWriteCellString( display->bitmap, 0, 0, status.header,
                       display->fore_color );

    format = "at %g points, first glyph index = %d";

    snprintf( status.header_buffer, 256, format,
              status.ptsize / 64., status.Num );

    if ( FT_HAS_GLYPH_NAMES( face ) )
    {
      char*  p;
      int    format_len, gindex, size;


      size = strlen( status.header_buffer );
      p    = status.header_buffer + size;
      size = 256 - size;

      format = ", name = ";
      format_len = strlen( format );

      if ( size >= format_len + 2 )
      {
        gindex = status.Num;

        strcpy( p, format );
        if ( FT_Get_Glyph_Name( face, gindex,
                                p + format_len, size - format_len ) )
          *p = '\0';
      }
    }

    status.header = (const char *)status.header_buffer;
    grWriteCellString( display->bitmap, 0, HEADER_HEIGHT,
                       status.header_buffer, display->fore_color );

    grRefreshSurface( display->surface );
  }


  static void
  usage( char*  execname )
  {
    fprintf( stderr,
      "\n"
      "ftgrid: simple glyph grid viewer -- part of the FreeType project\n"
      "----------------------------------------------------------------\n"
      "\n" );
    fprintf( stderr,
      "Usage: %s [options] pt font ...\n"
      "\n",
             execname );
    fprintf( stderr,
      "  pt        The point size for the given resolution.\n"
      "            If resolution is 72dpi, this directly gives the\n"
      "            ppem value (pixels per EM).\n" );
    fprintf( stderr,
      "  font      The font file(s) to display.\n"
      "            For Type 1 font files, ftgrid also tries to attach\n"
      "            the corresponding metrics file (with extension\n"
      "            `.afm' or `.pfm').\n"
      "\n" );
    fprintf( stderr,
      "  -w W      Set the window width to W pixels (default: %dpx).\n"
      "  -h H      Set the window height to H pixels (default: %dpx).\n"
      "\n",
             DIM_X, DIM_Y );
    fprintf( stderr,
      "  -r R      Use resolution R dpi (default: 72dpi).\n"
      "  -f index  Specify first index to display (default: 0).\n"
      "\n"
      "  -v        Show version."
      "\n" );

    exit( 1 );
  }


  static void
  parse_cmdline( int*    argc,
                 char**  argv[] )
  {
    char*  execname;
    int    option;


    execname = ft_basename( (*argv)[0] );

    while ( 1 )
    {
      option = getopt( *argc, *argv, "f:h:r:vw:" );

      if ( option == -1 )
        break;

      switch ( option )
      {
      case 'f':
        status.Num = atoi( optarg );
        break;

      case 'h':
        status.height = atoi( optarg );
        if ( status.height < 1 )
          usage( execname );
        break;

      case 'r':
        status.res = atoi( optarg );
        if ( status.res < 1 )
          usage( execname );
        break;

      case 'v':
        {
          FT_Int  major, minor, patch;


          FT_Library_Version( handle->library, &major, &minor, &patch );

          printf( "ftgrid (FreeType) %d.%d", major, minor );
          if ( patch )
            printf( ".%d", patch );
          printf( "\n" );
          exit( 0 );
        }
        break;

      case 'w':
        status.width = atoi( optarg );
        if ( status.width < 1 )
          usage( execname );
        break;

      default:
        usage( execname );
        break;
      }
    }

    *argc -= optind;
    *argv += optind;

    if ( *argc <= 1 )
      usage( execname );

    status.ptsize = (int)( atof( *argv[0] ) * 64.0 );
    if ( status.ptsize == 0 )
      status.ptsize = 64 * 10;

    if ( status.res <= 0 )
      status.res = 72;

    (*argc)--;
    (*argv)++;
  }


  int
  main( int    argc,
        char*  argv[] )
  {
    grEvent  event;


    /* initialize engine */
    handle = FTDemo_New();

    grid_status_init( &status );
    parse_cmdline( &argc, &argv );

    /* get the default value as compiled into FreeType */
    FT_Property_Get( handle->library,
                     "cff",
                     "hinting-engine", &status.cff_hinting_engine );
    FT_Property_Get( handle->library,
                     "truetype",
                     "interpreter-version", &status.tt_interpreter_version );

    display = FTDemo_Display_New( gr_pixel_mode_rgb24,
                                  status.width, status.height );
    if ( !display )
      Fatal( "could not allocate display surface" );

    grid_status_display( &status, display );

    grSetTitle( display->surface,
                "FreeType Glyph Grid Viewer - press F1 for help" );

    for ( ; argc > 0; argc--, argv++ )
    {
      error = FTDemo_Install_Font( handle, argv[0], 1 );
      if ( error == FT_Err_Invalid_Argument )
        fprintf( stderr, "skipping font `%s' without outlines\n",
                         argv[0] );
    }

    if ( handle->num_fonts == 0 )
      Fatal( "could not find/open any font file" );

    printf( "ptsize =%g\n", status.ptsize / 64.0 );
    FTDemo_Set_Current_Charsize( handle, status.ptsize, status.res );
    FTDemo_Update_Current_Flags( handle );

    event_font_change( 0 );

    grid_status_rescale_initial( &status, handle );

    for ( ;; )
    {
      FTDemo_Display_Clear( display );

      grid_status_draw_grid( &status );

      if ( status.do_outline || status.do_dots )
        grid_status_draw_outline( &status, handle, display );

      write_header( 0 );

      grListenSurface( display->surface, 0, &event );
      if ( Process_Event( &event ) )
        break;
    }

    printf( "Execution completed successfully.\n" );

    FTDemo_Display_Done( display );
    FTDemo_Done( handle );
    exit( 0 );      /* for safety reasons */

    return 0;       /* never reached */
  }


/* End */
