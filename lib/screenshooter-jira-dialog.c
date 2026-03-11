#include "screenshooter-jira-dialog.h"
#include "screenshooter-jira.h"

#include <libxfce4ui/libxfce4ui.h>
#include <string.h>


typedef struct {
  const CloudConfig *config;
  const gchar *image_url;
  GtkWidget *search_entry;
  GtkWidget *list_box;
  GtkWidget *preset_combo;
  GtkWidget *desc_view;
  GtkWidget *post_button;
  gchar *selected_key;
  guint search_timeout_id;
} JiraDialogData;


static void
clear_list_box (GtkListBox *list_box)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (list_box));
  for (GList *l = children; l != NULL; l = l->next)
    gtk_widget_destroy (GTK_WIDGET (l->data));
  g_list_free (children);
}


static void
cb_row_selected (GtkListBox *box, GtkListBoxRow *row, JiraDialogData *data)
{
  if (row == NULL)
    {
      g_free (data->selected_key);
      data->selected_key = NULL;
      gtk_widget_set_sensitive (data->post_button, FALSE);
      return;
    }

  const gchar *key = g_object_get_data (G_OBJECT (row), "issue-key");
  g_free (data->selected_key);
  data->selected_key = g_strdup (key);
  gtk_widget_set_sensitive (data->post_button, TRUE);
}


static void
populate_results (JiraDialogData *data, GList *issues)
{
  clear_list_box (GTK_LIST_BOX (data->list_box));

  for (GList *l = issues; l != NULL; l = l->next)
    {
      JiraIssue *issue = l->data;
      gchar *label_text = g_strdup_printf ("%s  —  %s",
                                            issue->key, issue->summary);
      GtkWidget *label = gtk_label_new (label_text);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
      gtk_widget_set_margin_start (label, 6);
      gtk_widget_set_margin_end (label, 6);
      gtk_widget_set_margin_top (label, 4);
      gtk_widget_set_margin_bottom (label, 4);

      GtkWidget *row = gtk_list_box_row_new ();
      g_object_set_data_full (G_OBJECT (row), "issue-key",
                              g_strdup (issue->key), g_free);
      gtk_container_add (GTK_CONTAINER (row), label);
      gtk_list_box_insert (GTK_LIST_BOX (data->list_box), row, -1);
      g_free (label_text);
    }

  gtk_widget_show_all (data->list_box);
}


