#ifndef __HAVE_RECORDER_DIALOG_H__
#define __HAVE_RECORDER_DIALOG_H__

#include <sc-recorder.h>
#include <gtk/gtk.h>

typedef void (*RecorderStopCallback) (const gchar *output_path,
                                       gpointer     user_data);

void screenshooter_recorder_dialog_run (RecorderState        *state,
                                         RecorderStopCallback  callback,
                                         gpointer              user_data);

#endif
