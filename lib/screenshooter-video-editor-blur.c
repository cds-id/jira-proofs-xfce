#include "screenshooter-video-editor-blur.h"

#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <math.h>


VideoEditorState *
video_editor_state_new (const gchar *input_path)
{
  VideoEditorState *state = g_new0 (VideoEditorState, 1);
  state->input_path = g_strdup (input_path);
  state->blur_radius = VIDEO_EDITOR_DEFAULT_BLUR;
  state->regions = NULL;
  return state;
}


void
video_editor_state_free (VideoEditorState *state)
{
  if (state == NULL)
    return;

  g_free (state->input_path);
  g_list_free_full (state->regions, (GDestroyNotify) blur_region_free);
  g_free (state);
}


BlurRegion *
blur_region_new (gint x, gint y, gint w, gint h,
                 gdouble start_time, gdouble end_time,
                 gboolean full_frame)
{
  BlurRegion *region = g_new0 (BlurRegion, 1);
  region->x = x;
  region->y = y;
  region->w = w;
  region->h = h;
  region->start_time = start_time;
  region->end_time = end_time;
  region->full_frame = full_frame;
  return region;
}


void
blur_region_free (BlurRegion *region)
{
  g_free (region);
}


BlurRegion *
blur_region_copy (const BlurRegion *region)
{
  if (region == NULL)
    return NULL;
  return blur_region_new (region->x, region->y, region->w, region->h,
                          region->start_time, region->end_time,
                          region->full_frame);
}


gboolean
video_editor_add_region (VideoEditorState *state, BlurRegion *region)
{
  if (g_list_length (state->regions) >= VIDEO_EDITOR_MAX_REGIONS)
    return FALSE;

  state->regions = g_list_append (state->regions, region);
  return TRUE;
}


void
video_editor_remove_region (VideoEditorState *state, guint index)
{
  GList *link = g_list_nth (state->regions, index);
  if (link == NULL)
    return;

  blur_region_free (link->data);
  state->regions = g_list_delete_link (state->regions, link);
}


void
video_editor_clamp_region (BlurRegion *region, gint video_width, gint video_height)
{
  if (region->full_frame)
    return;

  region->x = CLAMP (region->x, 0, video_width - 1);
  region->y = CLAMP (region->y, 0, video_height - 1);

  if (region->x + region->w > video_width)
    region->w = video_width - region->x;
  if (region->y + region->h > video_height)
    region->h = video_height - region->y;

  if (region->w < VIDEO_EDITOR_MIN_REGION_PX)
    region->w = VIDEO_EDITOR_MIN_REGION_PX;
  if (region->h < VIDEO_EDITOR_MIN_REGION_PX)
    region->h = VIDEO_EDITOR_MIN_REGION_PX;

  /* Re-clamp after min-size enforcement */
  if (region->x + region->w > video_width)
    region->x = video_width - region->w;
  if (region->y + region->h > video_height)
    region->y = video_height - region->h;
}


