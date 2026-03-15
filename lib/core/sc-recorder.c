#include "sc-recorder.h"

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <time.h>
#include <glib/gstdio.h>

#ifndef G_OS_WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

static RecorderState *active_recorder = NULL;


gboolean
sc_recorder_available (void)
{
  gchar *path = g_find_program_in_path ("ffmpeg");
  if (path)
    {
      g_free (path);
      return TRUE;
    }
  return FALSE;
}


#ifndef G_OS_WIN32
static gchar *
get_display_string (void)
{
  const gchar *display = g_getenv ("DISPLAY");
  if (display == NULL || display[0] == '\0')
    return g_strdup (":0.0");

  /* Strip hostname prefix: "localhost:10.0" -> ":10.0" */
  const gchar *colon = strchr (display, ':');
  if (colon && colon != display)
    return g_strdup (colon);

  return g_strdup (display);
}
#endif


static gchar *
build_output_path (void)
{
  time_t now = time (NULL);
  struct tm *tm = localtime (&now);
  gchar timestamp[64];
  strftime (timestamp, sizeof (timestamp), "%Y-%m-%d_%H-%M-%S", tm);
  return g_strdup_printf ("%s/recording-%s.mp4", g_get_tmp_dir (), timestamp);
}


RecorderState *
sc_recorder_start (gint region, gint x, gint y, gint w, gint h,
                   GError **error)
{
#ifdef G_OS_WIN32
  /* TODO: Windows recording via gdigrab — needs platform layer integration */
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Recording is not yet supported on Windows");
  return NULL;
#else
  RecorderState *state;
  gchar *display_str;
  gchar *input_str;
  gchar *video_size;
  gchar *ffmpeg_path;
  GPid pid;
  gboolean spawned;

  ffmpeg_path = g_find_program_in_path ("ffmpeg");
  if (ffmpeg_path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "FFmpeg is required for video recording. "
                   "Install with: sudo apt install ffmpeg");
      return NULL;
    }

  display_str = get_display_string ();
  input_str = g_strdup_printf ("%s+%d,%d", display_str, x, y);
  video_size = g_strdup_printf ("%dx%d", w, h);

  state = g_new0 (RecorderState, 1);
  state->output_path = build_output_path ();
  state->region = region;
  state->x = x;
  state->y = y;
  state->w = w;
  state->h = h;
  state->stopped = FALSE;

  /* TODO: move platform-specific args to platform layer */
  gchar *argv[] = {
    ffmpeg_path,
    "-y",
    "-f", "x11grab",
    "-framerate", "30",
    "-video_size", video_size,
    "-i", input_str,
    "-c:v", "libx264",
    "-preset", "ultrafast",
    "-crf", "23",
    "-pix_fmt", "yuv420p",
    state->output_path,
    NULL
  };

  spawned = g_spawn_async (NULL, argv, NULL,
                            G_SPAWN_DO_NOT_REAP_CHILD,
                            NULL, NULL, &pid, error);

  g_free (ffmpeg_path);
  g_free (display_str);
  g_free (input_str);
  g_free (video_size);

  if (!spawned)
    {
      sc_recorder_free (state);
      return NULL;
    }

  state->pid = pid;
  active_recorder = state;
  return state;
#endif
}


gchar *
sc_recorder_stop (RecorderState *state, GError **error)
{
  gchar *result;

  if (state == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Recorder state is NULL");
      return NULL;
    }

  if (state->stopped)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Recording already stopped");
      return NULL;
    }

  state->stopped = TRUE;

#ifndef G_OS_WIN32
  {
    gint status;

    /* Send SIGINT for clean shutdown (writes MP4 trailer) */
    kill (state->pid, SIGINT);

    /* Remove child watch before waitpid to prevent race condition */
    if (state->child_watch_id > 0)
      {
        g_source_remove (state->child_watch_id);
        state->child_watch_id = 0;
      }

    /* Wait for ffmpeg to finish */
    waitpid (state->pid, &status, 0);
  }
#endif

  g_spawn_close_pid (state->pid);

  if (!g_file_test (state->output_path, G_FILE_TEST_EXISTS))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Recording file was not created");
      return NULL;
    }

  result = g_strdup (state->output_path);
  return result;
}


void
sc_recorder_free (RecorderState *state)
{
  if (state == NULL)
    return;

  if (active_recorder == state)
    active_recorder = NULL;

  g_free (state->output_path);
  g_free (state);
}


void
sc_recorder_cleanup (void)
{
  if (active_recorder == NULL || active_recorder->stopped)
    return;

  active_recorder->stopped = TRUE;

#ifndef G_OS_WIN32
  kill (active_recorder->pid, SIGINT);
  waitpid (active_recorder->pid, NULL, 0);
#endif

  if (active_recorder->output_path)
    g_unlink (active_recorder->output_path);
}
