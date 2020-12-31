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

#include "file-avif-dialog.h"

static void
save_dialog_min_quantizer_changed (GObject          *config,
                                   const GParamSpec *pspec,
                                   gpointer       user_data)
{
  double retval_max, retval_min;
  g_object_get (config, "max-quantizer", &retval_max,
                "min-quantizer", &retval_min,
                NULL);

  if (retval_min > retval_max)
    {
      g_object_set (config, "max-quantizer", retval_min, NULL);
    }

}

static void
save_dialog_max_quantizer_changed (GObject          *config,
                                   const GParamSpec *pspec,
                                   gpointer       user_data)
{
  double retval_max, retval_min;
  g_object_get (config, "max-quantizer", &retval_max,
                "min-quantizer", &retval_min,
                NULL);

  if (retval_max < retval_min)
    {
      g_object_set (config, "min-quantizer", retval_max, NULL);
    }
}

static GtkListStore *
avifplugin_create_codec_store (GObject       *config)
{
  avifCodecChoice codec_choice = AVIF_CODEC_CHOICE_AUTO;
  const char     *codec_aom;
  const char     *codec_rav1e;

  g_object_get (config, "av1-encoder", &codec_choice, NULL);

  codec_aom   = avifCodecName (AVIF_CODEC_CHOICE_AOM,   AVIF_CODEC_FLAG_CAN_ENCODE);
  codec_rav1e = avifCodecName (AVIF_CODEC_CHOICE_RAV1E, AVIF_CODEC_FLAG_CAN_ENCODE);

  if ( (! codec_aom && codec_choice == AVIF_CODEC_CHOICE_AOM) ||
       (! codec_rav1e && codec_choice == AVIF_CODEC_CHOICE_RAV1E))    /* if the selected encoder is not available, select auto */
    {
      codec_choice = AVIF_CODEC_CHOICE_AUTO;
      g_object_set (config, "av1-encoder", codec_choice, NULL);
    }

  if (codec_aom && codec_rav1e)   /* both encoders are available */
    {
      return gimp_int_store_new ("(auto)",    AVIF_CODEC_CHOICE_AUTO,
                                 codec_aom,   AVIF_CODEC_CHOICE_AOM,
                                 codec_rav1e, AVIF_CODEC_CHOICE_RAV1E,
                                 NULL);
    }
  else if (codec_aom && ! codec_rav1e)   /* only AOM is available */
    {
      return gimp_int_store_new ("(auto)",    AVIF_CODEC_CHOICE_AUTO,
                                 codec_aom,   AVIF_CODEC_CHOICE_AOM,
                                 NULL);
    }
  else if (! codec_aom && codec_rav1e)   /* only RAV1E is available */
    {
      return gimp_int_store_new ("(auto)",    AVIF_CODEC_CHOICE_AUTO,
                                 codec_rav1e, AVIF_CODEC_CHOICE_RAV1E,
                                 NULL);
    }

  /* no known encoders were detected */
  return gimp_int_store_new ("(auto)", AVIF_CODEC_CHOICE_AUTO,
                             NULL);
}