gboolean
video_editor_probe_metadata (VideoEditorState *state, GError **error)
{
  gchar *ffprobe_path;
  gchar *stdout_data = NULL;
  gchar *stderr_data = NULL;
  gint exit_status;
  gboolean spawned;
  JsonParser *parser = NULL;
  JsonNode *root;
  JsonObject *root_obj;
  JsonArray *streams;
  gboolean found_video = FALSE;

  ffprobe_path = g_find_program_in_path ("ffprobe");
  if (ffprobe_path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "ffprobe is required for video editing. "
                   "Install with: sudo apt install ffmpeg");
      return FALSE;
    }

  gchar *argv[] = {
    ffprobe_path,
    "-v", "quiet",
    "-print_format", "json",
    "-show_streams",
    "-show_format",
    (gchar *) state->input_path,
    NULL
  };

  spawned = g_spawn_sync (NULL, argv, NULL,
                           G_SPAWN_DEFAULT,
                           NULL, NULL,
                           &stdout_data, &stderr_data,
                           &exit_status, error);
  g_free (ffprobe_path);

  if (!spawned)
    return FALSE;

  if (exit_status != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ffprobe failed to read file");
      g_free (stdout_data);
      g_free (stderr_data);
      return FALSE;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, stdout_data, -1, error))
    {
      g_object_unref (parser);
      g_free (stdout_data);
      g_free (stderr_data);
      return FALSE;
    }

  root = json_parser_get_root (parser);
  root_obj = json_node_get_object (root);

  /* Parse format.duration */
  if (json_object_has_member (root_obj, "format"))
    {
      JsonObject *format = json_object_get_object_member (root_obj, "format");
      if (json_object_has_member (format, "duration"))
        state->duration = g_ascii_strtod (
          json_object_get_string_member (format, "duration"), NULL);
    }

  /* Parse streams */
  state->has_audio = FALSE;
  streams = json_object_get_array_member (root_obj, "streams");

  for (guint i = 0; i < json_array_get_length (streams); i++)
    {
      JsonObject *stream = json_array_get_object_element (streams, i);
      const gchar *codec_type = json_object_get_string_member (stream, "codec_type");

      if (g_strcmp0 (codec_type, "video") == 0 && !found_video)
        {
          state->video_width = (gint) json_object_get_int_member (stream, "width");
          state->video_height = (gint) json_object_get_int_member (stream, "height");

          /* Parse r_frame_rate (e.g. "30/1" or "30000/1001") */
          if (json_object_has_member (stream, "r_frame_rate"))
            {
              const gchar *fps_str = json_object_get_string_member (stream, "r_frame_rate");
              gchar **parts = g_strsplit (fps_str, "/", 2);
              if (parts[0] && parts[1])
                {
                  gdouble num = g_ascii_strtod (parts[0], NULL);
                  gdouble den = g_ascii_strtod (parts[1], NULL);
                  if (den > 0)
                    state->fps = num / den;
                }
              else if (parts[0])
                {
                  state->fps = g_ascii_strtod (parts[0], NULL);
                }
              g_strfreev (parts);
            }

          found_video = TRUE;
        }
      else if (g_strcmp0 (codec_type, "audio") == 0)
        {
          state->has_audio = TRUE;
        }
    }

  g_object_unref (parser);
  g_free (stdout_data);
  g_free (stderr_data);

  if (!found_video)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No video stream found in file");
      return FALSE;
    }

  if (state->video_width <= 0 || state->video_height <= 0 || state->duration <= 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid video metadata (width=%d, height=%d, duration=%.1f)",
                   state->video_width, state->video_height, state->duration);
      return FALSE;
    }

  return TRUE;
}


/* Format a double with C locale (dot decimal separator) for FFmpeg */
static const gchar *
fmt_time (gchar *buf, gsize buf_size, gdouble t)
{
  g_ascii_formatd (buf, buf_size, "%.3f", t);
  return buf;
}


