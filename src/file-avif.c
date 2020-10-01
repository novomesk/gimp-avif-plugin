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
#include "file-avif-save.h"
#include "file-avif-load.h"


#define LOAD_PROC      "file-avif-load"
#define SAVE_PROC      "file-avif-save"
#define PLUG_IN_BINARY "file-avif"
#define PLUG_IN_ROLE   "gimp-file-avif"

typedef struct _Avif      Avif;
typedef struct _AvifClass AvifClass;

struct _Avif
{
  GimpPlugIn      parent_instance;
};

struct _AvifClass
{
  GimpPlugInClass parent_class;
};


#define AVIF_TYPE  (avif_get_type ())
#define AVIF (obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), AVIF_TYPE, Avif))

GType                   avif_get_type (void) G_GNUC_CONST;

static GList           *avif_query_procedures (GimpPlugIn           *plug_in);
static GimpProcedure   *avif_create_procedure (GimpPlugIn           *plug_in,
    const gchar          *name);

static GimpValueArray *avif_load (GimpProcedure        *procedure,
                                  GimpRunMode           run_mode,
                                  GFile                *file,
                                  const GimpValueArray *args,
                                  gpointer              run_data);
static GimpValueArray *avif_save (GimpProcedure        *procedure,
                                  GimpRunMode           run_mode,
                                  GimpImage            *image,
                                  gint                  n_drawables,
                                  GimpDrawable        **drawables,
                                  GFile                *file,
                                  const GimpValueArray *args,
                                  gpointer              run_data);


