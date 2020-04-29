
#include <exiv2/exiv2.hpp>
#include <gexiv2/gexiv2.h>

#include "file-avif-exif.h"


/* Fragment from gexiv2-metadata-private.h */
G_BEGIN_DECLS

struct _GExiv2MetadataPrivate {
#if EXIV2_TEST_VERSION(0,27,99)
    Exiv2::Image::UniquePtr image;
#else
    Exiv2::Image::AutoPtr image;
#endif
    gchar* comment;
    gchar* mime_type;
    gint pixel_width;
    gint pixel_height;
    gboolean supports_exif;
    gboolean supports_xmp;
    gboolean supports_iptc;
    Exiv2::PreviewManager *preview_manager;
    GExiv2PreviewProperties **preview_properties;
};

G_END_DECLS
/* End of gexiv2-metadata-private.h fragment */

guchar * get_TIFF_Exif_raw_data ( GExiv2Metadata * metadata_source,
                                  size_t  * out_size )
{
    Exiv2::Blob blob;

    Exiv2::ExifData exif_data = metadata_source->priv->image->exifData();
    Exiv2::ExifParser::encode ( blob, Exiv2::littleEndian, exif_data );
    
    size_t encoded_size = blob.size();
    if ( encoded_size > 0 )
      {
        guchar *outptr = g_new ( guchar, encoded_size);
        if ( ! outptr ) return NULL;
        
        memcpy ( outptr, blob.data(), encoded_size );
        *out_size = encoded_size;
        return outptr;
      }
    *out_size = 0;
    return NULL;
}