gchar *
video_editor_build_filter_chain (VideoEditorState *state)
{
  GList *l;
  guint n;
  GString *filter;
  gchar t1[G_ASCII_DTOSTR_BUF_SIZE], t2[G_ASCII_DTOSTR_BUF_SIZE];

  if (state->regions == NULL)
    return NULL;

  n = g_list_length (state->regions);

  /* Check for single full-frame blur */
  if (n == 1)
    {
      BlurRegion *r = state->regions->data;
      if (r->full_frame)
        {
          fmt_time (t1, sizeof (t1), r->start_time);
          fmt_time (t2, sizeof (t2), r->end_time);
          return g_strdup_printf ("boxblur=%d:enable='between(t,%s,%s)'",
                                  state->blur_radius, t1, t2);
        }
    }

  filter = g_string_new ("");

  /* split into N+1 streams: [base][s0][s1]...[sN-1] */
  g_string_append_printf (filter, "[0:v]split=%u[base]", n + 1);
  for (guint i = 0; i < n; i++)
    g_string_append_printf (filter, "[s%u]", i);
  g_string_append (filter, ";\n");

  /* crop+blur each split (clamp radius to min(w,h)/2 per FFmpeg boxblur limit) */
  guint idx = 0;
  for (l = state->regions; l != NULL; l = l->next, idx++)
    {
      BlurRegion *r = l->data;
      gint radius = state->blur_radius;

      if (!r->full_frame)
        {
          /* YUV420p chroma planes are half-size, so max safe radius is min(w,h)/4 */
          gint max_radius = MIN (r->w, r->h) / 4;
          if (max_radius < 1)
            max_radius = 1;
          if (radius > max_radius)
            radius = max_radius;
        }

      if (r->full_frame)
        g_string_append_printf (filter, "[s%u]boxblur=%d[r%u];\n",
                                idx, radius, idx);
      else
        g_string_append_printf (filter, "[s%u]crop=%d:%d:%d:%d,boxblur=%d[r%u];\n",
                                idx, r->w, r->h, r->x, r->y,
                                radius, idx);
    }

  /* sequential overlays: all get output labels [t0]..[tN-1] */
  idx = 0;
  for (l = state->regions; l != NULL; l = l->next, idx++)
    {
      BlurRegion *r = l->data;
      gchar input_buf[16];

      if (idx == 0)
        g_snprintf (input_buf, sizeof (input_buf), "base");
      else
        g_snprintf (input_buf, sizeof (input_buf), "t%u", idx - 1);

      fmt_time (t1, sizeof (t1), r->start_time);
      fmt_time (t2, sizeof (t2), r->end_time);

      if (r->full_frame)
        g_string_append_printf (filter,
          "[%s][r%u]overlay=0:0:enable='between(t,%s,%s)'[t%u]",
          input_buf, idx, t1, t2, idx);
      else
        g_string_append_printf (filter,
          "[%s][r%u]overlay=%d:%d:enable='between(t,%s,%s)'[t%u]",
          input_buf, idx, r->x, r->y, t1, t2, idx);

      if (idx < n - 1)
        g_string_append (filter, ";\n");
    }

  return g_string_free (filter, FALSE);
}


gchar **
video_editor_build_ffmpeg_argv (VideoEditorState *state, const gchar *output_path)
{
  GPtrArray *args;
  gchar *ffmpeg_path;
  gchar *filter;
  gboolean use_vf;

  ffmpeg_path = g_find_program_in_path ("ffmpeg");
  if (ffmpeg_path == NULL)
    return NULL;

  filter = video_editor_build_filter_chain (state);
  if (filter == NULL)
    {
      g_free (ffmpeg_path);
      return NULL;
    }

  /* Check if single full-frame (uses -vf instead of -filter_complex) */
  use_vf = (g_list_length (state->regions) == 1 &&
            ((BlurRegion *) state->regions->data)->full_frame);

  args = g_ptr_array_new ();
  g_ptr_array_add (args, ffmpeg_path);
  g_ptr_array_add (args, g_strdup ("-y"));
  g_ptr_array_add (args, g_strdup ("-i"));
  g_ptr_array_add (args, g_strdup (state->input_path));

  if (use_vf)
    {
      g_ptr_array_add (args, g_strdup ("-vf"));
      g_ptr_array_add (args, filter);
    }
  else
    {
      guint n = g_list_length (state->regions);
      g_ptr_array_add (args, g_strdup ("-filter_complex"));
      g_ptr_array_add (args, filter);
      g_ptr_array_add (args, g_strdup ("-map"));
      g_ptr_array_add (args, g_strdup_printf ("[t%u]", n - 1));
    }

  /* Map audio optionally */
  g_ptr_array_add (args, g_strdup ("-map"));
  g_ptr_array_add (args, g_strdup ("0:a?"));

  /* Encoding params */
  g_ptr_array_add (args, g_strdup ("-c:v"));
  g_ptr_array_add (args, g_strdup ("libx264"));
  g_ptr_array_add (args, g_strdup ("-preset"));
  g_ptr_array_add (args, g_strdup ("ultrafast"));
  g_ptr_array_add (args, g_strdup ("-crf"));
  g_ptr_array_add (args, g_strdup ("23"));
  g_ptr_array_add (args, g_strdup ("-pix_fmt"));
  g_ptr_array_add (args, g_strdup ("yuv420p"));
  g_ptr_array_add (args, g_strdup ("-c:a"));
  g_ptr_array_add (args, g_strdup ("copy"));

  /* Progress tracking */
  g_ptr_array_add (args, g_strdup ("-progress"));
  g_ptr_array_add (args, g_strdup ("pipe:1"));

  g_ptr_array_add (args, g_strdup (output_path));
  g_ptr_array_add (args, NULL);

  return (gchar **) g_ptr_array_free (args, FALSE);
}


