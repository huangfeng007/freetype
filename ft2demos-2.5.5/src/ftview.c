/****************************************************************************/
/*                                                                          */
/*  The FreeType project -- a free and portable quality TrueType renderer.  */
/*                                                                          */
/*  Copyright 1996-2000, 2003-2007, 2009-2014 by                            */
/*  D. Turner, R.Wilhelm, and W. Lemberg                                    */
/*                                                                          */
/*                                                                          */
/*  FTView - a simple font viewer.                                          */
/*                                                                          */
/*  This is a new version using the MiGS graphics subsystem for             */
/*  blitting and display.                                                   */
/*                                                                          */
/*  Press F1 when running this program to have a list of key-bindings       */
/*                                                                          */
/****************************************************************************/


#include "ftcommon.h"
#include "common.h"
#include <math.h>
#include <stdio.h>

  /* the following header shouldn't be used in normal programs */
#include FT_INTERNAL_DEBUG_H

  /* showing driver name */
#include FT_MODULE_H
#include FT_INTERNAL_OBJECTS_H
#include FT_INTERNAL_DRIVER_H

#include FT_STROKER_H
#include FT_SYNTHESIS_H
#include FT_LCD_FILTER_H
#include FT_CFF_DRIVER_H
#include FT_TRUETYPE_DRIVER_H


#define MAXPTSIZE  500                 /* dtp */

#ifdef CEIL
#undef CEIL
#endif
#define CEIL( x )  ( ( (x) + 63 ) >> 6 )

#define START_X  19 * 8
#define START_Y  4 * HEADER_HEIGHT

#define INIT_SIZE( size, start_x, start_y, step_y, x, y )     \
          do {                                                \
            start_x = START_X;                                \
            start_y = CEIL( size->metrics.height ) + START_Y; \
            step_y  = CEIL( size->metrics.height ) + 4;       \
                                                              \
            x = start_x;                                      \
            y = start_y;                                      \
          } while ( 0 )

#define X_TOO_LONG( x, size, display )                   \
          ( (x) + ( (size)->metrics.max_advance >> 6 ) > \
            (display)->bitmap->width )
#define Y_TOO_LONG( y, size, display )       \
          ( (y) >= (display)->bitmap->rows )

#ifdef _WIN32
#define snprintf  _snprintf
#endif


