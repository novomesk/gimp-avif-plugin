/*
 * GIMP plug-in to allow import/export in AVIF image format.
 * Author: Daniel Novomesky
 */

/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*
This software uses libavif
URL: https://github.com/AOMediaCodec/libavif/

Copyright 2019 Joe Drago. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include <avif/avif.h>
#include <gexiv2/gexiv2.h>
#include <glib/gstdio.h>
#include <sys/time.h>

#include "file-avif-save.h"

#define MAX_TILE_WIDTH  4096
#define MAX_TILE_AREA  (4096 * 2304)
#define MAX_TILE_ROWS 64
#define MAX_TILE_COLS 64

typedef struct
{
  gchar *tag;
  gint  type;
} XmpStructs;

static inline unsigned int Max (unsigned int a, unsigned int b)
{
  return ( (a) > (b) ? a : b);
}
static inline unsigned int Min (unsigned int a, unsigned int b)
{
  return ( (a) < (b) ? a : b);
}

static inline unsigned int tile_log2 (unsigned int blkSize, unsigned int target)
{
  unsigned int k;
  for (k = 0; (blkSize << k) < target; k++)
    {
    }
  return k;
}

static void
avifplugin_set_tiles_recursive (unsigned int width, unsigned int height,
                                unsigned int Log2Tiles_needed,
                                unsigned int maxLog2TileCols,
                                unsigned int maxLog2TileRows,
                                avifEncoder *encoder)
{
  if (Log2Tiles_needed == 0) return;

  if (width > height)
    {
      if ( (unsigned int) encoder->tileColsLog2 < maxLog2TileCols)
        {
          encoder->tileColsLog2++;
          avifplugin_set_tiles_recursive (width >> 1, height, Log2Tiles_needed - 1, maxLog2TileCols, maxLog2TileRows, encoder);
        }
      else if ( (unsigned int) encoder->tileRowsLog2 < maxLog2TileRows)
        {
          encoder->tileRowsLog2++;
          avifplugin_set_tiles_recursive (width, height >> 1, Log2Tiles_needed - 1, maxLog2TileCols, maxLog2TileRows, encoder);
        }
    }
  else /* width <= height */
    {
      if ( (unsigned int) encoder->tileRowsLog2 < maxLog2TileRows)
        {
          encoder->tileRowsLog2++;
          avifplugin_set_tiles_recursive (width, height >> 1, Log2Tiles_needed - 1, maxLog2TileCols, maxLog2TileRows, encoder);
        }
      else if ( (unsigned int) encoder->tileColsLog2 < maxLog2TileCols)
        {
          encoder->tileColsLog2++;
          avifplugin_set_tiles_recursive (width >> 1, height, Log2Tiles_needed - 1, maxLog2TileCols, maxLog2TileRows, encoder);
        }
    }
}

/* procedure to set tileColsLog2, tileRowsLog2 values */
static void
avifplugin_set_tiles (unsigned int FrameWidth,
                      unsigned int FrameHeight,
                      avifEncoder *encoder)
{

  unsigned int MiCols = 2 * ( (FrameWidth + 7) >> 3);
  unsigned int MiRows = 2 * ( (FrameHeight + 7) >> 3);

  unsigned int sbCols = ( (MiCols + 31) >> 5);
  unsigned int sbRows = ( (MiRows + 31) >> 5);
  unsigned int sbShift = 5;
  unsigned int sbSize = sbShift + 2;
  unsigned int maxTileWidthSb = MAX_TILE_WIDTH >> sbSize;
  unsigned int maxTileAreaSb = MAX_TILE_AREA >> (2 * sbSize);
  unsigned int minLog2TileCols = tile_log2 (maxTileWidthSb, sbCols);
  unsigned int maxLog2TileCols = tile_log2 (1, Min (sbCols, MAX_TILE_COLS));
  unsigned int maxLog2TileRows = tile_log2 (1, Min (sbRows, MAX_TILE_ROWS));
  unsigned int minLog2Tiles = Max (minLog2TileCols,
                                   tile_log2 (maxTileAreaSb, sbRows * sbCols));

  encoder->tileColsLog2 = minLog2TileCols; /* we set minimal values */
  encoder->tileRowsLog2 = 0;

  if (minLog2Tiles > minLog2TileCols)   /* we need to set more tiles */
    {
      unsigned int Log2Tiles_needed = minLog2Tiles - minLog2TileCols;
      unsigned int tile_width = FrameWidth >> minLog2TileCols;

      avifplugin_set_tiles_recursive (tile_width, FrameHeight, Log2Tiles_needed, maxLog2TileCols, maxLog2TileRows, encoder);
    }
}

