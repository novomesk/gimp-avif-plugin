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
#include "file-avif-exif.h"

#define MAX_TILE_WIDTH  4096
#define MAX_TILE_AREA  (4096 * 2304)
#define MAX_TILE_ROWS 64
#define MAX_TILE_COLS 64

typedef struct
{
  gchar *tag;
  gint  type;
} XmpStructs;

/* identical as gimp_image_metadata_copy_tag */
static void
avifplugin_image_metadata_copy_tag (GExiv2Metadata *src,
                                    GExiv2Metadata *dest,
                                    const gchar    *tag)
{
  gchar **values = gexiv2_metadata_get_tag_multiple (src, tag);

  if (values)
    {
      gexiv2_metadata_set_tag_multiple (dest, tag, (const gchar **) values);
      g_strfreev (values);
    }
  else
    {
      gchar *value = gexiv2_metadata_get_tag_string (src, tag);

      if (value)
        {
          gexiv2_metadata_set_tag_string (dest, tag, value);
          g_free (value);
        }
    }
}

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

gboolean   save_layer (GFile         *file,
                       GimpImage     *image,
                       GimpDrawable  *drawable,
                       GObject       *config,
                       GimpMetadata  *metadata,
                       GError       **error)
{
  gchar          *filename;
  FILE           *outfile;
  GeglBuffer     *buffer;
  GimpImageType   drawable_type;
  const Babl     *file_format = NULL;
  const Babl     *space;
  gint            drawable_width;
  gint            drawable_height;
  gint            savedepth;
  gboolean        out_linear, save_alpha, is_gray;
  guchar         *pixels;

  GimpColorProfile *profile = NULL;
  int             min_quantizer = AVIF_QUANTIZER_BEST_QUALITY;
  int             max_quantizer = 40;
  int             alpha_quantizer = AVIF_QUANTIZER_BEST_QUALITY;
  double          retval_double = max_quantizer;
  double          retval_double2 = min_quantizer;
  double          retval_double3;
  double          retval_double4 = alpha_quantizer;
  avifPixelFormat pixel_format = AVIF_PIXEL_FORMAT_YUV420;
  avifCodecChoice codec_choice = AVIF_CODEC_CHOICE_AUTO;
  gboolean        save_icc_profile = TRUE;
  gboolean        save_exif = FALSE;
  gboolean        save_xmp = FALSE;
  gboolean        save_12bit_depth = FALSE;
  gint            num_threads = 1;
  gint            encoder_speed;
  gint            i, j;
  avifImage      *avif;
  avifResult      res;
  avifRWData      raw = AVIF_DATA_EMPTY;
  avifEncoder    *encoder;


  filename = g_file_get_path (file);
  gimp_progress_init_printf ("Exporting '%s'. Wait, it is slow.", filename);

  g_object_get (config, "max-quantizer", &retval_double,
                "min-quantizer", &retval_double2,
                "alpha-quantizer", &retval_double4,
                "pixel-format", &pixel_format,
                "av1-encoder", &codec_choice,
                "encoder-speed", &retval_double3,
                "save-color-profile", &save_icc_profile,
                "save-exif", &save_exif,
                "save-xmp", &save_xmp,
                "save-12bit-depth", &save_12bit_depth,
                NULL);
  max_quantizer = (int) (retval_double + 0.5);
  min_quantizer = (int) (retval_double2 + 0.5);
  encoder_speed = (int) (retval_double3 + 0.5);
  alpha_quantizer = (int) (retval_double4 + 0.5);

  g_object_get (gegl_config(), "threads", &num_threads, NULL);
  if (num_threads < 1)
    {
      num_threads = 1;
    }

  buffer = gimp_drawable_get_buffer (drawable);

  drawable_type   = gimp_drawable_type (drawable);
  drawable_width  = gimp_drawable_width (drawable);
  drawable_height = gimp_drawable_height (drawable);


  profile = gimp_image_get_effective_color_profile (image);

  switch (gimp_image_get_precision (image))
    {
    case GIMP_PRECISION_U8_LINEAR:
      savedepth = 8;
      out_linear = TRUE;
      break;

    case GIMP_PRECISION_U8_NON_LINEAR:
      savedepth = 8;
      out_linear = FALSE;
      break;

    case GIMP_PRECISION_U16_LINEAR:
    case GIMP_PRECISION_U32_LINEAR:
    case GIMP_PRECISION_HALF_LINEAR:
    case GIMP_PRECISION_FLOAT_LINEAR:
    case GIMP_PRECISION_DOUBLE_LINEAR:
      savedepth = save_12bit_depth ? 12 : 10;
      out_linear = TRUE;
      break;
    case GIMP_PRECISION_U16_NON_LINEAR:
    case GIMP_PRECISION_U32_NON_LINEAR:
    case GIMP_PRECISION_HALF_NON_LINEAR:
    case GIMP_PRECISION_FLOAT_NON_LINEAR:
    case GIMP_PRECISION_DOUBLE_NON_LINEAR:
      savedepth = save_12bit_depth ? 12 : 10;
      out_linear = FALSE;
      break;

    default:
      savedepth = save_12bit_depth ? 12 : 10;
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
      space = gimp_drawable_get_format (drawable);
    }

  if ( (drawable_type == GIMP_GRAYA_IMAGE) ||
       (drawable_type == GIMP_GRAY_IMAGE))    /* force YUV400 for grey images */
    {
      pixel_format = AVIF_PIXEL_FORMAT_YUV400;
    }

  avif = avifImageCreate (drawable_width, drawable_height, savedepth, pixel_format);
  avif->yuvRange = AVIF_RANGE_FULL;

  if (save_icc_profile)
    {
      const uint8_t *icc_data;
      size_t         icc_length;

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

      if (savedepth == 8)
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

      if (savedepth == 8)
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

      if (savedepth == 8)
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

      if (savedepth == 8)
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



  if (save_exif && metadata)
    {
      if (gexiv2_metadata_get_supports_exif (GEXIV2_METADATA (metadata)) && gexiv2_metadata_has_exif (GEXIV2_METADATA (metadata)))
        {
          GimpMetadata  *new_exif_metadata = gimp_metadata_new ();
          GExiv2Metadata *new_gexiv2metadata = GEXIV2_METADATA (new_exif_metadata);
          size_t exifSize = 0;
          guchar *raw_exif_data;
          gchar **exif_data = gexiv2_metadata_get_exif_tags (GEXIV2_METADATA (metadata));

          gexiv2_metadata_clear_exif (new_gexiv2metadata);

          for (i = 0; exif_data[i] != NULL; i++)
            {
              if (! gexiv2_metadata_has_tag (new_gexiv2metadata, exif_data[i]) &&
                  gimp_metadata_is_tag_supported (exif_data[i], "image/avif"))
                {
                  avifplugin_image_metadata_copy_tag (GEXIV2_METADATA (metadata),
                                                      new_gexiv2metadata,
                                                      exif_data[i]);
                }
            }

          g_strfreev (exif_data);


          raw_exif_data = get_TIFF_Exif_raw_data (new_gexiv2metadata, &exifSize);
          if (raw_exif_data)
            {
              if (exifSize >= 4)
                {
                  avifImageSetMetadataExif (avif, raw_exif_data, exifSize);
                }
              g_free (raw_exif_data);
            }


          g_object_unref (new_exif_metadata);
        }
    }


  if (save_xmp && metadata)
    {
      if (gexiv2_metadata_get_supports_xmp (GEXIV2_METADATA (metadata)) && gexiv2_metadata_has_xmp (GEXIV2_METADATA (metadata)))
        {
          GimpMetadata  *new_metadata = gimp_metadata_new ();
          GExiv2Metadata *new_g2metadata = GEXIV2_METADATA (new_metadata);

          static const XmpStructs structlist[] =
          {
            { "Xmp.iptcExt.LocationCreated", GEXIV2_STRUCTURE_XA_BAG },
            { "Xmp.iptcExt.LocationShown",   GEXIV2_STRUCTURE_XA_BAG },
            { "Xmp.iptcExt.ArtworkOrObject", GEXIV2_STRUCTURE_XA_BAG },
            { "Xmp.iptcExt.RegistryId",      GEXIV2_STRUCTURE_XA_BAG },
            { "Xmp.xmpMM.History",           GEXIV2_STRUCTURE_XA_SEQ },
            { "Xmp.plus.ImageSupplier",      GEXIV2_STRUCTURE_XA_SEQ },
            { "Xmp.plus.ImageCreator",       GEXIV2_STRUCTURE_XA_SEQ },
            { "Xmp.plus.CopyrightOwner",     GEXIV2_STRUCTURE_XA_SEQ },
            { "Xmp.plus.Licensor",           GEXIV2_STRUCTURE_XA_SEQ }
          };

          gchar         **xmp_data;
          struct timeval  timer_usec;
          gint64          timestamp_usec;
          gchar           ts[128];
          gchar          *xmp_packet;

          gexiv2_metadata_clear_xmp (new_g2metadata);

          gettimeofday (&timer_usec, NULL);
          timestamp_usec = ( (gint64) timer_usec.tv_sec) * 1000000ll +
                           (gint64) timer_usec.tv_usec;
          g_snprintf (ts, sizeof (ts), "%" G_GINT64_FORMAT, timestamp_usec);

          gimp_metadata_add_xmp_history (metadata, "");

          gexiv2_metadata_set_tag_string (GEXIV2_METADATA (metadata),
                                          "Xmp.GIMP.TimeStamp",
                                          ts);

          gexiv2_metadata_set_tag_string (GEXIV2_METADATA (metadata),
                                          "Xmp.xmp.CreatorTool",
                                          "GIMP");

          gexiv2_metadata_set_tag_string (GEXIV2_METADATA (metadata),
                                          "Xmp.GIMP.Version",
                                          GIMP_VERSION);

          gexiv2_metadata_set_tag_string (GEXIV2_METADATA (metadata),
                                          "Xmp.GIMP.API",
                                          GIMP_API_VERSION);
          gexiv2_metadata_set_tag_string (GEXIV2_METADATA (metadata),
                                          "Xmp.GIMP.Platform",
#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
                                          "Windows"
#elif defined(__linux__)
                                          "Linux"
#elif defined(__APPLE__) && defined(__MACH__)
                                          "Mac OS"
#elif defined(unix) || defined(__unix__) || defined(__unix)
                                          "Unix"
#else
                                          "Unknown"
#endif
                                         );


          xmp_data = gexiv2_metadata_get_xmp_tags (GEXIV2_METADATA (metadata));

          /* Patch necessary structures */
          for (i = 0; i < (gint) G_N_ELEMENTS (structlist); i++)
            {
              gexiv2_metadata_set_xmp_tag_struct (GEXIV2_METADATA (new_g2metadata),
                                                  structlist[i].tag,
                                                  structlist[i].type);
            }

          for (i = 0; xmp_data[i] != NULL; i++)
            {
              if (! gexiv2_metadata_has_tag (new_g2metadata, xmp_data[i]) &&
                  gimp_metadata_is_tag_supported (xmp_data[i], "image/avif"))
                {
                  avifplugin_image_metadata_copy_tag (GEXIV2_METADATA (metadata),
                                                      new_g2metadata,
                                                      xmp_data[i]);
                }
            }

          g_strfreev (xmp_data);

          xmp_packet = gexiv2_metadata_generate_xmp_packet (new_g2metadata, GEXIV2_USE_COMPACT_FORMAT | GEXIV2_OMIT_ALL_FORMATTING, 0);
          if (xmp_packet)
            {
              size_t xmpSize = strlen (xmp_packet);
              if (xmpSize > 0)
                {
                  avifImageSetMetadataXMP (avif, (const uint8_t *) xmp_packet, xmpSize);
                }
              g_free (xmp_packet);
            }

          g_object_unref (new_metadata);
        }
    }


  /* fetch the image */
  gegl_buffer_get (buffer, GEGL_RECTANGLE (0, 0,
                   drawable_width, drawable_height), 1.0,
                   file_format, pixels,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

  g_object_unref (buffer);

  if (save_alpha)
    {
      avif->alphaRange = AVIF_RANGE_FULL;
      avifImageAllocatePlanes (avif, AVIF_PLANES_YUV | AVIF_PLANES_A);
    }
  else
    {
      avifImageAllocatePlanes (avif, AVIF_PLANES_YUV);
    }


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

              if (savedepth == 10)
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
              else /* savedepth == 12 */
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
              if (savedepth == 10)
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
              else /* savedepth == 12 */
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


      g_free (pixels);
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
      g_free (pixels);

      if (res != AVIF_RESULT_OK)
        {
          g_message ("ERROR in avifImageRGBToYUV: %s\n", avifResultToString (res));
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

  gimp_progress_update (0.5);

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

  avifplugin_set_tiles (drawable_width, drawable_height, encoder);
  /* debug info to print encoder parameters
  printf ( "Qmin: %d, Qmax: %d, Qalpha: %d, Speed: %d, tileColsLog2: %d, tileRowsLog2 %d, Encoder: %d, threads: %d\n",
           encoder->minQuantizer, encoder->maxQuantizer, encoder->maxQuantizerAlpha,
           encoder->speed, encoder->tileColsLog2, encoder->tileRowsLog2, encoder->codecChoice,encoder->maxThreads );
  */

  res = avifEncoderWrite (encoder, avif, &raw);
  avifEncoderDestroy (encoder);
  avifImageDestroy (avif);

  if (res == AVIF_RESULT_OK)
    {
      gimp_progress_update (0.75);
      /* Let's take some file */
      outfile = g_fopen (filename, "wb");
      if (!outfile)
        {
          g_message ("Could not open '%s' for writing!\n", filename);
          g_free (filename);
          avifRWDataFree (&raw);
          return FALSE;
        }

      g_free (filename);
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

  g_free (filename);
  return FALSE;
}

gboolean   save_animation (GFile         *file,
                           GimpImage     *image,
                           gint           n_drawables,
                           GimpDrawable **drawables,
                           GObject       *config,
                           GimpMetadata  *metadata,
                           GError       **error)
{
  g_message ("save_animation NOT IMPLEMENTED YET!!!\n");
  return FALSE;
}

