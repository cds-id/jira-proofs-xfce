#include "screenshooter-video-editor.h"
#include <sc-video-editor-blur.h>
#include "screenshooter-video-editor-canvas.h"
#include "screenshooter-video-editor-timeline.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

typedef struct {
  GtkWidget *window;
  VideoEditorState *state;
  VideoEditorCanvas *canvas;
  VideoEditorTimeline *timeline;

  /* Frame extraction */
  gchar *preview_path;
  GPid extract_pid;
  guint extract_watch_id;
  gboolean extracting;

  /* Export */
  GPid export_pid;
  guint export_watch_id;
  GIOChannel *export_stdout_channel;
  guint export_stdout_watch_id;
  GIOChannel *export_stderr_channel;
  guint export_stderr_watch_id;
  GString *export_stderr_buf;
  GtkWidget *progress_dialog;
  GtkWidget *progress_bar;
  gchar *export_output_path;
} VideoEditorData;


static void extract_frame_at (VideoEditorData *data, gdouble timestamp);


gboolean
screenshooter_video_editor_available (void)
{
  gchar *ffmpeg = g_find_program_in_path ("ffmpeg");
  gchar *ffprobe = g_find_program_in_path ("ffprobe");
  gboolean available = (ffmpeg != NULL && ffprobe != NULL);
  g_free (ffmpeg);
  g_free (ffprobe);
  return available;
}


static void
cleanup_preview (VideoEditorData *data)
{
  if (data->preview_path)
    {
      g_unlink (data->preview_path);
      g_free (data->preview_path);
      data->preview_path = NULL;
    }
}


static void
cleanup_export (VideoEditorData *data)
{
  if (data->export_stdout_watch_id > 0)
    {
      g_source_remove (data->export_stdout_watch_id);
      data->export_stdout_watch_id = 0;
    }
  if (data->export_stderr_watch_id > 0)
    {
      g_source_remove (data->export_stderr_watch_id);
      data->export_stderr_watch_id = 0;
    }
  if (data->export_stdout_channel)
    {
      g_io_channel_unref (data->export_stdout_channel);
      data->export_stdout_channel = NULL;
    }
  if (data->export_stderr_channel)
    {
      g_io_channel_unref (data->export_stderr_channel);
      data->export_stderr_channel = NULL;
    }
  if (data->export_stderr_buf)
    {
      g_string_free (data->export_stderr_buf, TRUE);
      data->export_stderr_buf = NULL;
    }
}


static void
editor_data_free (VideoEditorData *data)
{
  if (data == NULL)
    return;

  if (data->extract_watch_id > 0)
    g_source_remove (data->extract_watch_id);

  cleanup_preview (data);
  cleanup_export (data);

  video_editor_canvas_free (data->canvas);
  video_editor_timeline_free (data->timeline);
  video_editor_state_free (data->state);
  g_free (data->export_output_path);
  g_free (data);
}


/* Frame extraction callbacks */

static void
cb_extract_child_watch (GPid pid, gint status, gpointer user_data)
{
  VideoEditorData *data = user_data;

  g_spawn_close_pid (pid);
  data->extract_watch_id = 0;
  data->extracting = FALSE;

  /* Check if the window was destroyed while extracting */
  if (data->canvas == NULL)
    return;

  /* Check if file exists regardless of exit status (FFmpeg may return
   * non-zero even when the frame was written successfully) */
  if (data->preview_path &&
      g_file_test (data->preview_path, G_FILE_TEST_EXISTS))
    {
      GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (data->preview_path, NULL);
      if (pixbuf)
        {
          video_editor_canvas_set_pixbuf (data->canvas, pixbuf);
          g_object_unref (pixbuf);
        }
      else
        {
          video_editor_canvas_set_loading (data->canvas, FALSE);
        }
    }
  else
    {
      video_editor_canvas_set_loading (data->canvas, FALSE);
    }
}


