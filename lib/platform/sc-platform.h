#ifndef __SC_PLATFORM_H__
#define __SC_PLATFORM_H__

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef enum {
    SC_CAPTURE_FULLSCREEN,
    SC_CAPTURE_WINDOW,
    SC_CAPTURE_REGION
} ScCaptureMode;

typedef struct {
    gint x, y, width, height;
} ScRegion;

GdkPixbuf *sc_platform_capture         (ScCaptureMode mode, ScRegion *region);
gboolean   sc_platform_select_region   (ScRegion *out_region);
gchar    **sc_platform_recorder_args   (ScCaptureMode mode, ScRegion *region);
void       sc_platform_recorder_args_free (gchar **args);
gchar     *sc_platform_config_dir      (void);
gboolean   sc_platform_clipboard_copy_image (GdkPixbuf *pixbuf);
void       sc_platform_notify          (const gchar *title, const gchar *body);
void       sc_platform_restrict_file   (const gchar *path);

#endif