gboolean   save_dialog (GimpImage     *image,
                        GimpProcedure *procedure,
                        GObject       *config)
{
  GtkWidget     *dialog;
  GtkWidget     *vbox;
  GtkWidget     *grid;
  GtkWidget     *toggle;
  GtkListStore  *store;
  GtkWidget     *combo;

  GtkWidget *min_quantizer_scale;
  GtkWidget *max_quantizer_scale;
  GtkWidget *alpha_quantizer_scale;
  GtkWidget *speed_scale;

  gboolean       run;
  gint           row = 0;
  gint           save_bit_depth = 8;

  gint32         nlayers;
  GimpLayer    **layers = gimp_image_get_layers (image, &nlayers);

  /* UNUSED gboolean animation_supported = nlayers > 1; */
  gboolean       alpha_supported = FALSE;

  gint32 i;
  for (i = 0 ; i < nlayers; i++)
    {
      if (gimp_drawable_has_alpha (GIMP_DRAWABLE (layers[i])) ||
          gimp_layer_get_mask (layers[i]))
        {
          alpha_supported = TRUE;
          break;
        }
    }

  g_free (layers);


  dialog = gimp_procedure_dialog_new (procedure,
                                      GIMP_PROCEDURE_CONFIG (config),
                                      "Export Image as AVIF");

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                      vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
  gtk_box_pack_start (GTK_BOX (vbox), grid, FALSE, FALSE, 0);
  gtk_widget_show (grid);

  min_quantizer_scale =
    gimp_prop_scale_entry_new (config, "min-quantizer",
                               NULL, 1.0, FALSE, 0, 0);
  gtk_widget_hide (gimp_labeled_get_label (GIMP_LABELED (min_quantizer_scale)));
  gimp_grid_attach_aligned (GTK_GRID (grid), 0, row++,
                            "Quantizer (Min):",
                            0.0, 0.5, min_quantizer_scale, 2);

  g_signal_connect (config, "notify::min-quantizer",
                    G_CALLBACK (save_dialog_min_quantizer_changed),
                    NULL);

  max_quantizer_scale =
    gimp_prop_scale_entry_new (config, "max-quantizer",
                               NULL, 1.0, FALSE, 0, 0);
  gtk_widget_hide (gimp_labeled_get_label (GIMP_LABELED (max_quantizer_scale)));
  gimp_grid_attach_aligned (GTK_GRID (grid), 0, row++,
                            "Quantizer (Max):",
                            0.0, 0.5, max_quantizer_scale, 2);

  g_signal_connect (config, "notify::max-quantizer",
                    G_CALLBACK (save_dialog_max_quantizer_changed),
                    NULL);

  if (alpha_supported)
    {
      alpha_quantizer_scale =
        gimp_prop_scale_entry_new (config, "alpha-quantizer",
                                   NULL, 1.0, FALSE, 0, 0);
      gtk_widget_hide (gimp_labeled_get_label (GIMP_LABELED (alpha_quantizer_scale)));
      gimp_grid_attach_aligned (GTK_GRID (grid), 0, row++,
                                "Quantizer (Alpha):",
                                0.0, 0.5, alpha_quantizer_scale, 2);
    }

  /* Create the combobox containing the Pixel formats */
  store = gimp_int_store_new ("YUV444 (best quality)",    AVIF_PIXEL_FORMAT_YUV444,
                              "YUV422 (better quality)",  AVIF_PIXEL_FORMAT_YUV422,
                              "YUV420 (standard quality)", AVIF_PIXEL_FORMAT_YUV420,
                              "YUV400 (grayscale)",       AVIF_PIXEL_FORMAT_YUV400,
                              NULL);
  combo = gimp_prop_int_combo_box_new (config, "pixel-format",
                                       GIMP_INT_STORE (store));
  g_object_unref (store);

  /*GtkWidget     *label =*/
  gimp_grid_attach_aligned (GTK_GRID (grid), 0, row++,
                            "Pixel format:", 0.0, 0.5,
                            combo, 2);

  /* Create combobox with Bit depth of saved image */
  g_object_get (config,
                "save-bit-depth", &save_bit_depth,
                NULL);

  switch (gimp_image_get_precision (image))
    {
    case GIMP_PRECISION_U8_LINEAR:
    case GIMP_PRECISION_U8_NON_LINEAR:
    case GIMP_PRECISION_U8_PERCEPTUAL:
      /* image is 8bit depth */
      if (save_bit_depth > 8)
        {
          save_bit_depth = 8;
          g_object_set (config,
                        "save-bit-depth", save_bit_depth,
                        NULL);
        }
      break;
    default:
      /* high bit depth */
      if (save_bit_depth < 10)
        {
          save_bit_depth = 10; /* I prefer 10bit, because it is more supported than 12bit */
          g_object_set (config,
                        "save-bit-depth", save_bit_depth,
                        NULL);
        }
      break;
    }

  store = gimp_int_store_new ("8 bit/channel",         8,
                              "10 bit/channel", 10,
                              "12 bit/channel", 12,
                              NULL);

  combo = gimp_prop_int_combo_box_new (config, "save-bit-depth",
                                       GIMP_INT_STORE (store));
  g_object_unref (store);
  gimp_grid_attach_aligned (GTK_GRID (grid), 0, row++,
                            "Bit depth:", 0.0, 0.5,
                            combo, 2);

  /* Create combobox with available encoders */
  store = avifplugin_create_codec_store (config);

  combo = gimp_prop_int_combo_box_new (config, "av1-encoder",
                                       GIMP_INT_STORE (store));
  g_object_unref (store);

  gimp_grid_attach_aligned (GTK_GRID (grid), 0, row++,
                            "Encoder:", 0.0, 0.5,
                            combo, 2);

  speed_scale =
    gimp_prop_scale_entry_new (config, "encoder-speed",
                               NULL, 1.0, FALSE, 0, 0);
  gtk_widget_hide (gimp_labeled_get_label (GIMP_LABELED (speed_scale)));
  gimp_grid_attach_aligned (GTK_GRID (grid), 0, row++,
                            "Encoder speed:",
                            0.0, 0.5, speed_scale, 2);


  /* Save trasparency */
  if (alpha_supported)
    {
      toggle = gimp_prop_check_button_new (config, "save-alpha-channel",
                                           "Save Alpha channel");
      gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
    }
  else
    {
      g_object_set (config, "save-alpha-channel", FALSE, NULL);
    }

  /* Save EXIF data */
  toggle = gimp_prop_check_button_new (config, "save-exif",
                                       "Save Exif data");
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);

  /* XMP metadata */
  toggle = gimp_prop_check_button_new (config, "save-xmp",
                                       "Save XMP data");
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);

  /* Color profile */
  toggle = gimp_prop_check_button_new (config, "save-color-profile",
                                       "Save ICC color profile");
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);



  gtk_widget_show (dialog);

  run = gimp_procedure_dialog_run (GIMP_PROCEDURE_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return run;
}
