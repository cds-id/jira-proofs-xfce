#include <stdio.h>
#include <glib.h>
#include "../lib/core/sc-video-editor-blur.h"

int
main (int argc, char **argv)
{
  const gchar *config_path = NULL;
  const gchar *video_path = NULL;
  const gchar *output_path = NULL;
  VideoEditorState *state;
  GError *error = NULL;
  gchar **ffargv;
  gchar *stdout_data = NULL;
  gchar *stderr_data = NULL;
  gint exit_status;

  if (argc < 3)
    {
      g_printerr ("Usage: %s <video.mp4> <config.json> [output.mp4]\n", argv[0]);
      return 1;
    }

  video_path = argv[1];
  config_path = argv[2];
  output_path = argc >= 4 ? argv[3] : NULL;

  /* Build default output path if not provided */
  gchar *default_output = NULL;
  if (output_path == NULL)
    {
      gchar *dir = g_path_get_dirname (video_path);
      gchar *base = g_path_get_basename (video_path);
      gchar *dot = g_strrstr (base, ".");
      if (dot)
        *dot = '\0';
      default_output = g_strdup_printf ("%s/%s_blurred.mp4", dir, base);
      output_path = default_output;
      g_free (dir);
      g_free (base);
    }

  /* Probe video */
  state = video_editor_state_new (video_path);
  if (!video_editor_probe_metadata (state, &error))
    {
      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);
      video_editor_state_free (state);
      g_free (default_output);
      return 1;
    }

  g_print ("Video: %dx%d, %.2fs, %.2f fps\n",
           state->video_width, state->video_height,
           state->duration, state->fps);

  /* Load blur config */
  if (!video_editor_load_config (state, config_path, &error))
    {
      g_printerr ("Config error: %s\n", error->message);
      g_error_free (error);
      video_editor_state_free (state);
      g_free (default_output);
      return 1;
    }

  g_print ("Loaded %u regions, blur radius %d\n",
           g_list_length (state->regions), state->blur_radius);

  /* Build and run ffmpeg */
  ffargv = video_editor_build_ffmpeg_argv (state, output_path);
  if (ffargv == NULL)
    {
      g_printerr ("Error: failed to build ffmpeg command\n");
      video_editor_state_free (state);
      g_free (default_output);
      return 1;
    }

  g_print ("Rendering to: %s\n", output_path);

  /* Print command */
  g_print ("Running: ");
  for (gchar **p = ffargv; *p != NULL; p++)
    g_print ("%s ", *p);
  g_print ("\n\n");

  gboolean ok = g_spawn_sync (NULL, ffargv, NULL,
                               G_SPAWN_CHILD_INHERITS_STDIN,
                               NULL, NULL,
                               &stdout_data, &stderr_data,
                               &exit_status, &error);

  video_editor_free_argv (ffargv);
  video_editor_state_free (state);
  g_free (default_output);

  if (!ok)
    {
      g_printerr ("Spawn error: %s\n", error->message);
      g_error_free (error);
      return 1;
    }

  if (stderr_data && *stderr_data)
    g_printerr ("%s", stderr_data);

  g_free (stdout_data);
  g_free (stderr_data);

  if (exit_status != 0)
    {
      g_printerr ("\nFFmpeg exited with code %d\n", exit_status);
      return 1;
    }

  g_print ("\nDone! Output: %s\n", output_path);
  return 0;
}
