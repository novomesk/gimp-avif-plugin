

#ifndef __AVIF_SAVE_H__
#define __AVIF_SAVE_H__


gboolean   save_layer     (GFile         *file,
                           GimpImage     *image,
                           GimpDrawable  *drawable,
                           GObject       *config,
                           GimpMetadata  *metadata,
                           GError       **error);

gboolean   save_animation (GFile         *file,
                           GimpImage     *image,
                           GimpDrawable  *drawable,
                           GObject       *config,
                           GimpMetadata  *metadata,
                           GError       **error);


#endif /* __AVIF_SAVE_H__ */
