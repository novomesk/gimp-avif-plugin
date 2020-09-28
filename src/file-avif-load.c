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

/*
  hlgCurveBinary.h + pqCurveBinary.h
  are from https://github.com/joedrago/colorist

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include <avif/avif.h>
#include <lcms2.h>
#include <gexiv2/gexiv2.h>
#include <glib/gstdio.h>

#include "file-avif-load.h"

#include "hlgCurveBinary.h"
#include "pqCurveBinary.h"

typedef enum clProfileCurveType
{
  CL_PCT_UNKNOWN = 0,
  CL_PCT_GAMMA,
  CL_PCT_HLG,
  CL_PCT_PQ,
  CL_PCT_PARAMETRIC_SRGB,
  CL_PCT_PARAMETRIC_REC709
} clProfileCurveType;

static cmsHPROFILE _create_lcms_profile_from_NCLX (const char *description_suffix, const avifColorPrimaries colour_primaries,
    const clProfileCurveType trctype, const float gamma, const int maxLuminance)
{
  float prim[8]; /* outPrimaries: rX, rY, gX, gY, bX, bY, wX, wY */
  cmsCIExyYTRIPLE primaries;
  cmsCIExyY       whitepoint;
  cmsToneCurve   *curves[3];
  cmsHPROFILE     profile = NULL;
  cmsCIEXYZ       lumi;
  gchar          *description;
  const char     *prim_name = NULL;

  cmsFloat64Number srgb_parameters[5] =
  { 2.4, 1.0 / 1.055,  0.055 / 1.055, 1.0 / 12.92, 0.04045 };

  cmsFloat64Number rec709_parameters[5] =
  { 2.2, 1.0 / 1.099,  0.099 / 1.099, 1.0 / 4.5, 0.081 };

  avifColorPrimariesGetValues (colour_primaries, prim);
  avifColorPrimariesFind (prim, &prim_name);

  if (! prim_name)
    prim_name = "";

  primaries.Red.x = prim[0];
  primaries.Red.y = prim[1];
  primaries.Red.Y = 0.0f; /* unused */
  primaries.Green.x = prim[2];
  primaries.Green.y = prim[3];
  primaries.Green.Y = 0.0f; /* unused */
  primaries.Blue.x = prim[4];
  primaries.Blue.y = prim[5];
  primaries.Blue.Y = 0.0f; /* unused */
  whitepoint.x = prim[6];
  whitepoint.y = prim[7];
  whitepoint.Y = 1.0f;

  switch (trctype)
    {
    case CL_PCT_GAMMA:
      curves[0] = cmsBuildGamma (NULL, gamma);
      curves[1] = curves[0];
      curves[2] = curves[0];
      profile = cmsCreateRGBProfile (&whitepoint, &primaries, curves);
      cmsFreeToneCurve (curves[0]);
      break;
    case CL_PCT_HLG:
      profile = cmsCreateRGBProfile (&whitepoint, &primaries, NULL);
      cmsWriteRawTag (profile, cmsSigRedTRCTag, hlgCurveBinaryData, hlgCurveBinarySize);
      cmsLinkTag (profile, cmsSigGreenTRCTag, cmsSigRedTRCTag);
      cmsLinkTag (profile, cmsSigBlueTRCTag, cmsSigRedTRCTag);
      break;
    case CL_PCT_PQ:
      profile = cmsCreateRGBProfile (&whitepoint, &primaries, NULL);
      cmsWriteRawTag (profile, cmsSigRedTRCTag, pqCurveBinaryData, pqCurveBinarySize);
      cmsLinkTag (profile, cmsSigGreenTRCTag, cmsSigRedTRCTag);
      cmsLinkTag (profile, cmsSigBlueTRCTag, cmsSigRedTRCTag);
      break;
    case CL_PCT_PARAMETRIC_SRGB:
      curves[0] = cmsBuildParametricToneCurve (NULL, 4, srgb_parameters);
      curves[1] = curves[0];
      curves[2] = curves[0];
      profile = cmsCreateRGBProfile (&whitepoint, &primaries, curves);
      cmsFreeToneCurve (curves[0]);
      break;
    case CL_PCT_PARAMETRIC_REC709:
      curves[0] = cmsBuildParametricToneCurve (NULL, 4, rec709_parameters);
      curves[1] = curves[0];
      curves[2] = curves[0];
      profile = cmsCreateRGBProfile (&whitepoint, &primaries, curves);
      cmsFreeToneCurve (curves[0]);
      break;
    default:
      profile = NULL;
    }

  if (profile)
    {
      cmsMLU *mlu1 = cmsMLUalloc (NULL, 1);
      cmsMLU *mlu2 = cmsMLUalloc (NULL, 1);
      cmsMLU *mlu3 = cmsMLUalloc (NULL, 1);
      cmsMLU *mlu4 = cmsMLUalloc (NULL, 1);

      if (maxLuminance > 0)
        {
          lumi.X = 0.0f;
          lumi.Y = (cmsFloat64Number) maxLuminance;
          lumi.Z = 0.0f;
          cmsWriteTag (profile, cmsSigLuminanceTag, &lumi);
        }

      cmsSetHeaderFlags (profile, cmsEmbeddedProfileTrue | cmsUseAnywhere);

      cmsMLUsetASCII (mlu1, "en", "US", "Public Domain");
      cmsWriteTag (profile, cmsSigCopyrightTag, mlu1);

      description = g_strdup_printf ("%s %s", prim_name, description_suffix);

      cmsMLUsetASCII (mlu2, "en", "US", description);
      cmsWriteTag (profile, cmsSigProfileDescriptionTag, mlu2);

      cmsMLUsetASCII (mlu3, "en", "US", description);
      cmsWriteTag (profile, cmsSigDeviceModelDescTag, mlu3);

      g_free (description);

      cmsMLUsetASCII (mlu4, "en", "US", "Gimp AVIF plug-in");
      cmsWriteTag (profile, cmsSigDeviceMfgDescTag, mlu4);

      cmsMLUfree (mlu1);
      cmsMLUfree (mlu2);
      cmsMLUfree (mlu3);
      cmsMLUfree (mlu4);
    }

  return profile;
}

