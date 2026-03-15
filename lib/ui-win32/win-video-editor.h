#ifndef __WIN_VIDEO_EDITOR_H__
#define __WIN_VIDEO_EDITOR_H__

#include <glib.h>

/* Launch the video editor for the given file (or file-open dialog if NULL) */
void win_video_editor_run (const gchar *filepath);

/* Returns TRUE if ffmpeg/ffprobe are available */
gboolean win_video_editor_available (void);

#endif
