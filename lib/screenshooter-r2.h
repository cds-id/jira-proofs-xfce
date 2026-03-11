#ifndef __SCREENSHOOTER_R2_H__
#define __SCREENSHOOTER_R2_H__

#include <glib.h>
#include "screenshooter-cloud-config.h"

typedef void (*R2ProgressCallback) (gdouble fraction, gpointer user_data);

gchar *screenshooter_r2_upload (const CloudConfig *config,
                                 const gchar *file_path,
                                 R2ProgressCallback progress_cb,
                                 gpointer progress_data,
                                 GError **error);

gchar *screenshooter_r2_build_object_key (const gchar *filename);
const gchar *screenshooter_r2_content_type (const gchar *extension);

#endif
