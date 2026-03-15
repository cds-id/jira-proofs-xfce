#ifndef __HAVE_VIDEO_EDITOR_H__
#define __HAVE_VIDEO_EDITOR_H__

#include <gtk/gtk.h>

gboolean screenshooter_video_editor_available (void);
void     screenshooter_video_editor_run       (GtkWindow *parent);
void     screenshooter_video_editor_run_with_file (const gchar *filepath);

#endif
