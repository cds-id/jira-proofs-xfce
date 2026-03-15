#include "win-video-editor.h"
#include <sc-recorder.h>
#include <sc-video-editor-blur.h>

#ifdef G_OS_WIN32

#include <windows.h>
#include <commdlg.h>


gboolean
win_video_editor_available (void)
{
  return sc_recorder_available ();
}


void
win_video_editor_run (const gchar *filepath)
{
  gchar *path = NULL;

  if (filepath == NULL)
    {
      /* Show file-open dialog */
      OPENFILENAMEA ofn;
      char szFile[MAX_PATH] = { 0 };

      memset (&ofn, 0, sizeof (ofn));
      ofn.lStructSize = sizeof (ofn);
      ofn.hwndOwner = NULL;
      ofn.lpstrFilter = "MP4 Video Files (*.mp4)\0*.mp4\0All Files (*.*)\0*.*\0";
      ofn.lpstrFile = szFile;
      ofn.nMaxFile = sizeof (szFile);
      ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
      ofn.lpstrTitle = "Select Video File";

      if (!GetOpenFileNameA (&ofn))
        return; /* cancelled */

      path = g_strdup (szFile);
    }
  else
    {
      path = g_strdup (filepath);
    }

  /* Probe metadata */
  VideoEditorState *state = video_editor_state_new (path);
  GError *err = NULL;

  if (video_editor_probe_metadata (state, &err))
    {
      gchar *info = g_strdup_printf (
          "Video: %s\n"
          "Resolution: %dx%d\n"
          "Duration: %.1f seconds\n"
          "FPS: %.1f\n"
          "Audio: %s\n\n"
          "Video editor UI coming soon.\n"
          "Use --help for CLI blur options.",
          path,
          state->video_width, state->video_height,
          state->duration, state->fps,
          state->has_audio ? "yes" : "no");

      MessageBoxA (NULL, info, "Video Editor", MB_OK | MB_ICONINFORMATION);
      g_free (info);
    }
  else
    {
      gchar *msg = g_strdup_printf ("Failed to probe video: %s",
          err ? err->message : "unknown error");
      MessageBoxA (NULL, msg, "Video Editor", MB_OK | MB_ICONERROR);
      g_free (msg);
      g_clear_error (&err);
    }

  video_editor_state_free (state);
  g_free (path);
}

#else /* !G_OS_WIN32 */

gboolean
win_video_editor_available (void)
{
  g_warning ("win_video_editor_available() called on non-Windows platform");
  return FALSE;
}

void
win_video_editor_run (const gchar *filepath)
{
  (void) filepath;
  g_warning ("win_video_editor_run() called on non-Windows platform");
}

#endif /* G_OS_WIN32 */
