#include "screenshooter-video-editor-timeline.h"

#define SCRUB_DEBOUNCE_MS 200


static gchar *
format_time (gdouble seconds)
{
  gint total = (gint) seconds;
  gint mins = total / 60;
  gint secs = total % 60;
  return g_strdup_printf ("%02d:%02d", mins, secs);
}


static void rebuild_regions_list (VideoEditorTimeline *tl);


static gboolean
cb_scrub_timeout (gpointer user_data)
{
  VideoEditorTimeline *tl = user_data;
  tl->scrub_timeout_id = 0;

  if (tl->scrub_cb)
    tl->scrub_cb (tl->current_time, tl->scrub_data);

  return G_SOURCE_REMOVE;
}


static void
cb_slider_value_changed (GtkRange *range, VideoEditorTimeline *tl)
{
  tl->current_time = gtk_range_get_value (range);

  gchar *cur = format_time (tl->current_time);
  gchar *total = format_time (tl->state->duration);
  gchar *label = g_strdup_printf ("%s / %s", cur, total);
  gtk_label_set_text (GTK_LABEL (tl->time_label), label);
  g_free (cur);
  g_free (total);
  g_free (label);

  /* Debounce scrub */
  if (tl->scrub_timeout_id > 0)
    g_source_remove (tl->scrub_timeout_id);
  tl->scrub_timeout_id = g_timeout_add (SCRUB_DEBOUNCE_MS, cb_scrub_timeout, tl);
}


static void
cb_blur_slider_changed (GtkRange *range, VideoEditorTimeline *tl)
{
  tl->state->blur_radius = (gint) gtk_range_get_value (range);
  gchar *label = g_strdup_printf ("%d", tl->state->blur_radius);
  gtk_label_set_text (GTK_LABEL (tl->blur_label), label);
  g_free (label);
}


static void
cb_delete_region (GtkButton *button, gpointer user_data)
{
  VideoEditorTimeline *tl;
  guint index;

  tl = g_object_get_data (G_OBJECT (button), "timeline");
  index = GPOINTER_TO_UINT (user_data);

  video_editor_remove_region (tl->state, index);
  rebuild_regions_list (tl);

  if (tl->region_changed_cb)
    tl->region_changed_cb (tl->region_changed_data);
}


/* Parse mm:ss text to seconds */
static gdouble
parse_time_text (const gchar *text)
{
  gint mins = 0, secs = 0;
  if (sscanf (text, "%d:%d", &mins, &secs) == 2)
    return mins * 60.0 + secs;
  else if (sscanf (text, "%d", &secs) == 1)
    return (gdouble) secs;
  return 0;
}


/* Commit time entry text to the BlurRegion */
static void
commit_time_entry (GtkEntry *entry, gboolean is_end)
{
  VideoEditorTimeline *tl = g_object_get_data (G_OBJECT (entry), "timeline");
  guint index = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (entry), "region-index"));

  BlurRegion *r = g_list_nth_data (tl->state->regions, index);
  if (r == NULL)
    return;

  gdouble val = parse_time_text (gtk_entry_get_text (entry));
  val = CLAMP (val, 0, tl->state->duration);

  if (is_end)
    r->end_time = val;
  else
    r->start_time = val;

  /* Reformat to canonical mm:ss */
  gchar *formatted = format_time (val);
  gtk_entry_set_text (entry, formatted);
  g_free (formatted);
}


static void
cb_start_entry_activate (GtkEntry *entry, gpointer user_data)
{
  commit_time_entry (entry, FALSE);
}


static gboolean
cb_start_entry_focus_out (GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
  commit_time_entry (GTK_ENTRY (widget), FALSE);
  return FALSE;
}


static void
cb_end_entry_activate (GtkEntry *entry, gpointer user_data)
{
  commit_time_entry (entry, TRUE);
}


static gboolean
cb_end_entry_focus_out (GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
  commit_time_entry (GTK_ENTRY (widget), TRUE);
  return FALSE;
}


static void
cb_fullframe_toggled (GtkToggleButton *toggle, VideoEditorTimeline *tl)
{
  gboolean active = gtk_toggle_button_get_active (toggle);

  if (active)
    {
      /* Remove all existing regions and add a full-frame one */
      g_list_free_full (tl->state->regions, (GDestroyNotify) blur_region_free);
      tl->state->regions = NULL;

      BlurRegion *r = blur_region_new (0, 0, 0, 0,
                                       tl->current_time,
                                       tl->state->duration,
                                       TRUE);
      video_editor_add_region (tl->state, r);
      rebuild_regions_list (tl);
    }

  /* Disable add button and drawing when full-frame is active */
  gtk_widget_set_sensitive (tl->add_button, !active);

  if (tl->region_changed_cb)
    tl->region_changed_cb (tl->region_changed_data);
}