void
video_editor_free_argv (gchar **argv)
{
  if (argv == NULL)
    return;
  for (gchar **p = argv; *p != NULL; p++)
    g_free (*p);
  g_free (argv);
}


gboolean
video_editor_save_config (VideoEditorState *state, const gchar *path,
                          GError **error)
{
  JsonBuilder *builder = json_builder_new ();

  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "blur_radius");
  json_builder_add_int_value (builder, state->blur_radius);

  json_builder_set_member_name (builder, "regions");
  json_builder_begin_array (builder);

  for (GList *l = state->regions; l != NULL; l = l->next)
    {
      BlurRegion *r = l->data;
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "x");
      json_builder_add_int_value (builder, r->x);
      json_builder_set_member_name (builder, "y");
      json_builder_add_int_value (builder, r->y);
      json_builder_set_member_name (builder, "w");
      json_builder_add_int_value (builder, r->w);
      json_builder_set_member_name (builder, "h");
      json_builder_add_int_value (builder, r->h);
      json_builder_set_member_name (builder, "start");
      json_builder_add_double_value (builder, r->start_time);
      json_builder_set_member_name (builder, "end");
      json_builder_add_double_value (builder, r->end_time);
      json_builder_set_member_name (builder, "full_frame");
      json_builder_add_boolean_value (builder, r->full_frame);
      json_builder_end_object (builder);
    }

  json_builder_end_array (builder);
  json_builder_end_object (builder);

  JsonGenerator *gen = json_generator_new ();
  json_generator_set_pretty (gen, TRUE);
  json_generator_set_root (gen, json_builder_get_root (builder));

  gboolean ok = json_generator_to_file (gen, path, error);

  g_object_unref (gen);
  g_object_unref (builder);
  return ok;
}


gboolean
video_editor_load_config (VideoEditorState *state, const gchar *path,
                          GError **error)
{
  JsonParser *parser = json_parser_new ();

  if (!json_parser_load_from_file (parser, path, error))
    {
      g_object_unref (parser);
      return FALSE;
    }

  JsonNode *root = json_parser_get_root (parser);
  JsonObject *obj = json_node_get_object (root);

  /* Clear existing regions */
  g_list_free_full (state->regions, (GDestroyNotify) blur_region_free);
  state->regions = NULL;

  if (json_object_has_member (obj, "blur_radius"))
    state->blur_radius = (gint) json_object_get_int_member (obj, "blur_radius");

  if (json_object_has_member (obj, "regions"))
    {
      JsonArray *arr = json_object_get_array_member (obj, "regions");
      for (guint i = 0; i < json_array_get_length (arr); i++)
        {
          JsonObject *robj = json_array_get_object_element (arr, i);
          BlurRegion *r = blur_region_new (
            (gint) json_object_get_int_member (robj, "x"),
            (gint) json_object_get_int_member (robj, "y"),
            (gint) json_object_get_int_member (robj, "w"),
            (gint) json_object_get_int_member (robj, "h"),
            json_object_get_double_member (robj, "start"),
            json_object_get_double_member (robj, "end"),
            json_object_get_boolean_member (robj, "full_frame"));
          state->regions = g_list_append (state->regions, r);
        }
    }

  g_object_unref (parser);
  return TRUE;
}
