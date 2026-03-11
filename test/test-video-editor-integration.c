#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include "../lib/screenshooter-video-editor-blur.h"


static gchar *
generate_test_video (gboolean with_audio, GError **error)
{
  gchar *ffmpeg_path = g_find_program_in_path ("ffmpeg");
  if (ffmpeg_path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "ffmpeg not found");
      return NULL;
    }

  gchar *output = g_build_filename (g_get_tmp_dir (),
                                    with_audio ? "xfce-test-with-audio.mp4"
                                               : "xfce-test-no-audio.mp4",
                                    NULL);

  GPtrArray *args = g_ptr_array_new ();
  g_ptr_array_add (args, ffmpeg_path);
  g_ptr_array_add (args, "-y");
  g_ptr_array_add (args, "-f");
  g_ptr_array_add (args, "lavfi");
  g_ptr_array_add (args, "-i");
  g_ptr_array_add (args, "testsrc=duration=3:size=320x240:rate=30");

  if (with_audio)
    {
      g_ptr_array_add (args, "-f");
      g_ptr_array_add (args, "lavfi");
      g_ptr_array_add (args, "-i");
      g_ptr_array_add (args, "sine=frequency=440:duration=3");
    }

  g_ptr_array_add (args, "-c:v");
  g_ptr_array_add (args, "libx264");
  g_ptr_array_add (args, "-preset");
  g_ptr_array_add (args, "ultrafast");
  g_ptr_array_add (args, "-pix_fmt");
  g_ptr_array_add (args, "yuv420p");

  if (with_audio)
    {
      g_ptr_array_add (args, "-c:a");
      g_ptr_array_add (args, "aac");
      g_ptr_array_add (args, "-shortest");
    }

  g_ptr_array_add (args, output);
  g_ptr_array_add (args, NULL);

  gint exit_status;
  gboolean ok = g_spawn_sync (NULL,
    (gchar **) args->pdata, NULL,
    G_SPAWN_DEFAULT,
    NULL, NULL, NULL, NULL,
    &exit_status, error);

  g_free (ffmpeg_path);
  g_ptr_array_free (args, TRUE);

  if (!ok || exit_status != 0)
    {
      if (ok && *error == NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "ffmpeg exited with status %d", exit_status);
      g_free (output);
      return NULL;
    }

  return output;
}


static void
test_probe_with_audio (void)
{
  GError *error = NULL;
  gchar *video = generate_test_video (TRUE, &error);
  if (video == NULL)
    {
      g_test_skip (error->message);
      g_error_free (error);
      return;
    }

  VideoEditorState *state = video_editor_state_new (video);
  g_assert_true (video_editor_probe_metadata (state, &error));
  g_assert_no_error (error);

  g_assert_cmpint (state->video_width, ==, 320);
  g_assert_cmpint (state->video_height, ==, 240);
  g_assert_true (state->duration > 2.5 && state->duration < 3.5);
  g_assert_true (state->has_audio);
  g_assert_true (state->fps > 29 && state->fps < 31);

  video_editor_state_free (state);
  g_unlink (video);
  g_free (video);
}


static void
test_probe_without_audio (void)
{
  GError *error = NULL;
  gchar *video = generate_test_video (FALSE, &error);
  if (video == NULL)
    {
      g_test_skip (error->message);
      g_error_free (error);
      return;
    }

  VideoEditorState *state = video_editor_state_new (video);
  g_assert_true (video_editor_probe_metadata (state, &error));
  g_assert_no_error (error);

  g_assert_cmpint (state->video_width, ==, 320);
  g_assert_cmpint (state->video_height, ==, 240);
  g_assert_false (state->has_audio);

  video_editor_state_free (state);
  g_unlink (video);
  g_free (video);
}


static void
test_apply_blur_and_verify (void)
{
  GError *error = NULL;
  gchar *video = generate_test_video (TRUE, &error);
  if (video == NULL)
    {
      g_test_skip (error->message);
      g_error_free (error);
      return;
    }

  VideoEditorState *state = video_editor_state_new (video);
  g_assert_true (video_editor_probe_metadata (state, &error));

  /* Add a blur region */
  BlurRegion *r = blur_region_new (50, 50, 100, 80, 0.5, 2.5, FALSE);
  video_editor_add_region (state, r);

  gchar *output = g_build_filename (g_get_tmp_dir (),
                                    "xfce-test-blurred.mp4", NULL);
  gchar **argv = video_editor_build_ffmpeg_argv (state, output);
  g_assert_nonnull (argv);

  /* Run FFmpeg */
  gint exit_status;
  gchar *stderr_out = NULL;
  gboolean ok = g_spawn_sync (NULL, argv, NULL,
    G_SPAWN_DEFAULT,
    NULL, NULL, NULL, &stderr_out,
    &exit_status, &error);

  g_assert_true (ok);
  if (exit_status != 0)
    g_test_message ("FFmpeg stderr: %s", stderr_out ? stderr_out : "(null)");
  g_assert_cmpint (exit_status, ==, 0);
  g_free (stderr_out);

  /* Verify output exists and is valid */
  g_assert_true (g_file_test (output, G_FILE_TEST_EXISTS));

  /* Probe the output to verify it's valid */
  VideoEditorState *out_state = video_editor_state_new (output);
  g_assert_true (video_editor_probe_metadata (out_state, &error));
  g_assert_no_error (error);

  /* Resolution should be preserved */
  g_assert_cmpint (out_state->video_width, ==, 320);
  g_assert_cmpint (out_state->video_height, ==, 240);
  /* Duration should be approximately preserved */
  g_assert_true (out_state->duration > 2.0 && out_state->duration < 4.0);

  video_editor_state_free (out_state);
  video_editor_free_argv (argv);
  video_editor_state_free (state);
  g_unlink (output);
  g_unlink (video);
  g_free (output);
  g_free (video);
}


static void
test_apply_fullframe_blur (void)
{
  GError *error = NULL;
  gchar *video = generate_test_video (FALSE, &error);
  if (video == NULL)
    {
      g_test_skip (error->message);
      g_error_free (error);
      return;
    }

  VideoEditorState *state = video_editor_state_new (video);
  g_assert_true (video_editor_probe_metadata (state, &error));

  BlurRegion *r = blur_region_new (0, 0, 0, 0, 0.0, 3.0, TRUE);
  video_editor_add_region (state, r);

  gchar *output = g_build_filename (g_get_tmp_dir (),
                                    "xfce-test-fullblur.mp4", NULL);
  gchar **argv = video_editor_build_ffmpeg_argv (state, output);
  g_assert_nonnull (argv);

  gint exit_status;
  gboolean ok = g_spawn_sync (NULL, argv, NULL,
    G_SPAWN_DEFAULT,
    NULL, NULL, NULL, NULL,
    &exit_status, &error);

  g_assert_true (ok);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_true (g_file_test (output, G_FILE_TEST_EXISTS));

  video_editor_free_argv (argv);
  video_editor_state_free (state);
  g_unlink (output);
  g_unlink (video);
  g_free (output);
  g_free (video);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/video-editor/integration/probe-with-audio",
                    test_probe_with_audio);
  g_test_add_func ("/video-editor/integration/probe-without-audio",
                    test_probe_without_audio);
  g_test_add_func ("/video-editor/integration/apply-blur",
                    test_apply_blur_and_verify);
  g_test_add_func ("/video-editor/integration/apply-fullframe",
                    test_apply_fullframe_blur);

  return g_test_run ();
}