static void
cb_add_region (GtkButton *button, VideoEditorTimeline *tl)
{
  /* Add a placeholder region at center of video, current time -> end */
  gint w = tl->state->video_width / 4;
  gint h = tl->state->video_height / 4;
  gint x = (tl->state->video_width - w) / 2;
  gint y = (tl->state->video_height - h) / 2;

  BlurRegion *r = blur_region_new (x, y, w, h,
                                   tl->current_time,
                                   tl->state->duration,
                                   FALSE);
  if (video_editor_add_region (tl->state, r))
    {
      rebuild_regions_list (tl);
      if (tl->region_changed_cb)
        tl->region_changed_cb (tl->region_changed_data);
    }
  else
    {
      blur_region_free (r);
    }
}


static void
cb_export_config (GtkButton *button, VideoEditorTimeline *tl)
{
  GtkWidget *dlg = gtk_file_chooser_dialog_new ("Save Blur Config",
    GTK_WINDOW (gtk_widget_get_toplevel (tl->container)),
    GTK_FILE_CHOOSER_ACTION_SAVE,
    "_Cancel", GTK_RESPONSE_CANCEL,
    "_Save", GTK_RESPONSE_ACCEPT,
    NULL);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dlg), TRUE);
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dlg), "blur-config.json");

  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "JSON (*.json)");
  gtk_file_filter_add_pattern (filter, "*.json");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dlg), filter);

  if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_ACCEPT)
    {
      gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dlg));
      GError *error = NULL;
      if (!video_editor_save_config (tl->state, path, &error))
        {
          GtkWidget *err = gtk_message_dialog_new (
            GTK_WINDOW (gtk_widget_get_toplevel (tl->container)),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Failed to save config: %s", error->message);
          gtk_dialog_run (GTK_DIALOG (err));
          gtk_widget_destroy (err);
          g_error_free (error);
        }
      g_free (path);
    }
  gtk_widget_destroy (dlg);
}


static void
cb_import_config (GtkButton *button, VideoEditorTimeline *tl)
{
  GtkWidget *dlg = gtk_file_chooser_dialog_new ("Load Blur Config",
    GTK_WINDOW (gtk_widget_get_toplevel (tl->container)),
    GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Cancel", GTK_RESPONSE_CANCEL,
    "_Open", GTK_RESPONSE_ACCEPT,
    NULL);

  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "JSON (*.json)");
  gtk_file_filter_add_pattern (filter, "*.json");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dlg), filter);

  if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_ACCEPT)
    {
      gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dlg));
      GError *error = NULL;
      if (video_editor_load_config (tl->state, path, &error))
        {
          /* Update blur slider */
          gtk_range_set_value (GTK_RANGE (tl->blur_slider), tl->state->blur_radius);
          rebuild_regions_list (tl);
          if (tl->region_changed_cb)
            tl->region_changed_cb (tl->region_changed_data);
        }
      else
        {
          GtkWidget *err = gtk_message_dialog_new (
            GTK_WINDOW (gtk_widget_get_toplevel (tl->container)),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Failed to load config: %s", error->message);
          gtk_dialog_run (GTK_DIALOG (err));
          gtk_widget_destroy (err);
          g_error_free (error);
        }
      g_free (path);
    }
  gtk_widget_destroy (dlg);
}


