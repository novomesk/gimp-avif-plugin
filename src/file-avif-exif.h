
#ifndef __AVIF_EXIF_H__
#define __AVIF_EXIF_H__

/*
 * Returns data you can you can pass to avifImageSetMetadataExif procedure
 * You must free it with g_free () afterwards
 * out_size will contain length of the returned data.
 */

G_BEGIN_DECLS

guchar * get_TIFF_Exif_raw_data ( GExiv2Metadata * metadata_source,
                                  size_t  * out_size);

G_END_DECLS

#endif /* __AVIF_EXIF_H__ */
