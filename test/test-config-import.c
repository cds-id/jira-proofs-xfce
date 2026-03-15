#include <stdio.h>
#include <glib.h>
#include "../lib/core/sc-video-editor-blur.h"

int
main (int argc, char **argv)
{
  const gchar *config_path;
  const gchar *video_path;
  VideoEditorState *state;
  GError *error = NULL;

  if (argc < 2)
    {
      g_printerr ("Usage: %s <config.json> [video.mp4]\n", argv[0]);
      return 1;
    }

  config_path = argv[1];
  video_path = argc >= 3 ? argv[2] : NULL;

  /* Create state (video path optional for config test) */
  state = video_editor_state_new (video_path ? video_path : "dummy.mp4");

  /* If video provided, probe metadata */
  if (video_path)
    {
      if (!video_editor_probe_metadata (state, &error))
        {
          g_printerr ("Probe failed: %s\n", error->message);
          g_error_free (error);
          video_editor_state_free (state);
          return 1;
        }
      g_print ("Video: %dx%d, %.2fs, %.2f fps, audio=%s\n",
               state->video_width, state->video_height,
               state->duration, state->fps,
               state->has_audio ? "yes" : "no");
    }

  /* Load config */
  g_print ("Loading config: %s\n", config_path);
  if (!video_editor_load_config (state, config_path, &error))
    {
      g_printerr ("Load failed: %s\n", error->message);
      g_error_free (error);
      video_editor_state_free (state);
      return 1;
    }

  g_print ("Blur radius: %d\n", state->blur_radius);
  g_print ("Regions: %u\n", g_list_length (state->regions));

  guint idx = 0;
  for (GList *l = state->regions; l != NULL; l = l->next, idx++)
    {
      BlurRegion *r = l->data;
      if (r->full_frame)
        g_print ("  #%u [Full Frame] %.1fs -> %.1fs\n",
                 idx + 1, r->start_time, r->end_time);
      else
        g_print ("  #%u [%d,%d %dx%d] %.1fs -> %.1fs\n",
                 idx + 1, r->x, r->y, r->w, r->h,
                 r->start_time, r->end_time);
    }

  /* Test save to temp file */
  gchar *tmp = g_build_filename (g_get_tmp_dir (), "test-blur-export.json", NULL);
  g_print ("\nSaving config to: %s\n", tmp);
  if (!video_editor_save_config (state, tmp, &error))
    {
      g_printerr ("Save failed: %s\n", error->message);
      g_error_free (error);
      g_free (tmp);
      video_editor_state_free (state);
      return 1;
    }
  g_print ("Save OK\n");

  /* Build filter chain if video was probed */
  if (video_path)
    {
      gchar *filter = video_editor_build_filter_chain (state);
      if (filter)
        {
          g_print ("\nFilter chain:\n%s\n", filter);
          g_free (filter);
        }

      gchar **ffargv = video_editor_build_ffmpeg_argv (state, "/tmp/test-output.mp4");
      if (ffargv)
        {
          g_print ("\nFFmpeg command:\n");
          for (gchar **p = ffargv; *p != NULL; p++)
            g_print ("  %s\n", *p);
          video_editor_free_argv (ffargv);
        }
    }

  g_free (tmp);
  video_editor_state_free (state);
  g_print ("\nAll OK\n");
  return 0;
}