static void
rebuild_regions_list (VideoEditorTimeline *tl)
{
  /* Clear existing children */
  GList *children = gtk_container_get_children (GTK_CONTAINER (tl->regions_box));
  for (GList *l = children; l != NULL; l = l->next)
    gtk_widget_destroy (GTK_WIDGET (l->data));
  g_list_free (children);

  guint idx = 0;
  for (GList *l = tl->state->regions; l != NULL; l = l->next, idx++)
    {
      BlurRegion *r = l->data;
      GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

      /* Region label */
      gchar *desc;
      if (r->full_frame)
        desc = g_strdup_printf ("#%u  [Full Frame]", idx + 1);
      else
        desc = g_strdup_printf ("#%u  [%d,%d %dx%d]", idx + 1,
                                r->x, r->y, r->w, r->h);
      GtkWidget *label = gtk_label_new (desc);
      gtk_widget_set_halign (label, GTK_ALIGN_START);
      gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
      g_free (desc);

      /* Start time entry (mm:ss format) */
      gchar *start_text = format_time (r->start_time);
      GtkWidget *start_entry = gtk_entry_new ();
      gtk_entry_set_text (GTK_ENTRY (start_entry), start_text);
      gtk_entry_set_width_chars (GTK_ENTRY (start_entry), 5);
      gtk_widget_set_size_request (start_entry, 70, -1);
      g_object_set_data (G_OBJECT (start_entry), "timeline", tl);
      g_object_set_data (G_OBJECT (start_entry), "region-index", GUINT_TO_POINTER (idx));
      g_signal_connect (start_entry, "activate",
                        G_CALLBACK (cb_start_entry_activate), NULL);
      g_signal_connect (start_entry, "focus-out-event",
                        G_CALLBACK (cb_start_entry_focus_out), NULL);
      gtk_box_pack_start (GTK_BOX (row), start_entry, FALSE, FALSE, 0);
      g_free (start_text);

      GtkWidget *arrow = gtk_label_new ("->");
      gtk_box_pack_start (GTK_BOX (row), arrow, FALSE, FALSE, 0);

      /* End time entry (mm:ss format) */
      gchar *end_text = format_time (r->end_time);
      GtkWidget *end_entry = gtk_entry_new ();
      gtk_entry_set_text (GTK_ENTRY (end_entry), end_text);
      gtk_entry_set_width_chars (GTK_ENTRY (end_entry), 5);
      gtk_widget_set_size_request (end_entry, 70, -1);
      g_object_set_data (G_OBJECT (end_entry), "timeline", tl);
      g_object_set_data (G_OBJECT (end_entry), "region-index", GUINT_TO_POINTER (idx));
      g_signal_connect (end_entry, "activate",
                        G_CALLBACK (cb_end_entry_activate), NULL);
      g_signal_connect (end_entry, "focus-out-event",
                        G_CALLBACK (cb_end_entry_focus_out), NULL);
      gtk_box_pack_start (GTK_BOX (row), end_entry, FALSE, FALSE, 0);
      g_free (end_text);

      /* Delete button */
      GtkWidget *del_btn = gtk_button_new_from_icon_name ("edit-delete-symbolic",
                                                          GTK_ICON_SIZE_BUTTON);
      g_object_set_data (G_OBJECT (del_btn), "timeline", tl);
      g_signal_connect (del_btn, "clicked",
                        G_CALLBACK (cb_delete_region), GUINT_TO_POINTER (idx));
      gtk_box_pack_end (GTK_BOX (row), del_btn, FALSE, FALSE, 0);

      gtk_box_pack_start (GTK_BOX (tl->regions_box), row, FALSE, FALSE, 0);
    }

  /* Update add button and checkbox (may be NULL during construction) */
  if (tl->add_button != NULL && tl->fullframe_check != NULL)
    {
      guint n = g_list_length (tl->state->regions);
      gtk_widget_set_sensitive (tl->add_button, n < VIDEO_EDITOR_MAX_REGIONS &&
                                !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (tl->fullframe_check)));

      gboolean has_fullframe = FALSE;
      for (GList *l = tl->state->regions; l != NULL; l = l->next)
        {
          if (((BlurRegion *) l->data)->full_frame)
            {
              has_fullframe = TRUE;
              break;
            }
        }
      g_signal_handlers_block_by_func (tl->fullframe_check, cb_fullframe_toggled, tl);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tl->fullframe_check), has_fullframe);
      g_signal_handlers_unblock_by_func (tl->fullframe_check, cb_fullframe_toggled, tl);
    }

  gtk_widget_show_all (tl->regions_box);
}