GimpImage *load_image (GFile       *file,
                       gboolean     interactive,
                       GError     **error)
{
  gchar            *filename = g_file_get_path (file);
  GimpImage        *image;
  GimpLayer        *layer;
  GeglBuffer       *buffer;

  gboolean          loadalpha;
  gboolean          loadgray;
  gboolean          loadlinear;

  GimpColorProfile *profile = NULL;
  GimpMetadata     *metadata = NULL;

  FILE *inputFile = g_fopen (filename, "rb");

  size_t            inputFileSize;
  avifRWData        raw = AVIF_DATA_EMPTY;
  avifDecoder      *decoder = NULL;
  avifResult        decodeResult;
  avifImage        *avif;

  if (!inputFile)
    {
      g_message ("Cannot open file for read: %s\n", filename);
      g_free (filename);
      return NULL;
    }

  fseek (inputFile, 0, SEEK_END);
  inputFileSize = ftell (inputFile);
  fseek (inputFile, 0, SEEK_SET);

  if (inputFileSize < 1)
    {
      g_message ("File too small: %s\n", filename);
      fclose (inputFile);
      g_free (filename);
      return NULL;
    }

  avifRWDataRealloc (&raw, inputFileSize);
  if (fread (raw.data, 1, inputFileSize, inputFile) != inputFileSize)
    {
      g_message ("Failed to read %zu bytes: %s\n", inputFileSize, filename);
      fclose (inputFile);
      avifRWDataFree (&raw);

      g_free (filename);
      return NULL;
    }

  fclose (inputFile);
  inputFile = NULL;

  if (avifPeekCompatibleFileType ( (avifROData *) &raw) == AVIF_FALSE)
    {
      g_message ("File %s is probably not in AVIF format!\n", filename);
      avifRWDataFree (&raw);
      g_free (filename);
      return NULL;
    }

  decoder = avifDecoderCreate();

  decodeResult = avifDecoderSetIOMemory (decoder, (avifROData *) &raw);
  if (decodeResult != AVIF_RESULT_OK)
    {
      g_message ("ERROR: avifDecoderSetIOMemory failed: %s\n", avifResultToString (decodeResult));

      avifDecoderDestroy (decoder);
      avifRWDataFree (&raw);
      g_free (filename);
      return NULL;
    }

  decodeResult = avifDecoderParse (decoder);
  if (decodeResult != AVIF_RESULT_OK)
    {
      g_message ("ERROR: Failed to parse input: %s\n", avifResultToString (decodeResult));

      avifDecoderDestroy (decoder);
      avifRWDataFree (&raw);
      g_free (filename);
      return NULL;
    }

  decodeResult = avifDecoderNextImage (decoder);
  if (decodeResult != AVIF_RESULT_OK)
    {
      g_message ("ERROR: Failed to decode image: %s\n", avifResultToString (decodeResult));

      avifDecoderDestroy (decoder);
      avifRWDataFree (&raw);
      g_free (filename);
      return NULL;
    }

  avif = decoder->image;

  if (avif->exif.size > 0 || avif->xmp.size > 0)
    {
      metadata = gimp_metadata_new ();

      if (avif->exif.size > 0)
        {
          GExiv2Metadata *exif_metadata = GEXIV2_METADATA (metadata);
          if (! gexiv2_metadata_open_buf (exif_metadata, avif->exif.data, avif->exif.size, error))
            {
              g_printerr ("%s: Failed to set EXIF metadata: %s\n", G_STRFUNC, (*error)->message);
              g_clear_error (error);
            }
        }

      if (avif->xmp.size > 0)
        {
          if (! gimp_metadata_set_from_xmp (metadata, avif->xmp.data, avif->xmp.size, error))
            {
              g_printerr ("%s: Failed to set XMP metadata: %s\n", G_STRFUNC, (*error)->message);
              g_clear_error (error);
            }
        }
    }


  if (avif->icc.data && (avif->icc.size > 0))     /* load profile from ICC */
    {
      profile = gimp_color_profile_new_from_icc_profile (avif->icc.data, avif->icc.size, error);
      if (profile)
        {
          loadlinear = gimp_color_profile_is_linear (profile);
          if (avif->matrixCoefficients != 0)
            {
              loadgray = gimp_color_profile_is_gray (profile);
            }
          else
            {
              /* AVIF_MATRIX_COEFFICIENTS_IDENTITY - image is RGBA */
              loadgray = FALSE;
            }
        }
      else /* error */
        {
          g_printerr ("%s: Failed to read ICC profile: %s\n", G_STRFUNC, (*error)->message);
          g_clear_error (error);

          loadgray = FALSE;
          loadlinear = FALSE;
        }
    }
  else /* load profile from CICP/NCLX information */
    {
      if (avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV400)   /* creating gray profile */
        {
          loadgray = TRUE;
          if (avif->transferCharacteristics == 8)   /* AVIF_TRANSFER_CHARACTERISTICS_LINEAR */
            {
              profile = gimp_color_profile_new_d65_gray_linear ();
              loadlinear = TRUE;
            }
          else
            {
              profile = gimp_color_profile_new_d65_gray_srgb_trc ();
              loadlinear = FALSE;
            }
        }
      else /* creating color profile */
        {
          cmsHPROFILE lcms_profile = NULL;
          avifColorPrimaries primaries_to_load;
          avifTransferCharacteristics trc_to_load;

          loadgray = FALSE;
          loadlinear = FALSE;

          if ( (avif->colorPrimaries == 2 /* AVIF_COLOR_PRIMARIES_UNSPECIFIED */) ||
               (avif->colorPrimaries == 0 /* AVIF_COLOR_PRIMARIES_UNKNOWN */))
            {
              primaries_to_load = (avifColorPrimaries) 1;   /* AVIF_COLOR_PRIMARIES_BT709 */
            }
          else
            {
              primaries_to_load = avif->colorPrimaries;
            }

          if ( (avif->transferCharacteristics == 2 /* AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED */) ||
               (avif->transferCharacteristics == 0 /* AVIF_TRANSFER_CHARACTERISTICS_UNKNOWN */))
            {
              trc_to_load = (avifTransferCharacteristics) 13;   /* AVIF_TRANSFER_CHARACTERISTICS_SRGB */
            }
          else
            {
              trc_to_load = avif->transferCharacteristics;
            }

          switch (trc_to_load)
            {
            /* AVIF_TRANSFER_CHARACTERISTICS_HLG */
            case 18:
              lcms_profile = _create_lcms_profile_from_NCLX ("HLG RGB", primaries_to_load, CL_PCT_HLG, 0, 0);
              break;
            /* AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084 */
            case 16:
              lcms_profile = _create_lcms_profile_from_NCLX ("PQ RGB", primaries_to_load, CL_PCT_PQ, 0, 10000);
              break;
            /* AVIF_TRANSFER_CHARACTERISTICS_BT470M */
            case 4:
              lcms_profile = _create_lcms_profile_from_NCLX ("Gamma2.2 RGB", primaries_to_load, CL_PCT_GAMMA, 2.2f, 0);
              break;
            /* AVIF_TRANSFER_CHARACTERISTICS_BT470BG */
            case 5:
              lcms_profile = _create_lcms_profile_from_NCLX ("Gamma2.8 RGB", primaries_to_load, CL_PCT_GAMMA, 2.8f, 0);
              break;
            /* AVIF_TRANSFER_CHARACTERISTICS_LINEAR */
            case 8:
              lcms_profile = _create_lcms_profile_from_NCLX ("linear RGB", primaries_to_load, CL_PCT_GAMMA, 1.0f, 0);
              loadlinear = TRUE;
              break;
            /* AVIF_TRANSFER_CHARACTERISTICS_SRGB */
            case 13:
              lcms_profile = _create_lcms_profile_from_NCLX ("sRGB-TRC RGB", primaries_to_load, CL_PCT_PARAMETRIC_SRGB, 0, 0);
              break;
            /* AVIF_TRANSFER_CHARACTERISTICS_BT709 */
            case 1:
              lcms_profile = _create_lcms_profile_from_NCLX ("Rec709 RGB", primaries_to_load, CL_PCT_PARAMETRIC_REC709, 0, 0);
              break;
            default:
              /* missing implementation, showing a debug message so far */
              g_message ("CICP colorPrimaries: %d, transferCharacteristics: %d\nPlease, report file to the plug-in author.", avif->colorPrimaries, avif->transferCharacteristics);
              profile = NULL;
              lcms_profile = NULL;
              break;
            }

          if (lcms_profile)
            {
              profile = gimp_color_profile_new_from_lcms_profile (lcms_profile, error);
              if (! profile)
                {
                  g_printerr ("%s: gimp_color_profile_new_from_lcms_profile call failed: %s\n", G_STRFUNC, (*error)->message);
                  g_clear_error (error);
                }
              cmsCloseProfile (lcms_profile);
            }
        }
    }

  if (avif->alphaPlane)
    {
      loadalpha = TRUE;
    }
  else
    {
      loadalpha = FALSE;
    }

  if (loadgray)   /* grayscale */
    {
      const gint grayimg_width = avif->width;
      const gint grayimg_height = avif->height;
      gint x, y;
      gpointer pixels;

      if (avifImageUsesU16 (avif))     /* 10 and 12 bit depth import */
        {
          uint16_t *gray16_pixel;
          const uint16_t *alpha16_src;
          const uint16_t *gray16_src;
          uint16_t tmpval16, tmp16_alpha;
          int tmp_pixelval;

          if (loadlinear)
            {
              image = gimp_image_new_with_precision (grayimg_width, grayimg_height, GIMP_GRAY, GIMP_PRECISION_U16_LINEAR);
            }
          else
            {
              image = gimp_image_new_with_precision (grayimg_width, grayimg_height, GIMP_GRAY, GIMP_PRECISION_U16_NON_LINEAR);
            }

          if (profile)
            {
              if (gimp_color_profile_is_gray (profile))
                {
                  gimp_image_set_color_profile (image, profile);
                }
            }

          if (loadalpha)
            {
              pixels = g_malloc_n (grayimg_height, grayimg_width * 4);

              gray16_pixel = pixels;
              for (y = 0; y < grayimg_height; y++)
                {
                  gray16_src = (const uint16_t *) (y * avif->yuvRowBytes[0] + avif->yuvPlanes[0]);
                  alpha16_src = (const uint16_t *) (y * avif->alphaRowBytes + avif->alphaPlane);
                  for (x = 0; x < grayimg_width; x++)
                    {
                      tmpval16 = *gray16_src;
                      tmp16_alpha = *alpha16_src;
                      gray16_src++;
                      alpha16_src++;

                      if (avif->depth == 10)   /* 10 bit depth */
                        {
                          if (avif->yuvRange == AVIF_RANGE_LIMITED)
                            {
                              tmpval16 = avifLimitedToFullY (10, tmpval16);
                            }

                          if (avif->alphaRange == AVIF_RANGE_LIMITED)
                            {
                              tmp16_alpha = avifLimitedToFullY (10, tmp16_alpha);
                            }

                          tmp_pixelval = (int) ( ( (float) tmpval16 / 1023.0f) * 65535.0f + 0.5f);
                          *gray16_pixel = CLAMP (tmp_pixelval, 0, 65535);
                          gray16_pixel++;

                          tmp_pixelval = (int) ( ( (float) tmp16_alpha / 1023.0f) * 65535.0f + 0.5f);
                          *gray16_pixel = CLAMP (tmp_pixelval, 0, 65535);
                        }
                      else /* 12 bit depth */
                        {
                          if (avif->yuvRange == AVIF_RANGE_LIMITED)
                            {
                              tmpval16 = avifLimitedToFullY (12, tmpval16);
                            }

                          if (avif->alphaRange == AVIF_RANGE_LIMITED)
                            {
                              tmp16_alpha = avifLimitedToFullY (12, tmp16_alpha);
                            }

                          tmp_pixelval = (int) ( ( (float) tmpval16 / 4095.0f) * 65535.0f + 0.5f);
                          *gray16_pixel = CLAMP (tmp_pixelval, 0, 65535);
                          gray16_pixel++;

                          tmp_pixelval = (int) ( ( (float) tmp16_alpha / 4095.0f) * 65535.0f + 0.5f);
                          *gray16_pixel = CLAMP (tmp_pixelval, 0, 65535);
                        }

                      gray16_pixel++;
                    }
                }

              layer = gimp_layer_new (image, "Background",
                                      grayimg_width, grayimg_height,
                                      GIMP_GRAYA_IMAGE, 100,
                                      gimp_image_get_default_new_layer_mode (image));
            }
          else /* no alpha */
            {
              pixels = g_malloc_n (grayimg_height, grayimg_width * 2);

              gray16_pixel = pixels;
              for (y = 0; y < grayimg_height; y++)
                {
                  gray16_src = (const uint16_t *) (y * avif->yuvRowBytes[0] + avif->yuvPlanes[0]);
                  for (x = 0; x < grayimg_width; x++)
                    {
                      tmpval16 = *gray16_src;
                      gray16_src++;

                      if (avif->depth == 10)   /* 10 bit depth */
                        {
                          if (avif->yuvRange == AVIF_RANGE_LIMITED)
                            {
                              tmpval16 = avifLimitedToFullY (10, tmpval16);
                            }

                          tmp_pixelval = (int) ( ( (float) tmpval16 / 1023.0f) * 65535.0f + 0.5f);
                        }
                      else /* 12 bit depth */
                        {
                          if (avif->yuvRange == AVIF_RANGE_LIMITED)
                            {
                              tmpval16 = avifLimitedToFullY (12, tmpval16);
                            }

                          tmp_pixelval = (int) ( ( (float) tmpval16 / 4095.0f) * 65535.0f + 0.5f);
                        }

                      *gray16_pixel = CLAMP (tmp_pixelval, 0, 65535);
                      gray16_pixel++;

                    }
                }

              layer = gimp_layer_new (image, "Background",
                                      grayimg_width, grayimg_height,
                                      GIMP_GRAY_IMAGE, 100,
                                      gimp_image_get_default_new_layer_mode (image));
            }
        }
      else /* 8 bit depth import */
        {
          uint8_t *gray8_pixel;
          const uint8_t *alpha8_src;
          const uint8_t *gray8_src;

          if (loadlinear)
            {
              image = gimp_image_new_with_precision (grayimg_width, grayimg_height, GIMP_GRAY, GIMP_PRECISION_U8_LINEAR);
            }
          else
            {
              image = gimp_image_new_with_precision (grayimg_width, grayimg_height, GIMP_GRAY, GIMP_PRECISION_U8_NON_LINEAR);
            }

          if (profile)
            {
              if (gimp_color_profile_is_gray (profile))
                {
                  gimp_image_set_color_profile (image, profile);
                }
            }

          if (loadalpha)
            {
              pixels = g_malloc_n (grayimg_height, grayimg_width * 2);

              gray8_pixel = pixels;
              for (y = 0; y < grayimg_height; y++)
                {
                  gray8_src =  y * avif->yuvRowBytes[0] + avif->yuvPlanes[0];
                  alpha8_src =  y * avif->alphaRowBytes + avif->alphaPlane;
                  for (x = 0; x < grayimg_width; x++)
                    {
                      if (avif->yuvRange == AVIF_RANGE_FULL)
                        {
                          *gray8_pixel = *gray8_src;
                        }
                      else
                        {
                          *gray8_pixel = avifLimitedToFullY (8, *gray8_src);
                        }
                      gray8_pixel++;
                      gray8_src++;

                      if (avif->alphaRange == AVIF_RANGE_FULL)
                        {
                          *gray8_pixel = *alpha8_src;
                        }
                      else
                        {
                          *gray8_pixel = avifLimitedToFullY (8, *alpha8_src);
                        }
                      gray8_pixel++;
                      alpha8_src++;
                    }
                }

              layer = gimp_layer_new (image, "Background",
                                      grayimg_width, grayimg_height,
                                      GIMP_GRAYA_IMAGE, 100,
                                      gimp_image_get_default_new_layer_mode (image));
            }
          else /* no alpha */
            {
              pixels = g_malloc_n (grayimg_height, grayimg_width);

              gray8_pixel = pixels;
              for (y = 0; y < grayimg_height; y++)
                {
                  gray8_src =  y * avif->yuvRowBytes[0] + avif->yuvPlanes[0];
                  for (x = 0; x < grayimg_width; x++)
                    {
                      if (avif->yuvRange == AVIF_RANGE_FULL)
                        {
                          *gray8_pixel = *gray8_src;
                        }
                      else
                        {
                          *gray8_pixel = avifLimitedToFullY (8, *gray8_src);
                        }
                      gray8_pixel++;
                      gray8_src++;
                    }
                }

              layer = gimp_layer_new (image, "Background",
                                      grayimg_width, grayimg_height,
                                      GIMP_GRAY_IMAGE, 100,
                                      gimp_image_get_default_new_layer_mode (image));
            }
        }

      gimp_image_insert_layer (image, layer, NULL, 0);

      buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));

      gegl_buffer_set (buffer, GEGL_RECTANGLE (0, 0, grayimg_width, grayimg_height), 0,
                       NULL, pixels, GEGL_AUTO_ROWSTRIDE);

      g_object_unref (buffer);
      g_free (pixels);

    }
  else /* loading colors, YUV to RGB conversion */
    {
      avifRGBImage rgb;
      avifRGBImageSetDefaults (&rgb, avif);

      if (loadalpha)
        {
          rgb.format = AVIF_RGB_FORMAT_RGBA;
        }
      else
        {
          rgb.format = AVIF_RGB_FORMAT_RGB;
        }



      if (avifImageUsesU16 (avif))     /* 10 and 12 bit depth import */
        {

          if (loadlinear)
            {
              image = gimp_image_new_with_precision (rgb.width, rgb.height, GIMP_RGB, GIMP_PRECISION_U16_LINEAR);
            }
          else
            {
              image = gimp_image_new_with_precision (rgb.width, rgb.height, GIMP_RGB, GIMP_PRECISION_U16_NON_LINEAR);
            }

          if (profile)
            {
              if (gimp_color_profile_is_rgb (profile))
                {
                  gimp_image_set_color_profile (image, profile);
                }
            }

          rgb.depth = 16;
          if (loadalpha)   /* RGBA */
            {
              rgb.rowBytes = rgb.width * 8;
              rgb.pixels = g_malloc_n (rgb.height, rgb.rowBytes);

              decodeResult = avifImageYUVToRGB (avif, &rgb);
              if (decodeResult != AVIF_RESULT_OK)
                {
                  g_printerr ("YUVToRGB conversion failed: %s\n", avifResultToString (decodeResult));
                }

              layer = gimp_layer_new (image, "Background",
                                      rgb.width, rgb.height,
                                      GIMP_RGBA_IMAGE, 100,
                                      gimp_image_get_default_new_layer_mode (image));
            }
          else /* RGB */
            {
              rgb.rowBytes = rgb.width * 6;
              rgb.pixels = g_malloc_n (rgb.height, rgb.rowBytes);

              decodeResult = avifImageYUVToRGB (avif, &rgb);
              if (decodeResult != AVIF_RESULT_OK)
                {
                  g_printerr ("YUVToRGB conversion failed: %s\n", avifResultToString (decodeResult));
                }

              layer = gimp_layer_new (image, "Background",
                                      rgb.width, rgb.height,
                                      GIMP_RGB_IMAGE, 100,
                                      gimp_image_get_default_new_layer_mode (image));
            }
        }
      else /* 8 bit depth import */
        {

          if (loadlinear)
            {
              image = gimp_image_new_with_precision (rgb.width, rgb.height, GIMP_RGB, GIMP_PRECISION_U8_LINEAR);
            }
          else
            {
              image = gimp_image_new_with_precision (rgb.width, rgb.height, GIMP_RGB, GIMP_PRECISION_U8_NON_LINEAR);
            }

          if (profile)
            {
              if (gimp_color_profile_is_rgb (profile))
                {
                  gimp_image_set_color_profile (image, profile);
                }
            }

          rgb.depth = 8;
          if (loadalpha)   /* RGBA */
            {
              rgb.rowBytes = rgb.width * 4;
              rgb.pixels = g_malloc_n (rgb.height, rgb.rowBytes);

              decodeResult = avifImageYUVToRGB (avif, &rgb);
              if (decodeResult != AVIF_RESULT_OK)
                {
                  g_printerr ("YUVToRGB conversion failed: %s\n", avifResultToString (decodeResult));
                }

              layer = gimp_layer_new (image, "Background",
                                      rgb.width, rgb.height,
                                      GIMP_RGBA_IMAGE, 100,
                                      gimp_image_get_default_new_layer_mode (image));
            }
          else /* RGB */
            {
              rgb.rowBytes = rgb.width * 3;
              rgb.pixels = g_malloc_n (rgb.height, rgb.rowBytes);

              decodeResult = avifImageYUVToRGB (avif, &rgb);
              if (decodeResult != AVIF_RESULT_OK)
                {
                  g_printerr ("YUVToRGB conversion failed: %s\n", avifResultToString (decodeResult));
                }

              layer = gimp_layer_new (image, "Background",
                                      rgb.width, rgb.height,
                                      GIMP_RGB_IMAGE, 100,
                                      gimp_image_get_default_new_layer_mode (image));
            }
        }

      gimp_image_insert_layer (image, layer, NULL, 0);

      buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));

      gegl_buffer_set (buffer, GEGL_RECTANGLE (0, 0, rgb.width, rgb.height), 0,
                       NULL, rgb.pixels, GEGL_AUTO_ROWSTRIDE);

      g_object_unref (buffer);
      g_free (rgb.pixels);

      if (profile)
        {
          if (gimp_color_profile_is_gray (profile) && image)     /* image was loaded as RGB but ICC profile indicate grayscale */
            {
              gimp_image_convert_grayscale (image);
            }

        }
    }

  gimp_image_undo_disable (image);
  gimp_image_set_file (image, file);

  if (profile)
    {
      g_object_unref (profile);
      profile = NULL;
    }

  /* transformations */
  if (avif->transformFlags & AVIF_TRANSFORM_CLAP)
    {
      if ( (avif->clap.widthD > 0) && (avif->clap.heightD > 0) &&
           (avif->clap.horizOffD > 0) && (avif->clap.vertOffD > 0))
        {
          const gint avif_width = avif->width;
          const gint avif_height = avif->height;
          gint  new_width, new_height, offx, offy;

          new_width = (gint) ( (double) (avif->clap.widthN)  / (avif->clap.widthD) + 0.5);
          if (new_width > avif_width)
            {
              new_width = avif_width;
            }

          new_height = (gint) ( (double) (avif->clap.heightN) / (avif->clap.heightD) + 0.5);
          if (new_height > avif_height)
            {
              new_height = avif_height;
            }

          if (new_width > 0 && new_height > 0)
            {

              offx = ( (double) ( (int32_t) avif->clap.horizOffN)) / (avif->clap.horizOffD) +
                     (avif_width - new_width) / 2.0 + 0.5;
              if (offx < 0)
                {
                  offx = 0;
                }
              else if (offx > (avif_width - new_width))
                {
                  offx = avif_width - new_width;
                }

              offy = ( (double) ( (int32_t) avif->clap.vertOffN)) / (avif->clap.vertOffD) +
                     (avif_height - new_height) / 2.0 + 0.5;
              if (offy < 0)
                {
                  offy = 0;
                }
              else if (offy > (avif_height - new_height))
                {
                  offy = avif_height - new_height;
                }

              gimp_image_crop (image, new_width, new_height, offx, offy);
            }
        }

      else /* Zero values, we need to avoid 0 divide. */
        {
          g_message ("ERROR: Wrong values in avifCleanApertureBox\n");
        }
    }

  if (avif->transformFlags & AVIF_TRANSFORM_IROT)
    {
      switch (avif->irot.angle)
        {
        case 1:
          gimp_image_rotate (image, GIMP_ROTATE_270);
          break;
        case 2:
          gimp_image_rotate (image, GIMP_ROTATE_180);
          break;
        case 3:
          gimp_image_rotate (image, GIMP_ROTATE_90);
          break;
        }
    }

  if (avif->transformFlags & AVIF_TRANSFORM_IMIR)
    {
      switch (avif->imir.axis)
        {
        case 0:
          gimp_image_flip (image, GIMP_ORIENTATION_VERTICAL);
          break;
        case 1:
          gimp_image_flip (image, GIMP_ORIENTATION_HORIZONTAL);
          break;
        }
    }

  if (metadata && image)
    {
      GimpMetadataLoadFlags flags = GIMP_METADATA_LOAD_COMMENT | GIMP_METADATA_LOAD_RESOLUTION ;
      gexiv2_metadata_erase_exif_thumbnail (GEXIV2_METADATA (metadata));
      gimp_image_set_metadata (image, metadata);

      gimp_image_metadata_load_finish (image, "image/avif", metadata, flags);
    }


  avifDecoderDestroy (decoder);
  avifRWDataFree (&raw);
  g_free (filename);
  return image;
}
