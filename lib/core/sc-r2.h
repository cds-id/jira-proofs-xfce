#ifndef __SC_R2_H__
#define __SC_R2_H__

#include <glib.h>
#include "sc-cloud-config.h"

typedef void (*R2ProgressCallback) (gdouble fraction, gpointer user_data);

gchar    *sc_r2_upload            (const CloudConfig *config, const gchar *file_path, R2ProgressCallback progress_cb, gpointer progress_data, GError **error);
gboolean  sc_r2_test_connection   (const CloudConfig *config, GError **error);
gchar    *sc_r2_build_object_key  (const gchar *filename);
const gchar *sc_r2_content_type   (const gchar *extension);

#endif