VideoEditorTimeline *
video_editor_timeline_new (VideoEditorState *state)
{
  VideoEditorTimeline *tl = g_new0 (VideoEditorTimeline, 1);
  tl->state = state;
  tl->current_time = 0;
  tl->scrub_timeout_id = 0;

  tl->container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start (tl->container, 12);
  gtk_widget_set_margin_end (tl->container, 12);
  gtk_widget_set_margin_bottom (tl->container, 8);

  /* Timeline slider row */
  GtkWidget *slider_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  tl->slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                         0, state->duration, 0.1);
  gtk_scale_set_draw_value (GTK_SCALE (tl->slider), FALSE);
  gtk_widget_set_hexpand (tl->slider, TRUE);
  g_signal_connect (tl->slider, "value-changed",
                    G_CALLBACK (cb_slider_value_changed), tl);
  gtk_box_pack_start (GTK_BOX (slider_row), tl->slider, TRUE, TRUE, 0);

  gchar *total = format_time (state->duration);
  gchar *init_label = g_strdup_printf ("00:00 / %s", total);
  tl->time_label = gtk_label_new (init_label);
  g_free (total);
  g_free (init_label);
  gtk_box_pack_end (GTK_BOX (slider_row), tl->time_label, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (tl->container), slider_row, FALSE, FALSE, 0);

  /* Blur Regions frame with scrolled list + button below */
  GtkWidget *regions_frame = gtk_frame_new ("Blur Regions");
  GtkWidget *frame_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_container_set_border_width (GTK_CONTAINER (frame_box), 6);
  gtk_container_add (GTK_CONTAINER (regions_frame), frame_box);

  GtkWidget *scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled, -1, 150);
  gtk_box_pack_start (GTK_BOX (frame_box), scrolled, TRUE, TRUE, 0);

  tl->regions_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_container_add (GTK_CONTAINER (scrolled), tl->regions_box);

  /* Button row (outside regions_box so rebuild doesn't destroy it) */
  GtkWidget *btn_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

  GtkWidget *import_btn = gtk_button_new_with_label ("Import");
  g_signal_connect (import_btn, "clicked", G_CALLBACK (cb_import_config), tl);
  gtk_box_pack_start (GTK_BOX (btn_row), import_btn, FALSE, FALSE, 0);

  GtkWidget *export_btn = gtk_button_new_with_label ("Export");
  g_signal_connect (export_btn, "clicked", G_CALLBACK (cb_export_config), tl);
  gtk_box_pack_start (GTK_BOX (btn_row), export_btn, FALSE, FALSE, 0);

  tl->add_button = gtk_button_new_with_label ("+ Add Region");
  g_signal_connect (tl->add_button, "clicked",
                    G_CALLBACK (cb_add_region), tl);
  gtk_box_pack_end (GTK_BOX (btn_row), tl->add_button, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (frame_box), btn_row, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (tl->container), regions_frame, FALSE, FALSE, 0);

  /* Blur intensity row */
  GtkWidget *blur_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *blur_label_text = gtk_label_new ("Blur intensity:");
  gtk_box_pack_start (GTK_BOX (blur_row), blur_label_text, FALSE, FALSE, 0);

  tl->blur_slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                              VIDEO_EDITOR_MIN_BLUR,
                                              VIDEO_EDITOR_MAX_BLUR, 1);
  gtk_range_set_value (GTK_RANGE (tl->blur_slider), state->blur_radius);
  gtk_scale_set_draw_value (GTK_SCALE (tl->blur_slider), FALSE);
  gtk_widget_set_hexpand (tl->blur_slider, TRUE);
  g_signal_connect (tl->blur_slider, "value-changed",
                    G_CALLBACK (cb_blur_slider_changed), tl);
  gtk_box_pack_start (GTK_BOX (blur_row), tl->blur_slider, TRUE, TRUE, 0);

  gchar *blur_val = g_strdup_printf ("%d", state->blur_radius);
  tl->blur_label = gtk_label_new (blur_val);
  g_free (blur_val);
  gtk_box_pack_end (GTK_BOX (blur_row), tl->blur_label, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (tl->container), blur_row, FALSE, FALSE, 0);

  /* Full frame checkbox */
  tl->fullframe_check = gtk_check_button_new_with_label ("Full frame blur");
  g_signal_connect (tl->fullframe_check, "toggled",
                    G_CALLBACK (cb_fullframe_toggled), tl);
  gtk_box_pack_start (GTK_BOX (tl->container), tl->fullframe_check, FALSE, FALSE, 0);

  /* Initial regions list build */
  rebuild_regions_list (tl);

  return tl;
}


void
video_editor_timeline_free (VideoEditorTimeline *tl)
{
  if (tl == NULL)
    return;
  if (tl->scrub_timeout_id > 0)
    g_source_remove (tl->scrub_timeout_id);
  g_free (tl);
}


void
video_editor_timeline_refresh (VideoEditorTimeline *tl)
{
  rebuild_regions_list (tl);
}


gdouble
video_editor_timeline_get_time (VideoEditorTimeline *tl)
{
  return tl->current_time;
}


void
video_editor_timeline_set_scrub_callback (VideoEditorTimeline *tl,
                                          TimelineScrubCallback cb,
                                          gpointer user_data)
{
  tl->scrub_cb = cb;
  tl->scrub_data = user_data;
}


void
video_editor_timeline_set_region_changed_callback (VideoEditorTimeline *tl,
                                                   TimelineRegionChanged cb,
                                                   gpointer user_data)
{
  tl->region_changed_cb = cb;
  tl->region_changed_data = user_data;
}
