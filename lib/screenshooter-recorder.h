#ifndef __HAVE_RECORDER_H__
#define __HAVE_RECORDER_H__

#include <glib.h>

typedef struct {
  GPid    pid;
  gchar  *output_path;
  gint    region;
  gint    x, y, w, h;
  guint   child_watch_id;
  gboolean stopped;
} RecorderState;

gboolean       screenshooter_recorder_available (void);

RecorderState *screenshooter_recorder_start     (gint     region,
                                                  gint     x,
                                                  gint     y,
                                                  gint     w,
                                                  gint     h,
                                                  GError **error);

gchar         *screenshooter_recorder_stop      (RecorderState  *state,
                                                  GError        **error);

void           screenshooter_recorder_free      (RecorderState *state);

void           screenshooter_recorder_cleanup   (void);

#endif
