#include "screenshooter-recorder-dialog.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#endif

typedef struct {
  RecorderState        *state;
  RecorderStopCallback  callback;
  gpointer              user_data;
  GtkWidget            *window;
  GtkWidget            *timer_label;
  guint                 timer_id;
  guint                 elapsed;
  gboolean              finished;
} RecorderDialogData;


static void
do_stop (RecorderDialogData *data)
{
  GError *error = NULL;
  gchar *output_path;

  if (data->finished)
    return;
  data->finished = TRUE;

  if (data->timer_id > 0)
    {
      g_source_remove (data->timer_id);
      data->timer_id = 0;
    }

  output_path = screenshooter_recorder_stop (data->state, &error);

  if (error)
    {
      GtkWidget *err_dlg = gtk_message_dialog_new (NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Recording failed: %s", error->message);
      gtk_dialog_run (GTK_DIALOG (err_dlg));
      gtk_widget_destroy (err_dlg);
      g_error_free (error);

      /* Clean up temp file */
      if (data->state->output_path)
        g_unlink (data->state->output_path);
    }

  gtk_widget_destroy (data->window);

  if (data->callback)
    data->callback (output_path, data->user_data);

  g_free (output_path);
  screenshooter_recorder_free (data->state);
  g_free (data);
}


static gboolean
cb_timer_tick (gpointer user_data)
{
  RecorderDialogData *data = user_data;
  gchar *text;

  data->elapsed++;
  text = g_strdup_printf ("%02u:%02u", data->elapsed / 60, data->elapsed % 60);
  gtk_label_set_text (GTK_LABEL (data->timer_label), text);
  g_free (text);

  return TRUE;
}


static void
cb_stop_clicked (GtkButton *button, RecorderDialogData *data)
{
  do_stop (data);
}


static gboolean
cb_key_press (GtkWidget *widget, GdkEventKey *event, RecorderDialogData *data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      do_stop (data);
      return TRUE;
    }
  return FALSE;
}


static gboolean
cb_delete_event (GtkWidget *widget, GdkEvent *event, RecorderDialogData *data)
{
  do_stop (data);
  return TRUE;
}


static void
cb_child_watch (GPid pid, gint status, gpointer user_data)
{
  RecorderDialogData *data = user_data;

  g_spawn_close_pid (pid);
  data->state->child_watch_id = 0;

  /* FFmpeg exited unexpectedly */
  if (!data->finished)
    {
      data->state->stopped = TRUE;
      data->finished = TRUE;

      if (data->timer_id > 0)
        {
          g_source_remove (data->timer_id);
          data->timer_id = 0;
        }

      GtkWidget *err_dlg = gtk_message_dialog_new (NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "FFmpeg exited unexpectedly during recording.");
      gtk_dialog_run (GTK_DIALOG (err_dlg));
      gtk_widget_destroy (err_dlg);

      /* Clean up temp file */
      if (data->state->output_path)
        g_unlink (data->state->output_path);

      gtk_widget_destroy (data->window);

      if (data->callback)
        data->callback (NULL, data->user_data);

      screenshooter_recorder_free (data->state);
      g_free (data);
    }
}


void
screenshooter_recorder_dialog_run (RecorderState        *state,
                                    RecorderStopCallback  callback,
                                    gpointer              user_data)
{
  RecorderDialogData *data;
  GtkWidget *window, *hbox, *stop_btn, *timer_label;
  GdkDisplay *display;
  GdkMonitor *monitor;
  GdkRectangle geometry;

  data = g_new0 (RecorderDialogData, 1);
  data->state = state;
  data->callback = callback;
  data->user_data = user_data;
  data->elapsed = 0;
  data->finished = FALSE;

  /* Create floating window */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "Recording");
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (window), 8);
  data->window = window;

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_container_add (GTK_CONTAINER (window), hbox);

  /* Red circle indicator */
  timer_label = gtk_label_new ("00:00");
  gtk_widget_set_margin_start (timer_label, 4);
  gtk_widget_set_margin_end (timer_label, 4);
  gtk_box_pack_start (GTK_BOX (hbox), timer_label, FALSE, FALSE, 0);
  data->timer_label = timer_label;

  stop_btn = gtk_button_new_with_label ("Stop Recording");
  gtk_box_pack_start (GTK_BOX (hbox), stop_btn, FALSE, FALSE, 0);

  /* Style the stop button red */
  GtkCssProvider *css = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (css,
    "button { background: #e74c3c; color: white; font-weight: bold; }", -1, NULL);
  gtk_style_context_add_provider (
    gtk_widget_get_style_context (stop_btn),
    GTK_STYLE_PROVIDER (css),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (css);

  /* Position in top-right corner */
  display = gdk_display_get_default ();
  monitor = gdk_display_get_primary_monitor (display);
  if (monitor == NULL)
    monitor = gdk_display_get_monitor (display, 0);
  gdk_monitor_get_geometry (monitor, &geometry);

  gtk_widget_show_all (window);

  /* Move after show to get the allocated size */
  GtkRequisition req;
  gtk_widget_get_preferred_size (window, NULL, &req);
  gtk_window_move (GTK_WINDOW (window),
                   geometry.x + geometry.width - req.width - 20,
                   geometry.y + 20);

  /* Connect signals */
  g_signal_connect (stop_btn, "clicked",
                    G_CALLBACK (cb_stop_clicked), data);
  g_signal_connect (window, "key-press-event",
                    G_CALLBACK (cb_key_press), data);
  g_signal_connect (window, "delete-event",
                    G_CALLBACK (cb_delete_event), data);

  /* Start timer */
  data->timer_id = g_timeout_add (1000, cb_timer_tick, data);

  /* Watch ffmpeg child process */
  state->child_watch_id = g_child_watch_add (state->pid,
                                              cb_child_watch, data);
}
