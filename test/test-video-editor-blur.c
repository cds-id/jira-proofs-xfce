#include <glib.h>
#include <string.h>
#include "../lib/screenshooter-video-editor-blur.h"


static VideoEditorState *
make_test_state (void)
{
  VideoEditorState *state = video_editor_state_new ("/tmp/test.mp4");
  state->video_width = 1920;
  state->video_height = 1080;
  state->duration = 60.0;
  state->fps = 30.0;
  state->has_audio = TRUE;
  state->blur_radius = 10;
  return state;
}


static void
test_single_region (void)
{
  VideoEditorState *state = make_test_state ();
  BlurRegion *r = blur_region_new (100, 200, 300, 150, 5.0, 30.0, FALSE);
  video_editor_add_region (state, r);

  gchar *filter = video_editor_build_filter_chain (state);
  g_assert_nonnull (filter);

  /* Should contain split, crop, boxblur, overlay */
  g_assert_true (strstr (filter, "split=2") != NULL);
  g_assert_true (strstr (filter, "crop=300:150:100:200") != NULL);
  g_assert_true (strstr (filter, "boxblur=10") != NULL);
  g_assert_true (strstr (filter, "overlay=100:200") != NULL);
  g_assert_true (strstr (filter, "between(t,5.000,30.000)") != NULL);

  g_free (filter);
  video_editor_state_free (state);
}


static void
test_multiple_regions (void)
{
  VideoEditorState *state = make_test_state ();

  BlurRegion *r1 = blur_region_new (10, 20, 100, 50, 0.0, 30.0, FALSE);
  BlurRegion *r2 = blur_region_new (500, 300, 200, 100, 10.0, 45.0, FALSE);
  video_editor_add_region (state, r1);
  video_editor_add_region (state, r2);

  gchar *filter = video_editor_build_filter_chain (state);
  g_assert_nonnull (filter);

  /* Should have split=3 (2 regions + 1 base) */
  g_assert_true (strstr (filter, "split=3") != NULL);
  /* Should have two crop+blur stages */
  g_assert_true (strstr (filter, "[s0]crop=") != NULL);
  g_assert_true (strstr (filter, "[s1]crop=") != NULL);
  /* Should have sequential overlays */
  g_assert_true (strstr (filter, "[base][r0]overlay=") != NULL);
  g_assert_true (strstr (filter, "[t0];\n") != NULL);
  g_assert_true (strstr (filter, "[t0][r1]overlay=") != NULL);

  g_free (filter);
  video_editor_state_free (state);
}


static void
test_full_frame_single (void)
{
  VideoEditorState *state = make_test_state ();
  BlurRegion *r = blur_region_new (0, 0, 0, 0, 5.0, 20.0, TRUE);
  video_editor_add_region (state, r);

  gchar *filter = video_editor_build_filter_chain (state);
  g_assert_nonnull (filter);

  /* Full-frame single uses simple -vf form */
  g_assert_true (strstr (filter, "boxblur=10:enable='between(t,5.000,20.000)'") != NULL);
  /* Should NOT contain split */
  g_assert_null (strstr (filter, "split"));

  g_free (filter);
  video_editor_state_free (state);
}


static void
test_max_regions (void)
{
  VideoEditorState *state = make_test_state ();

  for (gint i = 0; i < VIDEO_EDITOR_MAX_REGIONS; i++)
    {
      BlurRegion *r = blur_region_new (i * 100, 0, 80, 80, 0, 60.0, FALSE);
      g_assert_true (video_editor_add_region (state, r));
    }

  /* 11th region should fail */
  BlurRegion *extra = blur_region_new (0, 0, 80, 80, 0, 60.0, FALSE);
  g_assert_false (video_editor_add_region (state, extra));
  blur_region_free (extra);

  gchar *filter = video_editor_build_filter_chain (state);
  g_assert_nonnull (filter);

  /* Should have split=11 (10 regions + 1 base) */
  g_assert_true (strstr (filter, "split=11") != NULL);

  g_free (filter);
  video_editor_state_free (state);
}


static void
test_region_clamping (void)
{
  BlurRegion *r = blur_region_new (1900, 1060, 100, 100, 0, 10.0, FALSE);
  video_editor_clamp_region (r, 1920, 1080);

  /* Region should be clamped to fit within video bounds */
  g_assert_cmpint (r->x + r->w, <=, 1920);
  g_assert_cmpint (r->y + r->h, <=, 1080);
  g_assert_cmpint (r->w, >=, VIDEO_EDITOR_MIN_REGION_PX);
  g_assert_cmpint (r->h, >=, VIDEO_EDITOR_MIN_REGION_PX);

  blur_region_free (r);
}


static void
test_region_min_size (void)
{
  BlurRegion *r = blur_region_new (100, 100, 5, 5, 0, 10.0, FALSE);
  video_editor_clamp_region (r, 1920, 1080);

  g_assert_cmpint (r->w, ==, VIDEO_EDITOR_MIN_REGION_PX);
  g_assert_cmpint (r->h, ==, VIDEO_EDITOR_MIN_REGION_PX);

  blur_region_free (r);
}


static void
test_boundary_times (void)
{
  VideoEditorState *state = make_test_state ();

  /* Region at t=0 */
  BlurRegion *r1 = blur_region_new (0, 0, 100, 100, 0.0, 10.0, FALSE);
  video_editor_add_region (state, r1);

  /* Region at t=duration */
  BlurRegion *r2 = blur_region_new (200, 200, 100, 100, 50.0, 60.0, FALSE);
  video_editor_add_region (state, r2);

  gchar *filter = video_editor_build_filter_chain (state);
  g_assert_nonnull (filter);
  g_assert_true (strstr (filter, "between(t,0.000,10.000)") != NULL);
  g_assert_true (strstr (filter, "between(t,50.000,60.000)") != NULL);

  g_free (filter);
  video_editor_state_free (state);
}


