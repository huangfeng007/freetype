
  int                   h        = blit->height;
  const unsigned char*  src_line = blit->src_line;
  unsigned char*        dst_line = blit->dst_line;

  gblender_use_channels( blender, 0 );

  do
  {
    const unsigned char*  src = src_line + blit->src_x * 4;
    unsigned char*        dst = dst_line + blit->dst_x * GDST_INCR;
    int                   w   = blit->width;

    do
    {
      int  a  = GBLENDER_SHADE_INDEX(src[3]);
      int  ra = src[3];

      int  b = src[0];
      int  g = src[1];
      int  r = src[2];


      if ( a == 0 )
      {
        /* nothing */
      }
      else if ( a == 255 )
      {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
      }
      else
      {
        int  ba = 255 - ra;
        int  br = dst[0];
        int  bb = dst[1];
        int  bg = dst[2];


	dst[0] = br * ba / 255 + r;
	dst[1] = bg * ba / 255 + g;
	dst[2] = bb * ba / 255 + b;
      }

      src += 4;
      dst += GDST_INCR;

    } while ( --w > 0 );

    src_line += blit->src_pitch;
    dst_line += blit->dst_pitch;

  } while ( --h > 0 );

