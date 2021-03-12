

#ifndef __AVIF_SAVE_H__
#define __AVIF_SAVE_H__


gboolean   save_layers (GFile         *file,
                        GimpImage     *image,
                        gint           n_drawables,
                        GimpDrawable **drawables,
                        GObject       *config,
                        GimpMetadata  *metadata,
                        GError       **error);

#endif /* __AVIF_SAVE_H__ */