static gboolean
do_search (gpointer user_data)
{
  JiraDialogData *data = user_data;
  const gchar *query = gtk_entry_get_text (GTK_ENTRY (data->search_entry));
  GError *error = NULL;

  data->search_timeout_id = 0;

  GList *issues = screenshooter_jira_search (data->config, query, &error);
  if (error)
    {
      g_warning ("Jira search error: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  populate_results (data, issues);
  screenshooter_jira_issue_list_free (issues);
  return FALSE;
}


static void
cb_search_changed (GtkSearchEntry *entry, JiraDialogData *data)
{
  if (data->search_timeout_id > 0)
    g_source_remove (data->search_timeout_id);
  data->search_timeout_id = g_timeout_add (300, do_search, data);
}


gboolean
screenshooter_jira_dialog_run (GtkWindow *parent,
                                const CloudConfig *config,
                                const gchar *image_url)
{
  GtkWidget *dlg, *content, *box, *label, *scrolled;
  GtkWidget *search_entry, *list_box, *preset_combo, *desc_scrolled, *desc_view;
  JiraDialogData data = { 0 };
  gint response;
  gboolean result = FALSE;

  data.config = config;
  data.image_url = image_url;

  dlg = xfce_titled_dialog_new_with_mixed_buttons (
    "Post to Jira",
    parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "", "_Cancel", GTK_RESPONSE_CANCEL,
    "", "_Post", GTK_RESPONSE_OK,
    NULL);

  gtk_window_set_default_size (GTK_WINDOW (dlg), 500, 450);
  gtk_window_set_icon_name (GTK_WINDOW (dlg), "org.xfce.screenshooter");

  data.post_button = gtk_dialog_get_widget_for_response (
    GTK_DIALOG (dlg), GTK_RESPONSE_OK);
  gtk_widget_set_sensitive (data.post_button, FALSE);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dlg));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_box_pack_start (GTK_BOX (content), box, TRUE, TRUE, 0);

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
    "<span weight=\"bold\">Search Issues</span>");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  search_entry = gtk_search_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (search_entry),
    "Type to search issues...");
  gtk_box_pack_start (GTK_BOX (box), search_entry, FALSE, FALSE, 0);
  data.search_entry = search_entry;

  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height (
    GTK_SCROLLED_WINDOW (scrolled), 180);
  gtk_widget_set_vexpand (scrolled, TRUE);
  gtk_box_pack_start (GTK_BOX (box), scrolled, TRUE, TRUE, 0);

  list_box = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_box),
    GTK_SELECTION_SINGLE);
  gtk_container_add (GTK_CONTAINER (scrolled), list_box);
  data.list_box = list_box;

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
    "<span weight=\"bold\">Preset</span>");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  preset_combo = gtk_combo_box_text_new ();
  if (config->presets.bug_evidence && config->presets.bug_evidence[0] != '\0')
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (preset_combo),
      "bug", config->presets.bug_evidence);
  if (config->presets.work_evidence && config->presets.work_evidence[0] != '\0')
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (preset_combo),
      "work", config->presets.work_evidence);
  gtk_combo_box_set_active (GTK_COMBO_BOX (preset_combo), 0);
  gtk_box_pack_start (GTK_BOX (box), preset_combo, FALSE, FALSE, 0);
  data.preset_combo = preset_combo;

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
    "<span weight=\"bold\">Description</span>");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  desc_scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (desc_scrolled),
    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height (
    GTK_SCROLLED_WINDOW (desc_scrolled), 60);
  gtk_box_pack_start (GTK_BOX (box), desc_scrolled, FALSE, FALSE, 0);

  desc_view = gtk_text_view_new ();
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (desc_view), GTK_WRAP_WORD_CHAR);
  gtk_container_add (GTK_CONTAINER (desc_scrolled), desc_view);
  data.desc_view = desc_view;

  g_signal_connect (search_entry, "search-changed",
    G_CALLBACK (cb_search_changed), &data);
  g_signal_connect (list_box, "row-selected",
    G_CALLBACK (cb_row_selected), &data);

  gtk_widget_show_all (dlg);

  do_search (&data);

  response = gtk_dialog_run (GTK_DIALOG (dlg));

  if (response == GTK_RESPONSE_OK && data.selected_key)
    {
      gchar *preset_title = gtk_combo_box_text_get_active_text (
        GTK_COMBO_BOX_TEXT (preset_combo));
      GtkTextBuffer *buf = gtk_text_view_get_buffer (
        GTK_TEXT_VIEW (desc_view));
      GtkTextIter start, end;
      gtk_text_buffer_get_bounds (buf, &start, &end);
      gchar *desc = gtk_text_buffer_get_text (buf, &start, &end, FALSE);
      GError *error = NULL;

      result = screenshooter_jira_post_comment (config,
        data.selected_key,
        preset_title ? preset_title : "Screenshot",
        desc, image_url, &error);

      if (error)
        {
          GtkWidget *err_dlg = gtk_message_dialog_new (
            GTK_WINDOW (dlg), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Failed to post to Jira:\n%s", error->message);
          gtk_dialog_run (GTK_DIALOG (err_dlg));
          gtk_widget_destroy (err_dlg);
          g_error_free (error);
        }

      g_free (desc);
      g_free (preset_title);
    }

  if (data.search_timeout_id > 0)
    g_source_remove (data.search_timeout_id);
  g_free (data.selected_key);
  gtk_widget_destroy (dlg);
  return result;
}