#define N_CFF_HINTING_ENGINES  2


  enum
  {
    RENDER_MODE_ALL = 0,
    RENDER_MODE_EMBOLDEN,
    RENDER_MODE_SLANTED,
    RENDER_MODE_STROKE,
    RENDER_MODE_TEXT,
    RENDER_MODE_WATERFALL,
    N_RENDER_MODES
  };

  static struct  status_
  {
    int            update;

    int            width;
    int            height;
    int            render_mode;
    FT_Encoding    encoding;

    int            res;
    int            ptsize;            /* current point size, 26.6 format */
    int            lcd_mode;
    double         gamma;
    double         xbold_factor;
    double         ybold_factor;
    double         radius;
    double         slant;

    int            cff_hinting_engine;
    int            tt_interpreter_version;

    int            font_idx;
    int            offset;            /* as selected by the user */
    int            topleft;           /* as displayed by ftview  */
    int            num_fails;
    int            preload;

    int            use_custom_lcd_filter;
    unsigned char  filter_weights[5];
    int            fw_idx;

  } status = { 1,
               DIM_X, DIM_Y, RENDER_MODE_ALL, FT_ENCODING_NONE,
               72, 48, -1, 1.0, 0.04, 0.04, 0.02, 0.22,
               0, 0, /* default values are set at runtime */
               0, 0, 0, 0, 0,
               0, { 0x10, 0x40, 0x70, 0x40, 0x10 }, 2 };


  static FTDemo_Display*  display;
  static FTDemo_Handle*   handle;


  /*
     In UTF-8 encoding:

       The quick brown fox jumps over the lazy dog
       0123456789
       âêîûôäëïöüÿàùéèç
       &#~"'(-`_^@)=+°
       ABCDEFGHIJKLMNOPQRSTUVWXYZ
       $£^¨*µù%!§:/;.,?<>

     The trailing space is for `looping' in case `Text' gets displayed more
     than once.
   */
  static const char*  Text =
    "The quick brown fox jumps over the lazy dog"
    " 0123456789"
    " \303\242\303\252\303\256\303\273\303\264"
     "\303\244\303\253\303\257\303\266\303\274\303\277"
     "\303\240\303\271\303\251\303\250\303\247"
    " &#~\"\'(-`_^@)=+\302\260"
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    " $\302\243^\302\250*\302\265\303\271%!\302\247:/;.,?<> ";


  static void
  Fatal( const char*  message )
  {
    FTDemo_Display_Done( display );
    FTDemo_Done( handle );
    PanicZ( message );
  }


  static FT_Error
  Render_Stroke( int  num_indices,
                 int  offset )
  {
    int           start_x, start_y, step_y, x, y;
    int           i, have_topleft;
    FT_Size       size;
    FT_Face       face;
    FT_GlyphSlot  slot;

    FT_Fixed  radius;


    error = FTDemo_Get_Size( handle, &size );

    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );
    face = size->face;
    slot = face->glyph;

    radius = status.radius * ( status.ptsize * status.res / 72 );

    FT_Stroker_Set( handle->stroker, radius,
                    FT_STROKER_LINECAP_ROUND,
                    FT_STROKER_LINEJOIN_ROUND,
                    0 );

    have_topleft = 0;

    for ( i = offset; i < num_indices; i++ )
    {
      int  glyph_idx;


      if ( handle->encoding == FT_ENCODING_NONE )
        glyph_idx = i;
      else
        glyph_idx = FTDemo_Get_Index( handle, i );

      error = FT_Load_Glyph( face, glyph_idx,
                             handle->load_flags | FT_LOAD_NO_BITMAP );

      if ( !error && slot->format == FT_GLYPH_FORMAT_OUTLINE )
      {
        FT_Glyph  glyph;


        error = FT_Get_Glyph( slot, &glyph );
        if ( error )
          goto Next;

        error = FT_Glyph_Stroke( &glyph, handle->stroker, 1 );
        if ( error )
        {
          FT_Done_Glyph( glyph );
          goto Next;
        }

        error = FTDemo_Draw_Glyph( handle, display, glyph, &x, &y );

        if ( !error )
        {
          FT_Done_Glyph( glyph );

          if ( !have_topleft )
          {
            have_topleft   = 1;
            status.topleft = i;
          }
        }

        if ( error )
          goto Next;
        else if ( X_TOO_LONG( x, size, display ) )
        {
          x  = start_x;
          y += step_y;

          if ( Y_TOO_LONG( y, size, display ) )
            break;
        }
      }
      else
    Next:
        status.num_fails++;
    }

    return error;
  }


  static FT_Error
  Render_Slanted( int  num_indices,
                  int  offset )
  {
    int           start_x, start_y, step_y, x, y;
    int           i, have_topleft;
    FT_Size       size;
    FT_Face       face;
    FT_GlyphSlot  slot;

    FT_Matrix  shear;


    error = FTDemo_Get_Size( handle, &size );

    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );
    face = size->face;
    slot = face->glyph;

    /***************************************************************/
    /*                                                             */
    /*  2*2 affine transformation matrix, 16.16 fixed float format */
    /*                                                             */
    /*  Shear matrix:                                              */
    /*                                                             */
    /*         | x' |     | 1  k |   | x |          x' = x + ky    */
    /*         |    |  =  |      | * |   |   <==>                  */
    /*         | y' |     | 0  1 |   | y |          y' = y         */
    /*                                                             */
    /*        outline'     shear    outline                        */
    /*                                                             */
    /***************************************************************/

    shear.xx = 1 << 16;
    shear.xy = (FT_Fixed)( status.slant * ( 1 << 16 ) );
    shear.yx = 0;
    shear.yy = 1 << 16;

    have_topleft = 0;

    for ( i = offset; i < num_indices; i++ )
    {
      int  glyph_idx;


      if ( handle->encoding == FT_ENCODING_NONE )
        glyph_idx = i;
      else
        glyph_idx = FTDemo_Get_Index( handle, i );

      error = FT_Load_Glyph( face, glyph_idx, handle->load_flags );
      if ( !error )
      {
        FT_Outline_Transform( &slot->outline, &shear );

        error = FTDemo_Draw_Slot( handle, display, slot, &x, &y );

        if ( !error )
        {
          if ( !have_topleft )
          {
            have_topleft   = 1;
            status.topleft = i;
          }
        }

        if ( error )
          goto Next;
        else if ( X_TOO_LONG( x, size, display ) )
        {
          x  = start_x;
          y += step_y;

          if ( Y_TOO_LONG( y, size, display ) )
            break;
        }
      }
      else
    Next:
        status.num_fails++;
    }

    return error;
  }


  static FT_Error
  Render_Embolden( int  num_indices,
                   int  offset )
  {
    int           start_x, start_y, step_y, x, y;
    int           i, have_topleft;
    FT_Size       size;
    FT_Face       face;
    FT_GlyphSlot  slot;

    FT_Pos  xstr, ystr;


    error = FTDemo_Get_Size( handle, &size );

    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );
    face = size->face;
    slot = face->glyph;

    ystr = status.ptsize * status.res / 72;
    xstr = status.xbold_factor * ystr;
    ystr = status.ybold_factor * ystr;

    have_topleft = 0;

    for ( i = offset; i < num_indices; i++ )
    {
      int  glyph_idx;


      if ( handle->encoding == FT_ENCODING_NONE )
        glyph_idx = i;
      else
        glyph_idx = FTDemo_Get_Index( handle, i );

      error = FT_Load_Glyph( face, glyph_idx, handle->load_flags );
      if ( !error )
      {
        /* this is essentially the code of function */
        /* `FT_GlyphSlot_Embolden'                  */

        if ( slot->format == FT_GLYPH_FORMAT_OUTLINE )
        {
          error = FT_Outline_EmboldenXY( &slot->outline, xstr, ystr );
          /* ignore error */
        }
        else if ( slot->format == FT_GLYPH_FORMAT_BITMAP )
        {
          /* round to full pixels */
          xstr &= ~63;
          ystr &= ~63;

          error = FT_GlyphSlot_Own_Bitmap( slot );
          if ( error )
            goto Next;

          error = FT_Bitmap_Embolden( slot->library, &slot->bitmap,
                                      xstr, ystr );
          if ( error )
            goto Next;
        } else
          goto Next;

        if ( slot->advance.x )
          slot->advance.x += xstr;

        if ( slot->advance.y )
          slot->advance.y += ystr;

        slot->metrics.width        += xstr;
        slot->metrics.height       += ystr;
        slot->metrics.horiAdvance  += xstr;
        slot->metrics.vertAdvance  += ystr;

        if ( slot->format == FT_GLYPH_FORMAT_BITMAP )
          slot->bitmap_top += ystr >> 6;

        error = FTDemo_Draw_Slot( handle, display, slot, &x, &y );

        if ( !error )
        {
          if ( !have_topleft )
          {
            have_topleft   = 1;
            status.topleft = i;
          }
        }

        if ( error )
          goto Next;
        else if ( X_TOO_LONG( x, size, display ) )
        {
          x  = start_x;
          y += step_y;

          if ( Y_TOO_LONG( y, size, display ) )
            break;
        }
      }
      else
    Next:
        status.num_fails++;
    }

    return error;
  }


  static FT_Error
  Render_All( int  num_indices,
              int  offset )
  {
    int      start_x, start_y, step_y, x, y;
    int      i, have_topleft;
    FT_Size  size;


    error = FTDemo_Get_Size( handle, &size );

    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );

    have_topleft = 0;

    for ( i = offset; i < num_indices; i++ )
    {
      int  glyph_idx;


      if ( handle->encoding == FT_ENCODING_NONE )
        glyph_idx = i;
      else
        glyph_idx = FTDemo_Get_Index( handle, i );

      error = FTDemo_Draw_Index( handle, display, glyph_idx, &x, &y );

      if ( !error )
      {
        if ( !have_topleft )
        {
          have_topleft   = 1;
          status.topleft = i;
        }
      }

      if ( error )
        status.num_fails++;
      else if ( X_TOO_LONG( x, size, display ) )
      {
        x = start_x;
        y += step_y;

        if ( Y_TOO_LONG( y, size, display ) )
          break;
      }
    }

    return FT_Err_Ok;
  }


  static FT_Error
  Render_Text( int  num_indices,
               int  offset )
  {
    int      start_x, start_y, step_y, x, y;
    FT_Size  size;
    int      have_topleft;

    const char*  p;
    const char*  pEnd;
    int          ch;


    error = FTDemo_Get_Size( handle, &size );
    if ( error )
    {
      /* probably a non-existent bitmap font size */
      return error;
    }

    INIT_SIZE( size, start_x, start_y, step_y, x, y );

    p    = Text;
    pEnd = p + strlen( Text );

    while ( offset-- )
    {
      ch = utf8_next( &p, pEnd );
      if ( ch < 0 )
      {
        p  = Text;
        ch = utf8_next( &p, pEnd );
      }
    }

    have_topleft = 0;

    while ( num_indices-- )
    {
      FT_UInt  glyph_idx;


      ch = utf8_next( &p, pEnd );
      if ( ch < 0 )
      {
        p  = Text;
        ch = utf8_next( &p, pEnd );
      }

      glyph_idx = FTDemo_Get_Index( handle, ch );

      error = FTDemo_Draw_Index( handle, display, glyph_idx, &x, &y );

      if ( !error )
      {
        if ( !have_topleft )
        {
          have_topleft   = 1;
          status.topleft = ch;
        }
      }

      if ( error )
        status.num_fails++;
      else
      {
        /* Draw_Index adds one pixel space */
        x--;

        if ( X_TOO_LONG( x, size, display ) )
        {
          x  = start_x;
          y += step_y;

          if ( Y_TOO_LONG( y, size, display ) )
            break;
        }
      }
    }

    return FT_Err_Ok;
  }


  static FT_Error
  Render_Waterfall( int  first_size,
                    int  offset )
  {
    int      start_x, start_y, step_y, x, y;
    int      pt_size, max_size = 100000;
    FT_Size  size;
    FT_Face  face;
    int      have_topleft, start;

    char         text[256];
    const char*  p;
    const char*  pEnd;


    error = FTC_Manager_LookupFace( handle->cache_manager,
                                    handle->scaler.face_id, &face );
    if ( error )
    {
      /* can't access the font file: do not render anything */
      fprintf( stderr, "can't access font file %p\n",
               (void*)handle->scaler.face_id );
      return 0;
    }

    if ( !FT_IS_SCALABLE( face ) )
    {
      int  i;


      max_size = 0;
      for ( i = 0; i < face->num_fixed_sizes; i++ )
        if ( face->available_sizes[i].height >= max_size / 64 )
          max_size = face->available_sizes[i].height * 64;
    }

    start_x = START_X;
    start_y = START_Y;

    have_topleft = 0;

    for ( pt_size = first_size; pt_size < max_size; pt_size += 64 )
    {
      int first = offset;
      int ch;


      FTDemo_Set_Current_Charsize( handle, pt_size, status.res );

      error = FTDemo_Get_Size( handle, &size );
      if ( error )
      {
        /* probably a non-existent bitmap font size */
        continue;
      }

      step_y = ( size->metrics.height >> 6 ) + 1;

      x = start_x;
      y = start_y + ( size->metrics.ascender >> 6 );

      start_y += step_y;

      if ( y >= display->bitmap->rows )
        break;

      p    = Text;
      pEnd = p + strlen( Text );

      while ( first-- )
      {
        ch = utf8_next( &p, pEnd );
        if ( ch < 0 )
        {
          p  = Text;
          ch = utf8_next( &p, pEnd );
        }
      }

      start = snprintf( text, 256, "%g: ", pt_size / 64.0 );
      snprintf( text + start, 256 - start, "%s", p );

      p    = text;
      pEnd = p + strlen( text );

      while ( 1 )
      {
        FT_UInt      glyph_idx;
        const char*  oldp;


        oldp = p;
        ch   = utf8_next( &p, pEnd );
        if ( ch < 0 )
        {
          p    = Text;
          oldp = p;
          ch   = utf8_next( &p, pEnd );
        }

        glyph_idx = FTDemo_Get_Index( handle, ch );

        error = FTDemo_Draw_Index( handle, display, glyph_idx, &x, &y );

        if ( !error )
        {
          /* `topleft' should be the first character after the size string */
          if ( oldp - text == start && !have_topleft )
          {
            have_topleft   = 1;
            status.topleft = ch;
          }
        }

        if ( error )
          status.num_fails++;
        else
        {
          /* Draw_Index adds one pixel space */
          x--;

          if ( X_TOO_LONG( x, size, display ) )
            break;
        }
      }
    }

    FTDemo_Set_Current_Charsize( handle, first_size, status.res );

    return FT_Err_Ok;
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
             "FreeType Glyph Viewer - part of the FreeType %s test suite",
             version );

    grWriteln( buf );
    grLn();
    grWriteln( "Use the following keys:" );
    grLn();
    /*          |----------------------------------|    |----------------------------------| */
    grWriteln( "F1, ?       display this help screen                                        " );
    grWriteln( "                                                                            " );
    grWriteln( "render modes:                           anti-aliasing modes:                " );
    grWriteln( "  1         all glyphs                    A         normal                  " );
    grWriteln( "  2         all glyphs emboldened         B         light                   " );
    grWriteln( "  3         all glyphs slanted            C         horizontal RGB (LCD)    " );
    grWriteln( "  4         all glyphs stroked            D         horizontal BGR (LCD)    " );
    grWriteln( "  5         text string                   E         vertical RGB (LCD)      " );
    grWriteln( "  6         waterfall                     F         vertical BGR (LCD)      " );
    grWriteln( "  space     cycle forwards                k         cycle forwards          " );
    grWriteln( "  backspace cycle backwards               l         cycle backwards         " );
    grWriteln( "                                                                            " );
    grWriteln( "b           toggle embedded bitmaps     x, X        adjust horizontal       " );
    grWriteln( "c           toggle color glyphs                      emboldening (in mode 2)" );
    grWriteln( "K           toggle cache modes          y, Y        adjust vertical         " );
    grWriteln( "                                                     emboldening (in mode 2)" );
    grWriteln( "p, n        previous/next font          s, S        adjust slanting         " );
    grWriteln( "                                                     (in mode 3)            " );
    grWriteln( "Up, Down    adjust size by 1 unit       r, R        adjust stroking radius  " );
    grWriteln( "PgUp, PgDn  adjust size by 10 units                  (in mode 4)            " );
    grWriteln( "                                                                            " );
    grWriteln( "Left, Right adjust index by 1           L           toggle custom           " );
    grWriteln( "F7, F8      adjust index by 10                       LCD filtering          " );
    grWriteln( "F9, F10     adjust index by 100         [, ]        select custom LCD       " );
    grWriteln( "F11, F12    adjust index by 1000                      filter weight         " );
    grWriteln( "                                                      (if custom filtering) " );
    grWriteln( "h           toggle hinting              -, +(=)     adjust selected custom  " );
    grWriteln( "H           cycle through hinting                    LCD filter weight      " );
    grWriteln( "             engines (if available)                                         " );
    grWriteln( "f           toggle forced auto-         G           show gamma ramp         " );
    grWriteln( "             hinting (if hinting)       g, v        adjust gamma value      " );
    grWriteln( "                                                                            " );
    grWriteln( "a           toggle anti-aliasing        q, ESC      quit ftview             " );
    /*          |----------------------------------|    |----------------------------------| */
    grLn();
    grLn();
    grWriteln( "press any key to exit this help screen" );

    grRefreshSurface( display->surface );
    grListenSurface( display->surface, gr_event_key, &dummy_event );
  }


  static void
  event_gamma_grid( void )
  {
    grEvent  dummy_event;
    int      g;
    int      yside  = 11;
    int      xside  = 10;
    int      levels = 17;
    int      gammas = 30;
    int      x_0    = ( display->bitmap->width - levels * xside ) / 2;
    int      y_0    = ( display->bitmap->rows - gammas * ( yside + 1 ) ) / 2;
    int      pitch  = display->bitmap->pitch;


    FTDemo_Display_Clear( display );
    grGotobitmap( display->bitmap );

    if ( pitch < 0 )
      pitch = -pitch;

    memset( display->bitmap->buffer, 100, pitch * display->bitmap->rows );

    grWriteCellString( display->bitmap, 0, 0, "Gamma grid",
                       display->fore_color );

    for ( g = 1; g <= gammas; g++ )
    {
      double  ggamma = 0.1 * g;
      char    temp[6];
      int     y = y_0 + ( yside + 1 ) * ( g - 1 );
      int     nx, ny;

      unsigned char*  line = display->bitmap->buffer +
                             y * display->bitmap->pitch;


      if ( display->bitmap->pitch < 0 )
        line -= display->bitmap->pitch * ( display->bitmap->rows - 1 );

      line += x_0 * 3;

      grSetPixelMargin( x_0 - 32, y + ( yside - 8 ) / 2 );
      grGotoxy( 0, 0 );

      sprintf( temp, "%.1f", ggamma );
      grWrite( temp );

      for ( ny = 0; ny < yside; ny++, line += display->bitmap->pitch )
      {
        unsigned char*  dst = line;


        for ( nx = 0; nx < levels; nx++, dst += 3 * xside )
        {
          double  p   = nx / (double)( levels - 1 );
          int     gm  = (int)( 255.0 * pow( p, ggamma ) );


          memset( dst, gm, xside * 3 );
        }
      }
    }

    grRefreshSurface( display->surface );
    grListenSurface( display->surface, gr_event_key, &dummy_event );
  }


  static int
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
      return 1;
    }

    return 0;
  }


  static int
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
      return 1;
    }

    return 0;
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
  }


  static int
  event_bold_change( double  xdelta,
                     double  ydelta )
  {
    double  old_xbold_factor = status.xbold_factor;
    double  old_ybold_factor = status.ybold_factor;


    status.xbold_factor += xdelta;
    status.ybold_factor += ydelta;

    if ( status.xbold_factor > 0.1 )
      status.xbold_factor = 0.1;
    else if ( status.xbold_factor < -0.1 )
      status.xbold_factor = -0.1;

    if ( status.ybold_factor > 0.1 )
      status.ybold_factor = 0.1;
    else if ( status.ybold_factor < -0.1 )
      status.ybold_factor = -0.1;

    return ( old_xbold_factor == status.xbold_factor &&
             old_ybold_factor == status.ybold_factor ) ? 0 : 1;
  }


  static int
  event_radius_change( double  delta )
  {
    double  old_radius = status.radius;


    status.radius += delta;

    if ( status.radius > 0.05 )
      status.radius = 0.05;
    else if ( status.radius < 0.0 )
      status.radius = 0.0;

    return old_radius == status.radius ? 0 : 1;
  }


  static int
  event_slant_change( double  delta )
  {
    double  old_slant = status.slant;


    status.slant += delta;

    if ( status.slant > 1.0 )
      status.slant = 1.0;
    else if ( status.slant < -1.0 )
      status.slant = -1.0;

    return old_slant == status.slant ? 0 : 1;
  }


  static int
  event_size_change( int  delta )
  {
    int  old_ptsize = status.ptsize;


    status.ptsize += delta;

    if ( status.ptsize < 64 * 1 )
      status.ptsize = 1 * 64;
    else if ( status.ptsize > MAXPTSIZE * 64 )
      status.ptsize = MAXPTSIZE * 64;

    FTDemo_Set_Current_Charsize( handle, status.ptsize, status.res );

    return old_ptsize == status.ptsize ? 0 : 1;
  }


  static int
  event_index_change( int  delta )
  {
    int  old_offset  = status.offset;
    int  num_indices = handle->current_font->num_indices;


    status.offset += delta;

    if ( status.offset < 0 )
      status.offset = 0;
    else if ( status.offset >= num_indices )
      status.offset = num_indices - 1;

    return old_offset == status.offset ? 0 : 1;
  }


  static void
  event_render_mode_change( int  delta )
  {
    if ( delta )
      status.render_mode = ( status.render_mode +
                             delta              +
                             N_RENDER_MODES     ) % N_RENDER_MODES;
  }


  static int
  event_font_change( int  delta )
  {
    int  num_indices;


    if ( status.font_idx + delta >= handle->num_fonts ||
         status.font_idx + delta < 0                  )
      return 0;

    status.font_idx += delta;

    FTDemo_Set_Current_Font( handle, handle->fonts[status.font_idx] );
    FTDemo_Set_Current_Charsize( handle, status.ptsize, status.res );
    FTDemo_Update_Current_Flags( handle );

    num_indices = handle->current_font->num_indices;

    if ( status.offset >= num_indices )
      status.offset = num_indices - 1;

    return 1;
  }


  static int
  Process_Event( grEvent*  event )
  {
    int  ret = 0;


    status.update = 0;

    if ( status.render_mode == (int)( event->key - '1' ) )
      return ret;
    if ( event->key >= '1' && event->key < '1' + N_RENDER_MODES )
    {
      status.render_mode = event->key - '1';
      event_render_mode_change( 0 );
      status.update = 1;
      return ret;
    }

    if ( handle->antialias )
    {
      if ( handle->lcd_mode == (int)( event->key - 'A' ) )
        return ret;
      if ( event->key >= 'A' && event->key < 'A' + N_LCD_MODES )
      {
        handle->lcd_mode = event->key - 'A';
        FTDemo_Update_Current_Flags( handle );
        status.update = 1;
        return ret;
      }
    }

    switch ( event->key )
    {
    case grKeyEsc:
    case grKEY( 'q' ):
      ret = 1;
      break;

    case grKeyF1:
    case grKEY( '?' ):
      event_help();
      status.update = 1;
      break;

    case grKEY( 'a' ):
      handle->antialias = !handle->antialias;
      FTDemo_Update_Current_Flags( handle );
      status.update = 1;
      break;

    case grKEY( 'b' ):
      handle->use_sbits = !handle->use_sbits;
      FTDemo_Update_Current_Flags( handle );
      status.update = 1;
      break;

    case grKEY( 'c' ):
      handle->color = !handle->color;
      FTDemo_Update_Current_Flags( handle );
      status.update = 1;
      break;

    case grKEY( 'K' ):
      handle->use_sbits_cache = !handle->use_sbits_cache;
      status.update = 1;
      break;

    case grKEY( 'f' ):
      if ( handle->hinted && handle->lcd_mode != LCD_MODE_LIGHT )
      {
        handle->autohint = !handle->autohint;
        FTDemo_Update_Current_Flags( handle );
        status.update = 1;
      }
      break;

    case grKEY( 'h' ):
      handle->hinted = !handle->hinted;
      FTDemo_Update_Current_Flags( handle );
      status.update = 1;
      break;

    case grKEY( 'H' ):
      if ( !handle->autohint                  &&
           handle->lcd_mode != LCD_MODE_LIGHT )
      {
        FT_Face    face;
        FT_Module  module;


        error = FTC_Manager_LookupFace( handle->cache_manager,
                                        handle->scaler.face_id, &face );
        if ( !error )
        {
          module = &face->driver->root;

          if ( !strcmp( module->clazz->module_name, "cff" ) )
            status.update = event_cff_hinting_engine_change( 1 );
          else if ( !strcmp( module->clazz->module_name, "truetype" ) )
            status.update = event_tt_interpreter_version_change();
        }
      }
      break;

    case grKEY( 'l' ):
    case grKEY( 'k' ):
      if ( handle->antialias )
      {
        handle->lcd_mode = ( event->key == grKEY( 'l' ) )
                           ? ( ( handle->lcd_mode == ( N_LCD_MODES - 1 ) )
                               ? 0
                               : handle->lcd_mode + 1 )
                           : ( ( handle->lcd_mode == 0 )
                               ? ( N_LCD_MODES - 1 )
                               : handle->lcd_mode - 1 );
        FTDemo_Update_Current_Flags( handle );
        status.update = 1;
      }
      break;

    case grKeySpace:
      event_render_mode_change( 1 );
      status.update = 1;
      break;

    case grKeyBackSpace:
      event_render_mode_change( -1 );
      status.update = 1;
      break;

    case grKEY( 'G' ):
      event_gamma_grid();
      status.update = 1;
      break;

    case grKEY( 's' ):
      if ( status.render_mode == RENDER_MODE_SLANTED )
        status.update = event_slant_change( 0.02 );
      break;

    case grKEY( 'S' ):
      if ( status.render_mode == RENDER_MODE_SLANTED )
        status.update = event_slant_change( -0.02 );
      break;

    case grKEY( 'r' ):
      if ( status.render_mode == RENDER_MODE_STROKE )
        status.update = event_radius_change( 0.005 );
      break;

    case grKEY( 'R' ):
      if ( status.render_mode == RENDER_MODE_STROKE )
        status.update = event_radius_change( -0.005 );
      break;

    case grKEY( 'x' ):
      if ( status.render_mode == RENDER_MODE_EMBOLDEN )
        status.update = event_bold_change( 0.005, 0.0 );
      break;

    case grKEY( 'X' ):
      if ( status.render_mode == RENDER_MODE_EMBOLDEN )
        status.update = event_bold_change( -0.005, 0.0 );
      break;

    case grKEY( 'y' ):
      if ( status.render_mode == RENDER_MODE_EMBOLDEN )
        status.update = event_bold_change( 0.0, 0.005 );
      break;

    case grKEY( 'Y' ):
      if ( status.render_mode == RENDER_MODE_EMBOLDEN )
        status.update = event_bold_change( 0.0, -0.005 );
      break;

    case grKEY( 'g' ):
      event_gamma_change( 0.1 );
      status.update = 1;
      break;

    case grKEY( 'v' ):
      event_gamma_change( -0.1 );
      status.update = 1;
      break;

    case grKEY( 'n' ):
      status.update = event_font_change( 1 );
      break;

    case grKEY( 'p' ):
      status.update = event_font_change( -1 );
      break;

    case grKeyUp:
      status.update = event_size_change( 64 );
      break;
    case grKeyDown:
      status.update = event_size_change( -64 );
      break;
    case grKeyPageUp:
      status.update = event_size_change( 640 );
      break;
    case grKeyPageDown:
      status.update = event_size_change( -640 );
      break;

    case grKeyLeft:
      status.update = event_index_change( -1 );
      break;
    case grKeyRight:
      status.update = event_index_change( 1 );
      break;
    case grKeyF7:
      status.update = event_index_change( -10 );
      break;
    case grKeyF8:
      status.update = event_index_change( 10 );
      break;
    case grKeyF9:
      status.update = event_index_change( -100 );
      break;
    case grKeyF10:
      status.update = event_index_change( 100 );
      break;
    case grKeyF11:
      status.update = event_index_change( -1000 );
      break;
    case grKeyF12:
      status.update = event_index_change( 1000 );
      break;

    case grKEY( 'L' ):
      FTC_Manager_RemoveFaceID( handle->cache_manager,
                                handle->scaler.face_id );

      status.use_custom_lcd_filter = !status.use_custom_lcd_filter;
      if ( status.use_custom_lcd_filter )
        FT_Library_SetLcdFilterWeights( handle->library,
                                        status.filter_weights );
      else
        FT_Library_SetLcdFilterWeights( handle->library,
                                        (unsigned char*)"\x10\x40\x70\x40\x10" );
      status.update = 1;
      break;

    case grKEY( '[' ):
      if ( status.use_custom_lcd_filter )
      {
        status.fw_idx--;
        if ( status.fw_idx < 0 )
          status.fw_idx = 4;
        status.update = 1;
      }
      break;

    case grKEY( ']' ):
      if ( status.use_custom_lcd_filter )
      {
        status.fw_idx++;
        if ( status.fw_idx > 4 )
          status.fw_idx = 0;
        status.update = 1;
      }
      break;

    case grKEY( '-' ):
      if ( status.use_custom_lcd_filter )
      {
        FTC_Manager_RemoveFaceID( handle->cache_manager,
                                handle->scaler.face_id );

        status.filter_weights[status.fw_idx]--;
        FT_Library_SetLcdFilterWeights( handle->library,
                                        status.filter_weights );
        status.update = 1;
      }
      break;

    case grKEY( '+' ):
    case grKEY( '=' ):
      if ( status.use_custom_lcd_filter )
      {
        FTC_Manager_RemoveFaceID( handle->cache_manager,
                                  handle->scaler.face_id );

        status.filter_weights[status.fw_idx]++;
        FT_Library_SetLcdFilterWeights( handle->library,
                                        status.filter_weights );
        status.update = 1;
      }
      break;

    default:
      break;
    }

    return ret;
  }


  static void
  write_header( FT_Error  error_code )
  {
    FT_Face      face;
    char         buf[256];
    const char*  basename;
    const char*  format;

    int          line = 0;


    error = FTC_Manager_LookupFace( handle->cache_manager,
                                    handle->scaler.face_id, &face );
    if ( error )
      Fatal( "can't access font file" );

    /* errors */
    switch ( error_code )
    {
    case FT_Err_Ok:
      sprintf( buf, " " );
      break;
    case FT_Err_Invalid_Pixel_Size:
      sprintf( buf, "Invalid pixel size" );
      break;
    case FT_Err_Invalid_PPem:
      sprintf( buf, "Invalid ppem value" );
      break;
    default:
      sprintf( buf, "error 0x%04x",
                    (FT_UShort)error_code );
      break;
    }
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, error_code ? display->warn_color
                                       : display->fore_color );

    /* font and file name */
    basename = ft_basename( handle->current_font->filepathname );
    sprintf( buf, "%.50s %.50s (file `%.100s')",
             face->family_name, face->style_name, basename );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    line++;

    /* char code, glyph index, glyph name */
    if ( status.encoding == FT_ENCODING_UNICODE      ||
         status.render_mode == RENDER_MODE_TEXT      ||
         status.render_mode == RENDER_MODE_WATERFALL )
      sprintf( buf, "top left charcode: U+%04X (glyph idx %d)",
                    status.topleft,
                    FTDemo_Get_Index( handle, status.topleft ) );
    else if ( status.encoding == FT_ENCODING_NONE )
      sprintf( buf, "top left glyph idx: %d",
                    status.topleft );
    else
      sprintf( buf, "top left charcode: 0x%X (glyph idx %d)",
                    status.topleft,
                    FTDemo_Get_Index( handle, status.topleft ) );

    if ( FT_HAS_GLYPH_NAMES( face ) )
    {
      char*  p;
      int    format_len, glyph_idx, size;


      size = strlen( buf );
      p    = buf + size;
      size = 256 - size;

      format = ", name: ";
      format_len = strlen( format );

      if ( size >= format_len + 2 )
      {
        glyph_idx = status.topleft;
        if ( status.encoding != FT_ENCODING_NONE         ||
             status.render_mode == RENDER_MODE_TEXT      ||
             status.render_mode == RENDER_MODE_WATERFALL )
          glyph_idx = FTDemo_Get_Index( handle, status.topleft );

        strcpy( p, format );
        if ( FT_Get_Glyph_Name( face, glyph_idx,
                                p + format_len, size - format_len ) )
          *p = '\0';
      }
    }
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    line++;

    /* encoding */
    if ( !( status.render_mode == RENDER_MODE_TEXT      ||
            status.render_mode == RENDER_MODE_WATERFALL ) )
    {
      const char*  encoding = NULL;


      switch ( status.encoding )
      {
      case FT_ENCODING_NONE:
        encoding = "glyph order";
        break;
      case FT_ENCODING_MS_SYMBOL:
        encoding = "MS Symbol";
        break;
      case FT_ENCODING_UNICODE:
        encoding = "Unicode";
        break;
      case FT_ENCODING_SJIS:
        encoding = "SJIS";
        break;
      case FT_ENCODING_GB2312:
        encoding = "GB 2312";
        break;
      case FT_ENCODING_BIG5:
        encoding = "Big 5";
        break;
      case FT_ENCODING_WANSUNG:
        encoding = "Wansung";
        break;
      case FT_ENCODING_JOHAB:
        encoding = "Johab";
        break;
      case FT_ENCODING_ADOBE_STANDARD:
        encoding = "Adobe Standard";
        break;
      case FT_ENCODING_ADOBE_EXPERT:
        encoding = "Adobe Expert";
        break;
      case FT_ENCODING_ADOBE_CUSTOM:
        encoding = "Adobe Custom";
        break;
      case FT_ENCODING_ADOBE_LATIN_1:
        encoding = "Latin 1";
        break;
      case FT_ENCODING_OLD_LATIN_2:
        encoding = "Latin 2";
        break;
      case FT_ENCODING_APPLE_ROMAN:
        encoding = "Apple Roman";
        break;
      }
      grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                         encoding, display->fore_color );
    }

    /* dpi */
    sprintf( buf, "%ddpi",
                  status.res );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    /* pt and ppem */
    sprintf( buf, "%gpt (%dppem)",
                  status.ptsize / 64.0,
                  ( status.ptsize * status.res / 72 + 32 ) >> 6 );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    line++;

    /* render mode */
    {
      const char*  render_mode = NULL;


      switch ( status.render_mode )
      {
      case RENDER_MODE_ALL:
        render_mode = "all glyphs";
        break;
      case RENDER_MODE_EMBOLDEN:
        render_mode = "emboldened";
        break;
      case RENDER_MODE_SLANTED:
        render_mode = "slanted";
        break;
      case RENDER_MODE_STROKE:
        render_mode = "stroked";
        break;
      case RENDER_MODE_TEXT:
        render_mode = "text string";
        break;
      case RENDER_MODE_WATERFALL:
        render_mode = "waterfall";
        break;
      }
      sprintf( buf, "%d: %s",
                    status.render_mode + 1,
                    render_mode );
      grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                         buf, display->fore_color );
    }

    if ( status.render_mode == RENDER_MODE_EMBOLDEN )
    {
      /* x emboldening */
      sprintf( buf, " x: %.3f",
                    status.xbold_factor );
      grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                         buf, display->fore_color );

      /* y emboldening */
      sprintf( buf, " y: %.3f",
                    status.ybold_factor );
      grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                         buf, display->fore_color );
    }

    if ( status.render_mode == RENDER_MODE_STROKE )
    {
      /* stroking radius */
      sprintf( buf, " radius: %.3f",
                    status.radius );
      grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                         buf, display->fore_color );
    }

    if ( status.render_mode == RENDER_MODE_SLANTED )
    {
      /* slanting */
      sprintf( buf, " value: %.3f",
                    status.slant );
      grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                         buf, display->fore_color );
    }

    line++;

    /* anti-aliasing */
    sprintf( buf, "anti-alias: %s",
                  handle->antialias ? "on" : "off" );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    if ( handle->antialias )
    {
      const char*  lcd_mode = NULL;


      switch ( handle->lcd_mode )
      {
      case LCD_MODE_AA:
        lcd_mode = " normal AA";
        break;
      case LCD_MODE_LIGHT:
        lcd_mode = " light AA";
        break;
      case LCD_MODE_RGB:
        lcd_mode = " LCD (horiz. RGB)";
        break;
      case LCD_MODE_BGR:
        lcd_mode = " LCD (horiz. BGR)";
        break;
      case LCD_MODE_VRGB:
        lcd_mode = " LCD (vert. RGB)";
        break;
      case LCD_MODE_VBGR:
        lcd_mode = " LCD (vert. BGR)";
        break;
      }
      grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                         lcd_mode, display->fore_color );
    }

    /* hinting */
    sprintf( buf, "hinting: %s",
                  handle->hinted ? "on" : "off" );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    if ( handle->hinted )
    {
      /* auto-hinting */
      sprintf( buf, " forced auto: %s",
                    ( handle->autohint                   ||
                      handle->lcd_mode == LCD_MODE_LIGHT ) ? "on" : "off" );
      grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                         buf, display->fore_color );
    }

    if ( !handle->autohint                  &&
         handle->lcd_mode != LCD_MODE_LIGHT )
    {
      /* hinting engine */

      FT_Module    module         = &face->driver->root;
      const char*  hinting_engine = NULL;


      if ( !strcmp( module->clazz->module_name, "cff" ) )
      {
        switch ( status.cff_hinting_engine )
        {
        case FT_CFF_HINTING_FREETYPE:
          hinting_engine = "FreeType";
          break;
        case FT_CFF_HINTING_ADOBE:
          hinting_engine = "Adobe";
          break;
        }
      }

      else if ( !strcmp( module->clazz->module_name, "truetype" ) )
      {
        switch ( status.tt_interpreter_version )
        {
        case TT_INTERPRETER_VERSION_35:
          hinting_engine = "v35";
          break;
        case TT_INTERPRETER_VERSION_38:
          hinting_engine = "v38";
          break;
        }
      }

      if ( hinting_engine )
      {
        sprintf( buf, "engine: %s",
                      hinting_engine );
        grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                           buf, display->fore_color );
      }
    }

    line++;

    /* embedded bitmaps */
    sprintf( buf, "bitmaps: %s",
                  handle->use_sbits ? "on" : "off" );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    /* embedded color bitmaps */
    sprintf( buf, "color bitmaps: %s",
                  handle->color ? "on" : "off" );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    /* cache */
    sprintf( buf, "cache: %s",
                  handle->use_sbits_cache ? "on" : "off" );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    /* gamma */
    sprintf( buf, "gamma: %.1f%s",
                  status.gamma, status.gamma == 0.0 ? " (sRGB mode)"
                                                    : "" );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    line++;

    /* custom LCD filtering */
    sprintf( buf, "custom LCD: %s",
                  status.use_custom_lcd_filter ? "on" : "off" );
    grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                       buf, display->fore_color );

    /* LCD filter settings */
    if ( status.use_custom_lcd_filter )
    {
      int             fwi = status.fw_idx;
      unsigned char*  fw  = status.filter_weights;
      int             i;


      for ( i = 0; i < 5; i++ )
      {
        sprintf( buf,
                 " %s0x%02X%s",
                 fwi == i ? "[" : " ",
                 fw[i],
                 fwi == i ? "]" : " " );
        grWriteCellString( display->bitmap, 0, (line++) * HEADER_HEIGHT,
                           buf, display->fore_color );
      }
    }

    grRefreshSurface( display->surface );
  }


  static void
  usage( char*  execname )
  {
    fprintf( stderr,
      "\n"
      "ftview: simple glyph viewer -- part of the FreeType project\n"
      "-----------------------------------------------------------\n"
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
      "            For Type 1 font files, ftview also tries to attach\n"
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
      "  -e enc    Specify encoding tag (default: no encoding).\n"
      "            Common values: `unic' (Unicode), `symb' (symbol),\n"
      "            `ADOB' (Adobe standard), `ADBC' (Adobe custom).\n"
      "  -m text   Use `text' for rendering.\n" );
    fprintf( stderr,
      "  -l mode   Set start-up rendering mode (0 <= mode <= %d).\n",
             N_LCD_MODES );
    fprintf( stderr,
      "  -p        Preload file in memory to simulate memory-mapping.\n"
      "\n"
      "  -v        Show version.\n"
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
      option = getopt( *argc, *argv, "e:f:h:l:m:pr:vw:" );

      if ( option == -1 )
        break;

      switch ( option )
      {
      case 'e':
        status.encoding = FTDemo_Make_Encoding_Tag( optarg );
        break;

      case 'f':
        status.offset  = atoi( optarg );
        break;

      case 'h':
        status.height = atoi( optarg );
        if ( status.height < 1 )
          usage( execname );
        break;

      case 'l':
        status.lcd_mode = atoi( optarg );
        if ( status.lcd_mode < 0 || status.lcd_mode > N_LCD_MODES )
        {
          fprintf( stderr, "argument to `l' must be between 0 and %d\n",
                   N_LCD_MODES );
          exit( 3 );
        }
        break;

      case 'm':
        Text               = optarg;
        status.render_mode = RENDER_MODE_TEXT;
        break;

      case 'p':
        status.preload = 1;
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

          printf( "ftview (FreeType) %d.%d", major, minor );
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

    (*argc)--;
    (*argv)++;
  }


  int
  main( int    argc,
        char*  argv[] )
  {
    grEvent  event;


    /* Initialize engine */
    handle = FTDemo_New();

    parse_cmdline( &argc, &argv );

    FT_Library_SetLcdFilter( handle->library, FT_LCD_FILTER_DEFAULT );

    /* get the default value as compiled into FreeType */
    FT_Property_Get( handle->library,
                     "cff",
                     "hinting-engine", &status.cff_hinting_engine );
    FT_Property_Get( handle->library,
                     "truetype",
                     "interpreter-version", &status.tt_interpreter_version );

    handle->encoding = status.encoding;

    if ( status.preload )
      FTDemo_Set_Preload( handle, 1 );

    for ( ; argc > 0; argc--, argv++ )
      FTDemo_Install_Font( handle, argv[0], 0 );

    if ( handle->num_fonts == 0 )
      Fatal( "could not find/open any font file" );

    display = FTDemo_Display_New( gr_pixel_mode_rgb24,
                                  status.width, status.height );
    if ( !display )
      Fatal( "could not allocate display surface" );

    grSetTitle( display->surface,
                "FreeType Glyph Viewer - press F1 for help" );

    status.num_fails = 0;

    event_font_change( 0 );

    if ( status.lcd_mode >= 0 )
      handle->lcd_mode = status.lcd_mode;

    FTDemo_Update_Current_Flags( handle );

    do
    {
      if ( !status.update )
        goto Listen;

      FTDemo_Display_Clear( display );

      switch ( status.render_mode )
      {
      case RENDER_MODE_ALL:
        error = Render_All( handle->current_font->num_indices,
                            status.offset );
        break;

      case RENDER_MODE_EMBOLDEN:
        error = Render_Embolden( handle->current_font->num_indices,
                                 status.offset );
        break;

      case RENDER_MODE_SLANTED:
        error = Render_Slanted( handle->current_font->num_indices,
                                status.offset );
        break;

      case RENDER_MODE_STROKE:
        error = Render_Stroke( handle->current_font->num_indices,
                               status.offset );
        break;

      case RENDER_MODE_TEXT:
        error = Render_Text( -1, status.offset );
        break;

      case RENDER_MODE_WATERFALL:
        error = Render_Waterfall( status.ptsize, status.offset );
        break;
      }

      write_header( error );

    Listen:
      grListenSurface( display->surface, 0, &event );
    } while ( Process_Event( &event ) == 0 );

    printf( "Execution completed successfully.\n" );
    printf( "Fails = %d\n", status.num_fails );

    FTDemo_Display_Done( display );
    FTDemo_Done( handle );
    exit( 0 );      /* for safety reasons */

    return 0;       /* never reached */
  }


/* End */
