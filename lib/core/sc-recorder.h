#ifndef __SC_RECORDER_H__
#define __SC_RECORDER_H__

#include <glib.h>

typedef struct {
  GPid    pid;
  gchar  *output_path;
  gint    region;
  gint    x, y, w, h;
  guint   child_watch_id;
  gboolean stopped;
} RecorderState;

gboolean       sc_recorder_available (void);

RecorderState *sc_recorder_start     (gint     region,
                                      gint     x,
                                      gint     y,
                                      gint     w,
                                      gint     h,
                                      GError **error);

gchar         *sc_recorder_stop      (RecorderState  *state,
                                      GError        **error);

void           sc_recorder_free      (RecorderState *state);

void           sc_recorder_cleanup   (void);

#endif