static float
ColorPrimariesDistance (const avifColorPrimaries tested, const float inPrimaries[8])
{
  float prim[8];
  float distance = 0;
  unsigned int i;

  avifColorPrimariesGetValues (tested, prim);

  for (i = 0; i < 8; i++)
    {
      float diff = inPrimaries[i] - prim[i];
      distance = distance + (diff * diff);
    }

  return distance;
}

static avifColorPrimaries
ColorPrimariesBestMatch (const float inPrimaries[8])
{
  avifColorPrimaries winner = (avifColorPrimaries) 1;   /* AVIF_COLOR_PRIMARIES_BT709 */
  float winnerdistance = ColorPrimariesDistance (winner, inPrimaries);
  float testdistance;

  /* AVIF_COLOR_PRIMARIES_BT2020 */
  testdistance = ColorPrimariesDistance ( (avifColorPrimaries) 9, inPrimaries);
  if (testdistance < winnerdistance)
    {
      winnerdistance = testdistance;
      winner = (avifColorPrimaries) 9;
    }

  /* AVIF_COLOR_PRIMARIES_SMPTE432 */
  testdistance = ColorPrimariesDistance ( (avifColorPrimaries) 12, inPrimaries);
  if (testdistance < winnerdistance)
    {
      winnerdistance = testdistance;
      winner = (avifColorPrimaries) 12;
    }

  return winner;
}