G_DEFINE_TYPE (Avif, avif, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (AVIF_TYPE)


static void
avif_class_init (AvifClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  plug_in_class->query_procedures = avif_query_procedures;
  plug_in_class->create_procedure = avif_create_procedure;
}

static void
avif_init (Avif *avif)
{
}

static GList *
avif_query_procedures (GimpPlugIn *plug_in)
{
  GList *list = NULL;

  list = g_list_append (list, g_strdup (LOAD_PROC));
  list = g_list_append (list, g_strdup (SAVE_PROC));

  return list;
}

static GimpProcedure *
avif_create_procedure (GimpPlugIn  *plug_in,
                       const gchar *name)
{
  GimpProcedure *procedure = NULL;

  if (! strcmp (name, LOAD_PROC))
    {
      procedure = gimp_load_procedure_new (plug_in, name,
                                           GIMP_PDB_PROC_TYPE_PLUGIN,
                                           avif_load, NULL, NULL);

      gimp_procedure_set_menu_label (procedure, "AVIF image");

      gimp_procedure_set_documentation (procedure,
                                        "Loads images in the AVIF file format",
                                        "Loads images in the AVIF file format",
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      "Daniel Novomesky",
                                      "(C) 2020 Daniel Novomesky",
                                      "2020");

      gimp_file_procedure_set_mime_types (GIMP_FILE_PROCEDURE (procedure),
                                          "image/avif");
      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "avif,avifs");
      gimp_file_procedure_set_magics (GIMP_FILE_PROCEDURE (procedure),
                                      "4,string,ftypavif,4,string,ftypavis");
    }
  else if (! strcmp (name, SAVE_PROC))
    {
      procedure = gimp_save_procedure_new (plug_in, name,
                                           GIMP_PDB_PROC_TYPE_PLUGIN,
                                           avif_save, NULL, NULL);

      gimp_procedure_set_image_types (procedure, "*");

      gimp_procedure_set_menu_label (procedure, "AVIF image");

      gimp_procedure_set_documentation (procedure,
                                        "Saves files in the AVIF image format",
                                        "Saves files in the AVIF image format",
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      "Daniel Novomesky",
                                      "(C) 2020 Daniel Novomesky",
                                      "2020");

      gimp_file_procedure_set_mime_types (GIMP_FILE_PROCEDURE (procedure),
                                          "image/avif");
      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "avif");

      GIMP_PROC_ARG_DOUBLE (procedure, "min-quantizer",
                            "Quantizer (Min)",
                            "Set higher values to limit/reduce image quality",
                            AVIF_QUANTIZER_BEST_QUALITY, AVIF_QUANTIZER_WORST_QUALITY, AVIF_QUANTIZER_BEST_QUALITY,
                            G_PARAM_READWRITE);

      GIMP_PROC_ARG_DOUBLE (procedure, "max-quantizer",
                            "Quantizer (Max)",
                            "AVIF quality parameter: 0 - highest quality, 63 - smallest file",
                            AVIF_QUANTIZER_BEST_QUALITY, AVIF_QUANTIZER_WORST_QUALITY, 40,
                            G_PARAM_READWRITE);

      GIMP_PROC_ARG_DOUBLE (procedure, "alpha-quantizer",
                            "Quantizer (Alpha)",
                            "AVIF quality parameter: 0 - highest quality (recommended!) , 63 - smallest file",
                            AVIF_QUANTIZER_BEST_QUALITY, AVIF_QUANTIZER_WORST_QUALITY, AVIF_QUANTIZER_BEST_QUALITY,
                            G_PARAM_READWRITE);

      GIMP_PROC_ARG_INT (procedure, "pixel-format",
                         "Pixel Format",
                         "YUV444 (needs lot of RAM), YUV422, YUV420, Grayscale",
                         AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV400,
                         AVIF_PIXEL_FORMAT_YUV420,
                         G_PARAM_READWRITE);

      GIMP_PROC_ARG_INT (procedure, "save-bit-depth",
                         "Bit depth",
                         "Bit depth of exported image",
                         8, 12, 8,
                         G_PARAM_READWRITE);

      GIMP_PROC_ARG_INT (procedure, "av1-encoder",
                         "AV1 encoder",
                         "Select encoder for AV1 stream",
                         AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_CHOICE_RAV1E, AVIF_CODEC_CHOICE_AUTO,
                         G_PARAM_READWRITE);

      GIMP_PROC_ARG_DOUBLE (procedure, "encoder-speed",
                            "Encoder speed",
                            "Speed of export: 0 - very slow, 5 - medium, 10 - fastest",
                            AVIF_SPEED_SLOWEST, AVIF_SPEED_FASTEST, 6, /* speed 6 is default for rav1e */
                            G_PARAM_READWRITE);

      GIMP_PROC_ARG_BOOLEAN (procedure, "save-alpha-channel",
                             "Save Alpha channel",
                             "Save information about transparent pixels when possible",
                             TRUE,
                             G_PARAM_READWRITE);

      GIMP_PROC_ARG_BOOLEAN (procedure, "animation",
                             "Animation",
                             "Use layers for animation",
                             FALSE,
                             G_PARAM_READWRITE);

      GIMP_PROC_ARG_BOOLEAN (procedure, "save-color-profile",
                             "Save color profle",
                             "Enable to save ICC color profile, disable to save NCLX information",
                             FALSE,
                             G_PARAM_READWRITE);

      GIMP_PROC_ARG_BOOLEAN (procedure, "save-exif",
                             "Save Exif",
                             "Toggle saving Exif data",
                             gimp_export_exif (),
                             G_PARAM_READWRITE);

      GIMP_PROC_ARG_BOOLEAN (procedure, "save-xmp",
                             "Save XMP",
                             "Toggle saving XMP data",
                             gimp_export_xmp (),
                             G_PARAM_READWRITE);

    }

  return procedure;
}

static GimpValueArray *
avif_load (GimpProcedure        *procedure,
           GimpRunMode           run_mode,
           GFile                *file,
           const GimpValueArray *args,
           gpointer              run_data)
{
  GimpValueArray *return_vals;
  GimpImage      *image;
  GError         *error = NULL;


  gegl_init (NULL, NULL);

  image = load_image (file, FALSE, &error);

  if (! image)
    return gimp_procedure_new_return_values (procedure,
           GIMP_PDB_EXECUTION_ERROR,
           error);

  return_vals = gimp_procedure_new_return_values (procedure,
                GIMP_PDB_SUCCESS,
                NULL);

  GIMP_VALUES_SET_IMAGE (return_vals, 1, image);

  return return_vals;
}