static void
extract_frame_at (VideoEditorData *data, gdouble timestamp)
{
  gchar *ffmpeg_path;
  gchar *timestamp_str;
  gboolean spawned;
  GError *error = NULL;

  /* Cancel any in-progress extraction */
  if (data->extracting && data->extract_pid > 0)
    {
      kill (data->extract_pid, SIGINT);
      waitpid (data->extract_pid, NULL, 0);
      if (data->extract_watch_id > 0)
        {
          g_source_remove (data->extract_watch_id);
          data->extract_watch_id = 0;
        }
      data->extracting = FALSE;
    }

  ffmpeg_path = g_find_program_in_path ("ffmpeg");
  if (ffmpeg_path == NULL)
    return;

  /* Set up preview path */
  if (data->preview_path == NULL)
    data->preview_path = g_build_filename (g_get_tmp_dir (),
                                           "xfce-screenshooter-preview.png",
                                           NULL);

  /* Use C locale formatting — FFmpeg rejects comma decimal separators */
  timestamp_str = g_malloc (G_ASCII_DTOSTR_BUF_SIZE);
  g_ascii_formatd (timestamp_str, G_ASCII_DTOSTR_BUF_SIZE, "%.3f", timestamp);

  /* Build argv: -ss before -i for fast input seeking, but skip -ss for
   * timestamp 0 where input seeking can fail on some videos */
  GPtrArray *av = g_ptr_array_new ();
  g_ptr_array_add (av, ffmpeg_path);
  g_ptr_array_add (av, (gchar *) "-y");
  if (timestamp > 0.01)
    {
      g_ptr_array_add (av, (gchar *) "-ss");
      g_ptr_array_add (av, timestamp_str);
    }
  g_ptr_array_add (av, (gchar *) "-i");
  g_ptr_array_add (av, data->state->input_path);
  g_ptr_array_add (av, (gchar *) "-frames:v");
  g_ptr_array_add (av, (gchar *) "1");
  g_ptr_array_add (av, (gchar *) "-update");
  g_ptr_array_add (av, (gchar *) "1");
  g_ptr_array_add (av, data->preview_path);
  g_ptr_array_add (av, NULL);

  gchar **argv = (gchar **) g_ptr_array_free (av, FALSE);

  video_editor_canvas_set_loading (data->canvas, TRUE);

  spawned = g_spawn_async (NULL, argv, NULL,
                            G_SPAWN_DO_NOT_REAP_CHILD,
                            NULL, NULL,
                            &data->extract_pid, &error);

  g_free (argv);  /* Only free the array, not the strings (they're literals/owned elsewhere) */
  g_free (ffmpeg_path);
  g_free (timestamp_str);

  if (!spawned)
    {
      g_warning ("Failed to extract frame: %s", error->message);
      g_error_free (error);
      video_editor_canvas_set_loading (data->canvas, FALSE);
      return;
    }

  data->extracting = TRUE;
  data->extract_watch_id = g_child_watch_add (data->extract_pid,
                                               cb_extract_child_watch, data);
}


/* Canvas region drawn callback */

static void
cb_region_drawn (gint x, gint y, gint w, gint h, gpointer user_data)
{
  VideoEditorData *data = user_data;

  BlurRegion *r = blur_region_new (x, y, w, h,
                                   video_editor_timeline_get_time (data->timeline),
                                   data->state->duration,
                                   FALSE);
  video_editor_clamp_region (r, data->state->video_width, data->state->video_height);

  if (video_editor_add_region (data->state, r))
    {
      video_editor_timeline_refresh (data->timeline);
      video_editor_canvas_queue_draw (data->canvas);
    }
  else
    {
      blur_region_free (r);
    }
}


/* Timeline callbacks */

static void
cb_scrub (gdouble timestamp, gpointer user_data)
{
  VideoEditorData *data = user_data;
  video_editor_canvas_set_time (data->canvas, timestamp);
  extract_frame_at (data, timestamp);
}