static void
test_ffmpeg_argv_single_region (void)
{
  VideoEditorState *state = make_test_state ();
  BlurRegion *r = blur_region_new (100, 200, 300, 150, 5.0, 30.0, FALSE);
  video_editor_add_region (state, r);

  gchar **argv = video_editor_build_ffmpeg_argv (state, "/tmp/out.mp4");
  g_assert_nonnull (argv);

  /* Check for key arguments */
  gboolean has_filter_complex = FALSE;
  gboolean has_map = FALSE;
  gboolean has_crf = FALSE;
  gboolean has_progress = FALSE;
  gboolean has_output = FALSE;

  for (gchar **p = argv; *p != NULL; p++)
    {
      if (g_strcmp0 (*p, "-filter_complex") == 0)
        has_filter_complex = TRUE;
      if (g_strcmp0 (*p, "-map") == 0)
        has_map = TRUE;
      if (g_strcmp0 (*p, "-crf") == 0)
        has_crf = TRUE;
      if (g_strcmp0 (*p, "-progress") == 0)
        has_progress = TRUE;
      if (g_strcmp0 (*p, "/tmp/out.mp4") == 0)
        has_output = TRUE;
    }

  g_assert_true (has_filter_complex);
  g_assert_true (has_map);
  g_assert_true (has_crf);
  g_assert_true (has_progress);
  g_assert_true (has_output);

  video_editor_free_argv (argv);
  video_editor_state_free (state);
}


static void
test_ffmpeg_argv_fullframe (void)
{
  VideoEditorState *state = make_test_state ();
  BlurRegion *r = blur_region_new (0, 0, 0, 0, 0.0, 60.0, TRUE);
  video_editor_add_region (state, r);

  gchar **argv = video_editor_build_ffmpeg_argv (state, "/tmp/out.mp4");
  g_assert_nonnull (argv);

  /* Full-frame should use -vf, not -filter_complex */
  gboolean has_vf = FALSE;
  gboolean has_filter_complex = FALSE;

  for (gchar **p = argv; *p != NULL; p++)
    {
      if (g_strcmp0 (*p, "-vf") == 0)
        has_vf = TRUE;
      if (g_strcmp0 (*p, "-filter_complex") == 0)
        has_filter_complex = TRUE;
    }

  g_assert_true (has_vf);
  g_assert_false (has_filter_complex);

  video_editor_free_argv (argv);
  video_editor_state_free (state);
}


static void
test_remove_region (void)
{
  VideoEditorState *state = make_test_state ();

  BlurRegion *r1 = blur_region_new (10, 10, 100, 100, 0, 10.0, FALSE);
  BlurRegion *r2 = blur_region_new (200, 200, 100, 100, 0, 10.0, FALSE);
  BlurRegion *r3 = blur_region_new (400, 400, 100, 100, 0, 10.0, FALSE);
  video_editor_add_region (state, r1);
  video_editor_add_region (state, r2);
  video_editor_add_region (state, r3);

  g_assert_cmpuint (g_list_length (state->regions), ==, 3);

  video_editor_remove_region (state, 1);
  g_assert_cmpuint (g_list_length (state->regions), ==, 2);

  /* First and third should remain */
  BlurRegion *first = g_list_nth_data (state->regions, 0);
  BlurRegion *second = g_list_nth_data (state->regions, 1);
  g_assert_cmpint (first->x, ==, 10);
  g_assert_cmpint (second->x, ==, 400);

  video_editor_state_free (state);
}


static void
test_five_regions (void)
{
  VideoEditorState *state = make_test_state ();

  for (gint i = 0; i < 5; i++)
    {
      BlurRegion *r = blur_region_new (i * 200, i * 100, 150, 80,
                                       i * 5.0, (i + 1) * 10.0, FALSE);
      video_editor_add_region (state, r);
    }

  gchar *filter = video_editor_build_filter_chain (state);
  g_assert_nonnull (filter);
  g_assert_true (strstr (filter, "split=6") != NULL);

  g_free (filter);
  video_editor_state_free (state);
}


static void
test_no_regions (void)
{
  VideoEditorState *state = make_test_state ();

  gchar *filter = video_editor_build_filter_chain (state);
  g_assert_null (filter);

  gchar **argv = video_editor_build_ffmpeg_argv (state, "/tmp/out.mp4");
  g_assert_null (argv);

  video_editor_state_free (state);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/video-editor/blur/single-region", test_single_region);
  g_test_add_func ("/video-editor/blur/multiple-regions", test_multiple_regions);
  g_test_add_func ("/video-editor/blur/full-frame-single", test_full_frame_single);
  g_test_add_func ("/video-editor/blur/max-regions", test_max_regions);
  g_test_add_func ("/video-editor/blur/five-regions", test_five_regions);
  g_test_add_func ("/video-editor/blur/region-clamping", test_region_clamping);
  g_test_add_func ("/video-editor/blur/region-min-size", test_region_min_size);
  g_test_add_func ("/video-editor/blur/boundary-times", test_boundary_times);
  g_test_add_func ("/video-editor/blur/ffmpeg-argv-single", test_ffmpeg_argv_single_region);
  g_test_add_func ("/video-editor/blur/ffmpeg-argv-fullframe", test_ffmpeg_argv_fullframe);
  g_test_add_func ("/video-editor/blur/remove-region", test_remove_region);
  g_test_add_func ("/video-editor/blur/no-regions", test_no_regions);

  return g_test_run ();
}