static GimpValueArray *
avif_save (GimpProcedure        *procedure,
           GimpRunMode           run_mode,
           GimpImage            *image,
           gint                  n_drawables,
           GimpDrawable        **drawables,
           GFile                *file,
           const GimpValueArray *args,
           gpointer              run_data)
{
  GimpProcedureConfig *config;
  GimpPDBStatusType    status = GIMP_PDB_SUCCESS;
  GimpExportReturn     export = GIMP_EXPORT_CANCEL;
  GimpMetadata        *metadata;
  gboolean             animation;
  gboolean             save_alpha_channel;
  GError              *error  = NULL;
  avifPixelFormat      pixel_format = AVIF_PIXEL_FORMAT_YUV420;


  gegl_init (NULL, NULL);

  config = gimp_procedure_create_config (procedure);
  metadata = gimp_procedure_config_begin_export (config, image, run_mode,
             args, "image/avif");


  if (run_mode == GIMP_RUN_INTERACTIVE ||
      run_mode == GIMP_RUN_WITH_LAST_VALS)
    gimp_ui_init (PLUG_IN_BINARY);

  if (run_mode == GIMP_RUN_INTERACTIVE)
    {
      if (! save_dialog (image, procedure, G_OBJECT (config)))
        return gimp_procedure_new_return_values (procedure,
               GIMP_PDB_CANCEL,
               NULL);
    }

  g_object_get (config,
                "animation", &animation,
                "save-alpha-channel", &save_alpha_channel,
                "pixel-format", &pixel_format,
                NULL);

  if (run_mode == GIMP_RUN_INTERACTIVE ||
      run_mode == GIMP_RUN_WITH_LAST_VALS)
    {
      GimpExportCapabilities capabilities;

      if (pixel_format == AVIF_PIXEL_FORMAT_YUV400)
        {
          capabilities = GIMP_EXPORT_CAN_HANDLE_GRAY;
        }
      else
        {
          capabilities = (GIMP_EXPORT_CAN_HANDLE_RGB | GIMP_EXPORT_CAN_HANDLE_GRAY);
        }

      if (animation)
        capabilities |= GIMP_EXPORT_CAN_HANDLE_LAYERS_AS_ANIMATION;

      if (save_alpha_channel)
        capabilities |= GIMP_EXPORT_CAN_HANDLE_ALPHA;

      export = gimp_export_image (&image, &n_drawables, &drawables, "AVIF",
                                  capabilities);

      if (export == GIMP_EXPORT_CANCEL)
        return gimp_procedure_new_return_values (procedure,
               GIMP_PDB_CANCEL,
               NULL);
    }

  if (animation)
    {
      if (! save_animation (file, image, n_drawables, drawables, G_OBJECT (config), metadata,
                            &error))
        {
          status = GIMP_PDB_EXECUTION_ERROR;
        }
    }
  else
    {
      if (n_drawables != 1)
        {
          g_set_error (&error, G_FILE_ERROR, 0,
                       "The AVIF plug-in cannot export multiple layer, except in animation mode.");

          return gimp_procedure_new_return_values (procedure,
                 GIMP_PDB_CALLING_ERROR,
                 error);
        }

      if (! save_layer (file, image, drawables[0], G_OBJECT (config), metadata,
                        &error))
        {
          status = GIMP_PDB_EXECUTION_ERROR;
        }
    }


  gimp_procedure_config_end_export (config, image, file, status);
  g_object_unref (config);

  if (export == GIMP_EXPORT_EXPORT)
    gimp_image_delete (image);

  return gimp_procedure_new_return_values (procedure, status, error);
}