static void
cb_region_changed (gpointer user_data)
{
  VideoEditorData *data = user_data;

  /* Check if any region is full-frame to disable canvas drawing */
  gboolean has_fullframe = FALSE;
  for (GList *l = data->state->regions; l != NULL; l = l->next)
    {
      if (((BlurRegion *) l->data)->full_frame)
        {
          has_fullframe = TRUE;
          break;
        }
    }
  video_editor_canvas_set_draw_enabled (data->canvas, !has_fullframe);
  video_editor_canvas_queue_draw (data->canvas);
}


/* Export (Apply & Save) */

static gchar *
build_default_output_path (const gchar *input_path)
{
  gchar *dir = g_path_get_dirname (input_path);
  gchar *base = g_path_get_basename (input_path);

  /* Remove .mp4 extension */
  gchar *dot = g_strrstr (base, ".mp4");
  if (dot == NULL)
    dot = g_strrstr (base, ".MP4");
  if (dot)
    *dot = '\0';

  gchar *result = g_strdup_printf ("%s/%s_blurred.mp4", dir, base);
  g_free (dir);
  g_free (base);
  return result;
}


static gboolean
cb_export_stdout_watch (GIOChannel *channel, GIOCondition condition,
                        gpointer user_data)
{
  VideoEditorData *data = user_data;
  gchar *line = NULL;
  gsize length;

  if (condition & G_IO_IN)
    {
      while (g_io_channel_read_line (channel, &line, &length, NULL, NULL)
             == G_IO_STATUS_NORMAL)
        {
          if (g_str_has_prefix (line, "out_time_ms="))
            {
              gint64 out_time_us = g_ascii_strtoll (line + 12, NULL, 10);
              gdouble progress = (gdouble) out_time_us / (data->state->duration * 1000000.0);
              progress = CLAMP (progress, 0.0, 1.0);

              if (data->progress_bar)
                {
                  gtk_progress_bar_set_fraction (
                    GTK_PROGRESS_BAR (data->progress_bar), progress);

                  gchar *text = g_strdup_printf ("%.0f%%", progress * 100);
                  gtk_progress_bar_set_text (
                    GTK_PROGRESS_BAR (data->progress_bar), text);
                  g_free (text);
                }
            }
          g_free (line);
        }
    }

  if (condition & (G_IO_HUP | G_IO_ERR))
    {
      data->export_stdout_watch_id = 0;
      return FALSE;
    }

  return TRUE;
}


static gboolean
cb_export_stderr_watch (GIOChannel *channel, GIOCondition condition,
                        gpointer user_data)
{
  VideoEditorData *data = user_data;
  gchar *line = NULL;
  gsize length;

  if (condition & G_IO_IN)
    {
      while (g_io_channel_read_line (channel, &line, &length, NULL, NULL)
             == G_IO_STATUS_NORMAL)
        {
          g_string_append (data->export_stderr_buf, line);
          g_free (line);
        }
    }

  if (condition & (G_IO_HUP | G_IO_ERR))
    {
      data->export_stderr_watch_id = 0;
      return FALSE;
    }

  return TRUE;
}