gboolean   save_layers (GFile         *file,
                        GimpImage     *image,
                        gint           n_drawables,
                        GimpDrawable **drawables,
                        GObject       *config,
                        GimpMetadata  *metadata,
                        GError       **error)
{
  FILE           *outfile;
  GeglBuffer     *buffer;
  GimpImageType   drawable_type;
  const Babl     *file_format = NULL;
  const Babl     *space;
  gint            drawable_width;
  gint            drawable_height;
  gint            save_bit_depth = 8;
  gboolean        out_linear, save_alpha, is_gray;
  guchar         *pixels;

  GimpColorProfile *profile = NULL;
  gint            min_quantizer = AVIF_QUANTIZER_BEST_QUALITY;
  gint            max_quantizer = 40;
  gint            alpha_quantizer = AVIF_QUANTIZER_BEST_QUALITY;

  avifPixelFormat pixel_format = AVIF_PIXEL_FORMAT_YUV420;
  avifCodecChoice codec_choice = AVIF_CODEC_CHOICE_AUTO;
  gboolean        save_icc_profile = TRUE;
  gboolean        save_exif = FALSE;
  gboolean        save_xmp = FALSE;
  gint            num_threads = 1;
  gint            encoder_speed;
  gint            i, j;
  gint            animation_frame_duration = 1;
  gint            animation_timescale = 1;
  gint            frame_index;
  avifImage      *avif;
  avifResult      res;
  avifRWData      raw = AVIF_DATA_EMPTY;
  avifEncoder    *encoder;

  if (n_drawables < 1)
    {
      return FALSE;
    }

  drawable_type   = gimp_drawable_type (drawables[0]);
  drawable_width  = gimp_drawable_get_width (drawables[0]);
  drawable_height = gimp_drawable_get_height (drawables[0]);

  if (n_drawables >= 2)
    {
      for (i = 1; i < n_drawables; i++)
        {
          if (drawable_width != gimp_drawable_get_width (drawables[i]) || drawable_height != gimp_drawable_get_height (drawables[i]))
            {
              g_warning ("Can't save animation. Layers have different width or height!\n");
              return FALSE;
            }
        }

      g_object_get (config,
                    "animation-frame-duration", &animation_frame_duration,
                    "animation-timescale", &animation_timescale,
                    NULL);
    }

  gimp_progress_init_printf ("Exporting '%s'. Wait, it is slow.", gimp_file_get_utf8_name (file));

  g_object_get (config, "max-quantizer", &max_quantizer,
                "min-quantizer", &min_quantizer,
                "alpha-quantizer", &alpha_quantizer,
                "pixel-format", &pixel_format,
                "save-bit-depth", &save_bit_depth,
                "av1-encoder", &codec_choice,
                "encoder-speed", &encoder_speed,
                "save-color-profile", &save_icc_profile,
                "save-exif", &save_exif,
                "save-xmp", &save_xmp,
                NULL);

  num_threads = gimp_get_num_processors();
  num_threads = CLAMP (num_threads, 1, 64);

  profile = gimp_image_get_effective_color_profile (image);

  switch (gimp_image_get_precision (image))
    {
    case GIMP_PRECISION_U8_LINEAR:
      out_linear = TRUE;
      break;

    case GIMP_PRECISION_U8_NON_LINEAR:
      out_linear = FALSE;
      break;

    case GIMP_PRECISION_U16_LINEAR:
    case GIMP_PRECISION_U32_LINEAR:
    case GIMP_PRECISION_HALF_LINEAR:
    case GIMP_PRECISION_FLOAT_LINEAR:
    case GIMP_PRECISION_DOUBLE_LINEAR:
      out_linear = TRUE;
      break;
    case GIMP_PRECISION_U16_NON_LINEAR:
    case GIMP_PRECISION_U32_NON_LINEAR:
    case GIMP_PRECISION_HALF_NON_LINEAR:
    case GIMP_PRECISION_FLOAT_NON_LINEAR:
    case GIMP_PRECISION_DOUBLE_NON_LINEAR:
      out_linear = FALSE;
      break;

    default:
      if (gimp_color_profile_is_linear (profile))
        {
          out_linear = TRUE;
        }
      else
        {
          out_linear = FALSE;
        }
    }

  space = gimp_color_profile_get_space (profile,
                                        GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                        error);

  if (error && *error)
    {
      g_printerr ("%s: error getting the profile space: %s\n", G_STRFUNC, (*error)->message);
      g_clear_error (error);
      space = gimp_drawable_get_format (drawables[0]);
    }

  if ( (drawable_type == GIMP_GRAYA_IMAGE) ||
       (drawable_type == GIMP_GRAY_IMAGE))    /* force YUV400 for grey images */
    {
      pixel_format = AVIF_PIXEL_FORMAT_YUV400;
    }

  avif = avifImageCreate (drawable_width, drawable_height, save_bit_depth, pixel_format);
  avif->yuvRange = AVIF_RANGE_FULL;

  if (save_icc_profile)
    {
      const uint8_t  *icc_data = NULL;
      size_t          icc_length = 0;

      avif->matrixCoefficients = (avifMatrixCoefficients) 1;   /* AVIF_MATRIX_COEFFICIENTS_BT709 */

      if (gimp_color_profile_is_gray (profile))
        {
          g_object_unref (profile);

          if (out_linear)
            {
              profile = gimp_color_profile_new_d65_gray_linear ();
              avif->transferCharacteristics = (avifTransferCharacteristics) 8;   /* AVIF_TRANSFER_CHARACTERISTICS_LINEAR */
            }
          else
            {
              profile = gimp_color_profile_new_d65_gray_srgb_trc ();
              avif->transferCharacteristics = (avifTransferCharacteristics) 13;   /* AVIF_TRANSFER_CHARACTERISTICS_SRGB */
            }
        }
      icc_data = gimp_color_profile_get_icc_profile (profile, &icc_length);
      avifImageSetProfileICC (avif, icc_data, icc_length);

    }
  else
    {

      if ( (drawable_type == GIMP_GRAYA_IMAGE) ||
           (drawable_type == GIMP_GRAY_IMAGE))    /* grey profiles */
        {
          avif->colorPrimaries = (avifColorPrimaries) 1;   /* AVIF_COLOR_PRIMARIES_BT709 */
          if (out_linear)
            {
              avif->transferCharacteristics = (avifTransferCharacteristics) 8;   /* AVIF_TRANSFER_CHARACTERISTICS_LINEAR */
            }
          else
            {
              avif->transferCharacteristics = (avifTransferCharacteristics) 13;   /* AVIF_TRANSFER_CHARACTERISTICS_SRGB */
            }

          avif->matrixCoefficients = (avifMatrixCoefficients) 1;   /* AVIF_MATRIX_COEFFICIENTS_BT709 */
        }
      else /* autodetect supported primaries */
        {
          double xw, yw, xr, yr, xg, yg, xb, yb;
          const Babl *red_trc;
          const Babl *green_trc;
          const Babl *blue_trc;
          float primaries[8]; /* rX, rY, gX, gY, bX, bY, wX, wY */
          const char *primaries_name = NULL;
          avifColorPrimaries primaries_found = (avifColorPrimaries) 0;

          babl_space_get (space, &xw, &yw, &xr, &yr, &xg, &yg, &xb, &yb, &red_trc, &green_trc, &blue_trc);

          primaries[0] = xr;
          primaries[1] = yr;
          primaries[2] = xg;
          primaries[3] = yg;
          primaries[4] = xb;
          primaries[5] = yb;
          primaries[6] = xw;
          primaries[7] = yw;

          primaries_found = avifColorPrimariesFind (primaries, &primaries_name);

          if (primaries_found == 0)
            {
              primaries_found = ColorPrimariesBestMatch (primaries);
            }

          avif->colorPrimaries = primaries_found;

          if (primaries_found == 1)
            {
              avif->matrixCoefficients = (avifMatrixCoefficients) 1;   /* AVIF_MATRIX_COEFFICIENTS_BT709 */
            }
          else
            {
              avif->matrixCoefficients = (avifMatrixCoefficients) 12;   /* AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL */
            }

          if (out_linear)
            {
              red_trc = babl_trc ("linear");
              avif->transferCharacteristics = (avifTransferCharacteristics) 8;   /* AVIF_TRANSFER_CHARACTERISTICS_LINEAR */
            }
          else
            {
              red_trc = babl_trc ("sRGB");
              avif->transferCharacteristics = (avifTransferCharacteristics) 13;   /* AVIF_TRANSFER_CHARACTERISTICS_SRGB */
            }

          green_trc = red_trc;
          blue_trc = red_trc;

          avifColorPrimariesGetValues (primaries_found, primaries);

          xw = primaries[6];
          yw = primaries[7];
          xr = primaries[0];
          yr = primaries[1];
          xg = primaries[2];
          yg = primaries[3];
          xb = primaries[4];
          yb = primaries[5];

          space = babl_space_from_chromaticities (NULL, xw, yw, xr, yr, xg, yg, xb, yb, red_trc, green_trc, blue_trc, BABL_SPACE_FLAG_NONE);
          if (!space)
            {
              g_warning ("babl_space_from_chromaticities failed!\n");
            }
        }
    }

  g_object_unref (profile);

  switch (drawable_type)
    {
    case GIMP_RGBA_IMAGE:
      save_alpha = TRUE;
      is_gray = FALSE;

      if (save_bit_depth == 8)
        {
          pixels = g_new (guchar, drawable_width * drawable_height * 4);
          if (out_linear)
            {
              file_format = babl_format_with_space ("RGBA u8", space);
            }
          else
            {
              file_format = babl_format_with_space ("R'G'B'A u8", space);
            }
        }
      else
        {
          pixels = g_new (guchar, drawable_width * drawable_height * 8);
          if (out_linear)
            {
              file_format = babl_format_with_space ("RGBA u16", space);
            }
          else
            {
              file_format = babl_format_with_space ("R'G'B'A u16", space);
            }
        }
      break;
    case GIMP_RGB_IMAGE:
      save_alpha = FALSE;
      is_gray = FALSE;

      if (save_bit_depth == 8)
        {
          pixels = g_new (guchar, drawable_width * drawable_height * 3);
          if (out_linear)
            {
              file_format = babl_format_with_space ("RGB u8", space);
            }
          else
            {
              file_format = babl_format_with_space ("R'G'B' u8", space);
            }
        }
      else
        {
          pixels = g_new (guchar, drawable_width * drawable_height * 6);
          if (out_linear)
            {
              file_format = babl_format_with_space ("RGB u16", space);
            }
          else
            {
              file_format = babl_format_with_space ("R'G'B' u16", space);
            }
        }
      break;
    case GIMP_GRAYA_IMAGE:
      save_alpha = TRUE;
      is_gray = TRUE;

      if (save_bit_depth == 8)
        {
          pixels = g_new (guchar, drawable_width * drawable_height * 2);
          if (out_linear)
            {
              file_format = babl_format ("YA u8");
            }
          else
            {
              file_format = babl_format ("Y'A u8");
            }
        }
      else
        {
          pixels = g_new (guchar, drawable_width * drawable_height * 4);
          if (out_linear)
            {
              file_format = babl_format ("YA u16");
            }
          else
            {
              file_format = babl_format ("Y'A u16");
            }
        }
      break;
    case GIMP_GRAY_IMAGE:
      save_alpha = FALSE;
      is_gray = TRUE;

      if (save_bit_depth == 8)
        {
          pixels = g_new (guchar, drawable_width * drawable_height);
          if (out_linear)
            {
              file_format = babl_format ("Y u8");
            }
          else
            {
              file_format = babl_format ("Y' u8");
            }
        }
      else
        {
          pixels = g_new (guchar, drawable_width * drawable_height * 2);
          if (out_linear)
            {
              file_format = babl_format ("Y u16");
            }
          else
            {
              file_format = babl_format ("Y' u16");
            }
        }
      break;
    default:
      g_assert_not_reached ();
    }


  if (metadata && (save_exif || save_xmp))
    {
      GimpMetadata         *filtered_metadata;
      GimpMetadataSaveFlags metadata_flags = 0;

      if (save_exif)
        {
          metadata_flags |= GIMP_METADATA_SAVE_EXIF;
        }

      if (save_xmp)
        {
          metadata_flags |= GIMP_METADATA_SAVE_XMP;
        }

      filtered_metadata = gimp_image_metadata_save_filter (image, "image/heif", metadata, metadata_flags, NULL, error);
      if(! filtered_metadata)
        {
          if (error && *error)
            {
              g_printerr ("%s: error filtering metadata: %s",
                          G_STRFUNC, (*error)->message);
              g_clear_error (error);
            }
        }
      else
        {
          GExiv2Metadata *filtered_g2metadata = GEXIV2_METADATA (filtered_metadata);

          /*  EXIF metadata  */
          if (save_exif && gexiv2_metadata_has_exif (filtered_g2metadata))
            {
              GBytes *raw_exif_data;

              raw_exif_data = gexiv2_metadata_get_exif_data (filtered_g2metadata, GEXIV2_BYTE_ORDER_LITTLE, error);
              if (raw_exif_data)
                {
                  gsize exif_size = 0;
                  gconstpointer exif_buffer = g_bytes_get_data (raw_exif_data, &exif_size);

                  if (exif_size >= 4)
                    {
                      avifImageSetMetadataExif (avif, (const uint8_t *) exif_buffer, exif_size);
                    }
                  g_bytes_unref (raw_exif_data);
                }
              else
                {
                  if (error && *error)
                    {
                      g_printerr ("%s: error preparing EXIF metadata: %s",
                                  G_STRFUNC, (*error)->message);
                      g_clear_error (error);
                    }
                }
            }

          /*  XMP metadata  */
          if (save_xmp && gexiv2_metadata_has_xmp (filtered_g2metadata))
            {
              gchar *xmp_packet;

              xmp_packet = gexiv2_metadata_try_generate_xmp_packet (filtered_g2metadata, GEXIV2_USE_COMPACT_FORMAT | GEXIV2_OMIT_ALL_FORMATTING, 0, NULL);
              if (xmp_packet)
                {
                  size_t xmp_size = strlen (xmp_packet);
                  if (xmp_size > 0)
                    {
                      avifImageSetMetadataXMP (avif, (const uint8_t *) xmp_packet, xmp_size);
                    }
                  g_free (xmp_packet);
                }
            }

          g_object_unref (filtered_metadata);
        }
    }


  if (max_quantizer > AVIF_QUANTIZER_WORST_QUALITY)
    {
      max_quantizer = AVIF_QUANTIZER_WORST_QUALITY;
    }
  else if (max_quantizer < AVIF_QUANTIZER_BEST_QUALITY)
    {
      max_quantizer = AVIF_QUANTIZER_BEST_QUALITY;
    }

  if (min_quantizer > max_quantizer)
    {
      min_quantizer = max_quantizer;
    }
  else if (min_quantizer < AVIF_QUANTIZER_BEST_QUALITY)
    {
      min_quantizer = AVIF_QUANTIZER_BEST_QUALITY;
    }

  if (encoder_speed < AVIF_SPEED_SLOWEST)
    {
      encoder_speed = AVIF_SPEED_SLOWEST;
    }
  else if (encoder_speed > AVIF_SPEED_FASTEST)
    {
      encoder_speed = AVIF_SPEED_FASTEST;
    }

  encoder = avifEncoderCreate();
  encoder->maxThreads = num_threads;
  encoder->minQuantizer = min_quantizer;
  encoder->maxQuantizer = max_quantizer;
  encoder->speed = encoder_speed;
  encoder->codecChoice = codec_choice;

  if (save_alpha)
    {
      encoder->minQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;

      if (alpha_quantizer > AVIF_QUANTIZER_WORST_QUALITY)
        {
          alpha_quantizer = AVIF_QUANTIZER_WORST_QUALITY;
        }
      else if (alpha_quantizer < AVIF_QUANTIZER_BEST_QUALITY)
        {
          alpha_quantizer = AVIF_QUANTIZER_BEST_QUALITY;
        }

      encoder->maxQuantizerAlpha = alpha_quantizer;
    }

  if (n_drawables >= 2)
    {
      encoder->timescale = animation_timescale;
    }

  avifplugin_set_tiles (drawable_width, drawable_height, encoder);
  /* debug info to print encoder parameters
  printf ( "Qmin: %d, Qmax: %d, Qalpha: %d, Speed: %d, tileColsLog2: %d, tileRowsLog2 %d, Encoder: %d, threads: %d\n",
           encoder->minQuantizer, encoder->maxQuantizer, encoder->maxQuantizerAlpha,
           encoder->speed, encoder->tileColsLog2, encoder->tileRowsLog2, encoder->codecChoice,encoder->maxThreads );
  */

  if (save_alpha)
    {
      avif->alphaRange = AVIF_RANGE_FULL;
      avifImageAllocatePlanes (avif, AVIF_PLANES_YUV | AVIF_PLANES_A);
    }
  else
    {
      avifImageAllocatePlanes (avif, AVIF_PLANES_YUV);
    }

  for (frame_index = n_drawables - 1; frame_index >= 0; frame_index--)
    {
      /* fetch the image */
      buffer = gimp_drawable_get_buffer (drawables[frame_index]);
      gegl_buffer_get (buffer, GEGL_RECTANGLE (0, 0,
                       drawable_width, drawable_height), 1.0,
                       file_format, pixels,
                       GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

      g_object_unref (buffer);


      if (is_gray)   /* Gray export */
        {
          if (avifImageUsesU16 (avif))
            {
              const uint16_t  *graypixels_src = (const uint16_t *) pixels;
              uint16_t  *graypixels_dest;
              int tmp_pixelval;

              if (save_alpha)
                {
                  uint16_t  *alpha_dest;

                  if (save_bit_depth == 10)
                    {
                      for (j = 0; j < drawable_height; ++j)
                        {
                          graypixels_dest = (uint16_t *) (j * avif->yuvRowBytes[0]   + avif->yuvPlanes[0]);
                          alpha_dest      = (uint16_t *) (j * avif->alphaRowBytes + avif->alphaPlane);
                          for (i = 0; i < drawable_width; ++i)
                            {
                              tmp_pixelval = (int) ( ( (float) (*graypixels_src) / 65535.0f) * 1023.0f + 0.5f);
                              *graypixels_dest = CLAMP (tmp_pixelval, 0, 1023);
                              graypixels_dest++;
                              graypixels_src++;

                              tmp_pixelval = (int) ( ( (float) (*graypixels_src) / 65535.0f) * 1023.0f + 0.5f);
                              *alpha_dest = CLAMP (tmp_pixelval, 0, 1023);
                              alpha_dest++;
                              graypixels_src++;
                            }
                        }
                    }
                  else /* save_bit_depth == 12 */
                    {
                      for (j = 0; j < drawable_height; ++j)
                        {
                          graypixels_dest = (uint16_t *) (j * avif->yuvRowBytes[0]   + avif->yuvPlanes[0]);
                          alpha_dest      = (uint16_t *) (j * avif->alphaRowBytes + avif->alphaPlane);
                          for (i = 0; i < drawable_width; ++i)
                            {
                              tmp_pixelval = (int) ( ( (float) (*graypixels_src) / 65535.0f) * 4095.0f + 0.5f);
                              *graypixels_dest = CLAMP (tmp_pixelval, 0, 4095);
                              graypixels_dest++;
                              graypixels_src++;

                              tmp_pixelval = (int) ( ( (float) (*graypixels_src) / 65535.0f) * 4095.0f + 0.5f);
                              *alpha_dest = CLAMP (tmp_pixelval, 0, 4095);
                              alpha_dest++;
                              graypixels_src++;
                            }
                        }
                    }
                }
              else /* no alpha channel */
                {
                  if (save_bit_depth == 10)
                    {
                      for (j = 0; j < drawable_height; ++j)
                        {
                          graypixels_dest = (uint16_t *) (j * avif->yuvRowBytes[0]   + avif->yuvPlanes[0]);
                          for (i = 0; i < drawable_width; ++i)
                            {
                              tmp_pixelval = (int) ( ( (float) (*graypixels_src) / 65535.0f) * 1023.0f + 0.5f);
                              *graypixels_dest = CLAMP (tmp_pixelval, 0, 1023);
                              graypixels_dest++;
                              graypixels_src++;
                            }
                        }
                    }
                  else /* save_bit_depth == 12 */
                    {
                      for (j = 0; j < drawable_height; ++j)
                        {
                          graypixels_dest = (uint16_t *) (j * avif->yuvRowBytes[0]   + avif->yuvPlanes[0]);
                          for (i = 0; i < drawable_width; ++i)
                            {
                              tmp_pixelval = (int) ( ( (float) (*graypixels_src) / 65535.0f) * 4095.0f + 0.5f);
                              *graypixels_dest = CLAMP (tmp_pixelval, 0, 4095);
                              graypixels_dest++;
                              graypixels_src++;
                            }
                        }
                    }
                }
            }
          else /* 8bit gray */
            {
              const uint8_t  *graypixels8_src = (const uint8_t *) pixels;
              uint8_t  *graypixels8_dest;
              if (save_alpha)
                {
                  uint8_t *alpha8_dest;

                  for (j = 0; j < drawable_height; ++j)
                    {
                      graypixels8_dest = j * avif->yuvRowBytes[0] + avif->yuvPlanes[0];
                      alpha8_dest = j * avif->alphaRowBytes + avif->alphaPlane;
                      for (i = 0; i < drawable_width; ++i)
                        {
                          *graypixels8_dest = *graypixels8_src;
                          graypixels8_dest++;
                          graypixels8_src++;

                          *alpha8_dest = *graypixels8_src;
                          alpha8_dest++;
                          graypixels8_src++;
                        }
                    }
                }
              else
                {

                  for (j = 0; j < drawable_height; ++j)
                    {
                      graypixels8_dest = j * avif->yuvRowBytes[0] + avif->yuvPlanes[0];
                      for (i = 0; i < drawable_width; ++i)
                        {
                          *graypixels8_dest = *graypixels8_src;
                          graypixels8_dest++;
                          graypixels8_src++;
                        }
                    }
                }
            }

        }
      else /* color export */
        {
          avifRGBImage rgb;
          avifRGBImageSetDefaults (&rgb, avif);
          rgb.pixels = pixels;

          if (avifImageUsesU16 (avif))     /* 10 and 12 bit depth export */
            {
              rgb.depth = 16;
              if (save_alpha)
                {
                  rgb.format = AVIF_RGB_FORMAT_RGBA;
                  rgb.rowBytes = rgb.width * 8;
                }
              else
                {
                  rgb.format = AVIF_RGB_FORMAT_RGB;
                  rgb.rowBytes = rgb.width * 6;
                }
            }
          else /* 8 bit depth export */
            {
              rgb.depth = 8;
              if (save_alpha)
                {
                  rgb.format = AVIF_RGB_FORMAT_RGBA;
                  rgb.rowBytes = rgb.width * 4;
                }
              else
                {
                  rgb.format = AVIF_RGB_FORMAT_RGB;
                  rgb.rowBytes = rgb.width * 3;
                }
            }

          res = avifImageRGBToYUV (avif, &rgb);
          if (res != AVIF_RESULT_OK)
            {
              g_message ("ERROR in avifImageRGBToYUV: %s\n", avifResultToString (res));
            }
        }

      res = avifEncoderAddImage (encoder, avif, animation_frame_duration, (n_drawables == 1) ? AVIF_ADD_IMAGE_FLAG_SINGLE : AVIF_ADD_IMAGE_FLAG_NONE);
      if (res != AVIF_RESULT_OK)
        {
          g_message ("ERROR in avifEncoderAddImage: %s\n", avifResultToString (res));
          g_free (pixels);
          avifImageDestroy (avif);
          avifEncoderDestroy (encoder);
          return FALSE;
        }

      gimp_progress_update (0.5 * frame_index / n_drawables);
    }

  g_free (pixels);
  avifImageDestroy (avif);
  res = avifEncoderFinish (encoder,  &raw);
  avifEncoderDestroy (encoder);

  if (res == AVIF_RESULT_OK)
    {
      gimp_progress_update (0.75);
      /* Let's take some file */
      outfile = g_fopen (g_file_peek_path (file), "wb");
      if (!outfile)
        {
          g_message ("Could not open '%s' for writing!\n", g_file_peek_path (file));
          avifRWDataFree (&raw);
          return FALSE;
        }

      fwrite (raw.data, 1, raw.size, outfile);
      fclose (outfile);


      gimp_progress_update (1.0);
      avifRWDataFree (&raw);
      return TRUE;
    }
  else
    {
      g_message ("ERROR: Failed to encode: %s\n", avifResultToString (res));
    }

  return FALSE;
}