static void
cb_export_child_watch (GPid pid, gint status, gpointer user_data)
{
  VideoEditorData *data = user_data;

  g_spawn_close_pid (pid);
  data->export_watch_id = 0;

  if (data->progress_dialog)
    {
      gtk_widget_destroy (data->progress_dialog);
      data->progress_dialog = NULL;
      data->progress_bar = NULL;
    }

  /* Read stderr before cleanup frees the buffer */
  gchar *stderr_msg = NULL;
  if (data->export_stderr_buf && data->export_stderr_buf->len > 0)
    stderr_msg = g_strdup (data->export_stderr_buf->str);

  cleanup_export (data);

  if (status == 0)
    {
      GtkWidget *info = gtk_message_dialog_new (GTK_WINDOW (data->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "Video saved successfully to:\n%s", data->export_output_path);
      gtk_dialog_run (GTK_DIALOG (info));
      gtk_widget_destroy (info);
    }
  else
    {
      /* Delete partial output */
      if (data->export_output_path &&
          g_file_test (data->export_output_path, G_FILE_TEST_EXISTS))
        g_unlink (data->export_output_path);

      GtkWidget *err = gtk_message_dialog_new (GTK_WINDOW (data->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Export failed:\n%s", stderr_msg ? stderr_msg : "Unknown error");
      gtk_dialog_run (GTK_DIALOG (err));
      gtk_widget_destroy (err);
    }

  g_free (stderr_msg);
}


static void
cb_progress_cancel (GtkDialog *dialog, gint response_id, VideoEditorData *data)
{
  if (data->export_pid > 0)
    {
      kill (data->export_pid, SIGINT);
      /* Child watch will handle cleanup */
    }
}


static void
do_export (VideoEditorData *data)
{
  gchar *default_path;
  GtkWidget *save_dlg;
  gchar *output_path;
  gchar **argv;
  gint stdout_fd, stderr_fd;
  GError *error = NULL;

  if (data->state->regions == NULL)
    {
      GtkWidget *warn = gtk_message_dialog_new (GTK_WINDOW (data->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
        "Add at least one blur region before applying.");
      gtk_dialog_run (GTK_DIALOG (warn));
      gtk_widget_destroy (warn);
      return;
    }

  /* File chooser for output */
  default_path = build_default_output_path (data->state->input_path);

  save_dlg = gtk_file_chooser_dialog_new ("Save Blurred Video",
    GTK_WINDOW (data->window), GTK_FILE_CHOOSER_ACTION_SAVE,
    "_Cancel", GTK_RESPONSE_CANCEL,
    "_Save", GTK_RESPONSE_ACCEPT,
    NULL);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (save_dlg), TRUE);
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (save_dlg),
                                     g_path_get_basename (default_path));

  gchar *dir = g_path_get_dirname (default_path);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (save_dlg), dir);
  g_free (dir);
  g_free (default_path);

  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "MP4 Video (*.mp4)");
  gtk_file_filter_add_pattern (filter, "*.mp4");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (save_dlg), filter);

  if (gtk_dialog_run (GTK_DIALOG (save_dlg)) != GTK_RESPONSE_ACCEPT)
    {
      gtk_widget_destroy (save_dlg);
      return;
    }

  output_path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (save_dlg));
  gtk_widget_destroy (save_dlg);

  /* Prevent overwriting source file */
  if (g_strcmp0 (output_path, data->state->input_path) == 0)
    {
      g_free (output_path);
      output_path = build_default_output_path (data->state->input_path);
    }

  g_free (data->export_output_path);
  data->export_output_path = output_path;

  /* Build FFmpeg command */
  argv = video_editor_build_ffmpeg_argv (data->state, output_path);
  if (argv == NULL)
    {
      GtkWidget *err = gtk_message_dialog_new (GTK_WINDOW (data->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Failed to build FFmpeg command.");
      gtk_dialog_run (GTK_DIALOG (err));
      gtk_widget_destroy (err);
      return;
    }

  /* Show progress dialog */
  data->progress_dialog = gtk_dialog_new_with_buttons ("Exporting...",
    GTK_WINDOW (data->window),
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "_Cancel", GTK_RESPONSE_CANCEL,
    NULL);
  gtk_window_set_resizable (GTK_WINDOW (data->progress_dialog), FALSE);
  gtk_widget_set_size_request (data->progress_dialog, 350, -1);

  GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (data->progress_dialog));
  gtk_container_set_border_width (GTK_CONTAINER (content), 12);

  data->progress_bar = gtk_progress_bar_new ();
  gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (data->progress_bar), TRUE);
  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (data->progress_bar), "0%");
  gtk_box_pack_start (GTK_BOX (content), data->progress_bar, FALSE, FALSE, 8);

  g_signal_connect (data->progress_dialog, "response",
                    G_CALLBACK (cb_progress_cancel), data);

  gtk_widget_show_all (data->progress_dialog);

  /* Spawn FFmpeg */
  data->export_stderr_buf = g_string_new ("");

  gboolean spawned = g_spawn_async_with_pipes (
    NULL, argv, NULL,
    G_SPAWN_DO_NOT_REAP_CHILD,
    NULL, NULL,
    &data->export_pid,
    NULL,        /* stdin */
    &stdout_fd,  /* stdout (progress) */
    &stderr_fd,  /* stderr (errors) */
    &error);

  video_editor_free_argv (argv);

  if (!spawned)
    {
      gtk_widget_destroy (data->progress_dialog);
      data->progress_dialog = NULL;
      data->progress_bar = NULL;

      GtkWidget *err = gtk_message_dialog_new (GTK_WINDOW (data->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Failed to start FFmpeg: %s", error->message);
      gtk_dialog_run (GTK_DIALOG (err));
      gtk_widget_destroy (err);
      g_error_free (error);
      return;
    }

  /* Watch stdout for progress */
  data->export_stdout_channel = g_io_channel_unix_new (stdout_fd);
  g_io_channel_set_flags (data->export_stdout_channel, G_IO_FLAG_NONBLOCK, NULL);
  data->export_stdout_watch_id = g_io_add_watch (data->export_stdout_channel,
    G_IO_IN | G_IO_HUP | G_IO_ERR,
    cb_export_stdout_watch, data);

  /* Watch stderr for errors */
  data->export_stderr_channel = g_io_channel_unix_new (stderr_fd);
  g_io_channel_set_flags (data->export_stderr_channel, G_IO_FLAG_NONBLOCK, NULL);
  data->export_stderr_watch_id = g_io_add_watch (data->export_stderr_channel,
    G_IO_IN | G_IO_HUP | G_IO_ERR,
    cb_export_stderr_watch, data);

  /* Watch child process */
  data->export_watch_id = g_child_watch_add (data->export_pid,
                                              cb_export_child_watch, data);
}


/* Window callbacks */

static void
cb_cancel (GtkButton *button, VideoEditorData *data)
{
  gtk_widget_destroy (data->window);
}


static void
cb_apply (GtkButton *button, VideoEditorData *data)
{
  do_export (data);
}


static void
cb_window_destroy (GtkWidget *widget, VideoEditorData *data)
{
  /* Remove child watch BEFORE killing/reaping to prevent callback on freed data */
  if (data->extract_watch_id > 0)
    {
      g_source_remove (data->extract_watch_id);
      data->extract_watch_id = 0;
    }

  /* Kill any running extraction */
  if (data->extracting && data->extract_pid > 0)
    {
      kill (data->extract_pid, SIGINT);
      waitpid (data->extract_pid, NULL, 0);
      g_spawn_close_pid (data->extract_pid);
      data->extracting = FALSE;
    }

  cleanup_preview (data);

  /* Free subcomponents but not data itself if export is still running */
  if (data->export_watch_id > 0)
    {
      /* Export still running - kill it */
      kill (data->export_pid, SIGINT);
    }

  cleanup_export (data);
  video_editor_canvas_free (data->canvas);
  data->canvas = NULL;
  video_editor_timeline_free (data->timeline);
  data->timeline = NULL;
  video_editor_state_free (data->state);
  data->state = NULL;
  g_free (data->export_output_path);
  data->export_output_path = NULL;
  g_free (data);
}


static void
launch_editor (const gchar *filepath)
{
  VideoEditorData *data;
  GError *error = NULL;

  data = g_new0 (VideoEditorData, 1);
  data->state = video_editor_state_new (filepath);

  /* Probe metadata */
  if (!video_editor_probe_metadata (data->state, &error))
    {
      GtkWidget *err = gtk_message_dialog_new (NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Failed to read video: %s", error->message);
      gtk_dialog_run (GTK_DIALOG (err));
      gtk_widget_destroy (err);
      g_error_free (error);
      editor_data_free (data);
      return;
    }

  /* Create editor window */
  gchar *basename = g_path_get_basename (filepath);
  gchar *title = g_strdup_printf ("Edit Video - %s", basename);

  data->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (data->window), title);
  gtk_window_set_default_size (GTK_WINDOW (data->window), 800, 700);
  gtk_window_set_position (GTK_WINDOW (data->window), GTK_WIN_POS_CENTER);
  gtk_window_set_icon_name (GTK_WINDOW (data->window), "org.xfce.screenshooter");

  g_free (basename);
  g_free (title);

  g_signal_connect (data->window, "destroy",
                    G_CALLBACK (cb_window_destroy), data);

  /* Main layout */
  GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (data->window), main_box);

  /* Canvas */
  data->canvas = video_editor_canvas_new (data->state);
  video_editor_canvas_set_region_drawn_callback (data->canvas,
                                                 cb_region_drawn, data);
  gtk_box_pack_start (GTK_BOX (main_box), data->canvas->drawing_area, TRUE, TRUE, 0);

  /* Timeline and controls */
  data->timeline = video_editor_timeline_new (data->state);
  video_editor_timeline_set_scrub_callback (data->timeline, cb_scrub, data);
  video_editor_timeline_set_region_changed_callback (data->timeline,
                                                     cb_region_changed, data);
  gtk_box_pack_start (GTK_BOX (main_box), data->timeline->container, FALSE, FALSE, 0);

  /* Button row */
  GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start (button_box, 12);
  gtk_widget_set_margin_end (button_box, 12);
  gtk_widget_set_margin_top (button_box, 8);
  gtk_widget_set_margin_bottom (button_box, 12);
  gtk_widget_set_halign (button_box, GTK_ALIGN_END);

  GtkWidget *cancel_btn = gtk_button_new_with_label ("Cancel");
  g_signal_connect (cancel_btn, "clicked", G_CALLBACK (cb_cancel), data);
  gtk_box_pack_start (GTK_BOX (button_box), cancel_btn, FALSE, FALSE, 0);

  GtkWidget *apply_btn = gtk_button_new_with_label ("Apply & Save");
  GtkStyleContext *ctx = gtk_widget_get_style_context (apply_btn);
  gtk_style_context_add_class (ctx, "suggested-action");
  g_signal_connect (apply_btn, "clicked", G_CALLBACK (cb_apply), data);
  gtk_box_pack_start (GTK_BOX (button_box), apply_btn, FALSE, FALSE, 0);

  gtk_box_pack_end (GTK_BOX (main_box), button_box, FALSE, FALSE, 0);

  gtk_widget_show_all (data->window);

  /* Extract first frame */
  extract_frame_at (data, 0);
}


void
screenshooter_video_editor_run (GtkWindow *parent)
{
  GtkWidget *chooser;
  GtkFileFilter *filter;

  if (!screenshooter_video_editor_available ())
    {
      GtkWidget *err = gtk_message_dialog_new (parent,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "FFmpeg and ffprobe are required for video editing.\n"
        "Install with: sudo apt install ffmpeg");
      gtk_dialog_run (GTK_DIALOG (err));
      gtk_widget_destroy (err);
      return;
    }

  chooser = gtk_file_chooser_dialog_new ("Open Video",
    parent, GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Cancel", GTK_RESPONSE_CANCEL,
    "_Open", GTK_RESPONSE_ACCEPT,
    NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "MP4 Video (*.mp4)");
  gtk_file_filter_add_pattern (filter, "*.mp4");
  gtk_file_filter_add_pattern (filter, "*.MP4");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT)
    {
      gchar *filepath = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
      gtk_widget_destroy (chooser);
      launch_editor (filepath);
      g_free (filepath);
    }
  else
    {
      gtk_widget_destroy (chooser);
    }
}


void
screenshooter_video_editor_run_with_file (const gchar *filepath)
{
  launch_editor (filepath);
}
