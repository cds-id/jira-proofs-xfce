/*
 *  Copyright © 2024 Xfce Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "screenshooter-wizard.h"
#include <sc-cloud-config.h>
#include <sc-r2.h>
#include <sc-jira.h>

#include <libxfce4util/libxfce4util.h>


typedef struct {
  GtkAssistant *assistant;
  GMainLoop    *loop;
  gboolean      completed;
  const gchar  *config_dir;

  /* R2 entries */
  GtkWidget *r2_account_id;
  GtkWidget *r2_access_key_id;
  GtkWidget *r2_secret_access_key;
  GtkWidget *r2_bucket;
  GtkWidget *r2_public_url;
  GtkWidget *r2_result_label;

  /* Jira credentials */
  GtkWidget *jira_email;
  GtkWidget *jira_api_token;
  GtkWidget *jira_result_label;

  /* Jira workspaces */
  GtkWidget *jira_ws_list_box;
  GPtrArray *jira_ws_entries;

  /* Summary label */
  GtkWidget *summary_label;
} WizardData;


typedef struct {
  GtkWidget *label_entry;
  GtkWidget *url_entry;
  GtkWidget *project_entry;
  GtkWidget *row;
} WizardWorkspaceEntry;



static gboolean
has_r2_config (WizardData *wd)
{
  const gchar *account_id = gtk_entry_get_text (GTK_ENTRY (wd->r2_account_id));
  const gchar *access_key = gtk_entry_get_text (GTK_ENTRY (wd->r2_access_key_id));
  const gchar *secret_key = gtk_entry_get_text (GTK_ENTRY (wd->r2_secret_access_key));
  const gchar *bucket     = gtk_entry_get_text (GTK_ENTRY (wd->r2_bucket));

  return (account_id && *account_id &&
          access_key && *access_key &&
          secret_key && *secret_key &&
          bucket && *bucket);
}



static gboolean
has_jira_config (WizardData *wd)
{
  const gchar *email     = gtk_entry_get_text (GTK_ENTRY (wd->jira_email));
  const gchar *api_token = gtk_entry_get_text (GTK_ENTRY (wd->jira_api_token));

  if (!email || !*email || !api_token || !*api_token)
    return FALSE;

  if (wd->jira_ws_entries->len == 0)
    return FALSE;

  for (guint i = 0; i < wd->jira_ws_entries->len; i++)
    {
      WizardWorkspaceEntry *e = g_ptr_array_index (wd->jira_ws_entries, i);
      const gchar *label = gtk_entry_get_text (GTK_ENTRY (e->label_entry));
      const gchar *url   = gtk_entry_get_text (GTK_ENTRY (e->url_entry));
      if (!label || !*label || !url || !*url)
        return FALSE;
    }

  return TRUE;
}



static CloudConfig *
build_config_from_entries (WizardData *wd)
{
  CloudConfig *config = sc_cloud_config_create_default ();

  g_free (config->r2.account_id);
  g_free (config->r2.access_key_id);
  g_free (config->r2.secret_access_key);
  g_free (config->r2.bucket);
  g_free (config->r2.public_url);

  config->r2.account_id        = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->r2_account_id)));
  config->r2.access_key_id     = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->r2_access_key_id)));
  config->r2.secret_access_key = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->r2_secret_access_key)));
  config->r2.bucket            = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->r2_bucket)));
  config->r2.public_url        = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->r2_public_url)));

  g_free (config->jira.email);
  g_free (config->jira.api_token);

  config->jira.email     = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->jira_email)));
  config->jira.api_token = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->jira_api_token)));

  config->jira.n_workspaces = wd->jira_ws_entries->len;
  config->jira.workspaces = g_new0 (JiraWorkspace, wd->jira_ws_entries->len);
  for (guint i = 0; i < wd->jira_ws_entries->len; i++)
    {
      WizardWorkspaceEntry *e = g_ptr_array_index (wd->jira_ws_entries, i);
      config->jira.workspaces[i].label =
        g_strdup (gtk_entry_get_text (GTK_ENTRY (e->label_entry)));
      config->jira.workspaces[i].base_url =
        g_strdup (gtk_entry_get_text (GTK_ENTRY (e->url_entry)));
      config->jira.workspaces[i].default_project =
        g_strdup (gtk_entry_get_text (GTK_ENTRY (e->project_entry)));
    }

  config->loaded = TRUE;

  return config;
}



static void
update_summary (WizardData *wd)
{
  gchar *text;

  text = g_strdup_printf ("%s: %s\n%s: %s",
                          _("R2"),
                          has_r2_config (wd) ? _("configured") : _("not configured"),
                          _("Jira"),
                          has_jira_config (wd) ? _("configured") : _("not configured"));

  gtk_label_set_text (GTK_LABEL (wd->summary_label), text);
  g_free (text);
}



static void
cb_r2_test_connection (GtkButton *button, WizardData *wd)
{
  CloudConfig *config;
  GError *error = NULL;
  gboolean success;

  config = build_config_from_entries (wd);
  success = sc_r2_test_connection (config, &error);

  if (success)
    {
      gtk_label_set_markup (GTK_LABEL (wd->r2_result_label),
                            "<span foreground=\"green\">Connected!</span>");
    }
  else
    {
      gchar *markup = g_markup_printf_escaped (
          "<span foreground=\"red\">%s</span>",
          error ? error->message : _("Unknown error"));
      gtk_label_set_markup (GTK_LABEL (wd->r2_result_label), markup);
      g_free (markup);
      g_clear_error (&error);
    }

  sc_cloud_config_free (config);
}



static void
cb_jira_test_connection (GtkButton *button, WizardData *wd)
{
  const gchar *email = gtk_entry_get_text (GTK_ENTRY (wd->jira_email));
  const gchar *api_token = gtk_entry_get_text (GTK_ENTRY (wd->jira_api_token));
  GError *error = NULL;
  gboolean success = FALSE;

  if (wd->jira_ws_entries->len > 0)
    {
      WizardWorkspaceEntry *e = g_ptr_array_index (wd->jira_ws_entries, 0);
      JiraWorkspace ws = {
        .label = (gchar *) gtk_entry_get_text (GTK_ENTRY (e->label_entry)),
        .base_url = (gchar *) gtk_entry_get_text (GTK_ENTRY (e->url_entry)),
        .default_project = (gchar *) gtk_entry_get_text (GTK_ENTRY (e->project_entry)),
      };
      success = sc_jira_test_connection (email, api_token, &ws, &error);
    }
  else
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Add at least one workspace first");
    }

  if (success)
    {
      gtk_label_set_markup (GTK_LABEL (wd->jira_result_label),
                            "<span foreground=\"green\">Connected!</span>");
    }
  else
    {
      gchar *markup = g_markup_printf_escaped (
          "<span foreground=\"red\">%s</span>",
          error ? error->message : _("Unknown error"));
      gtk_label_set_markup (GTK_LABEL (wd->jira_result_label), markup);
      g_free (markup);
      g_clear_error (&error);
    }
}



static void
cb_wizard_prepare (GtkAssistant *assistant, GtkWidget *page, WizardData *wd)
{
  gint current_page = gtk_assistant_get_current_page (assistant);
  gint num_pages    = gtk_assistant_get_n_pages (assistant);

  /* Update summary on the last page */
  if (num_pages > 0 && current_page >= 0 && current_page == num_pages - 1)
    update_summary (wd);
}



static void
cb_wizard_apply (GtkAssistant *assistant, WizardData *wd)
{
  CloudConfig *config;
  GError *error = NULL;

  config = build_config_from_entries (wd);

  if (!sc_cloud_config_save (config, wd->config_dir, &error))
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new (GTK_WINDOW (assistant),
                                       GTK_DIALOG_MODAL,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_OK,
                                       _("Failed to save configuration: %s"),
                                       error ? error->message : _("Unknown error"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      g_clear_error (&error);
    }

  sc_cloud_config_free (config);

  wd->completed = TRUE;
  gtk_widget_destroy (GTK_WIDGET (assistant));
}



static void
cb_wizard_cancel (GtkAssistant *assistant, WizardData *wd)
{
  CloudConfig *config;
  GError *error = NULL;

  /* Save empty default config so wizard doesn't re-trigger */
  config = sc_cloud_config_create_default ();
  if (!sc_cloud_config_save (config, wd->config_dir, &error))
    g_clear_error (&error);
  sc_cloud_config_free (config);

  wd->completed = FALSE;
  gtk_widget_destroy (GTK_WIDGET (assistant));
}



static void
cb_wizard_close (GtkAssistant *assistant, WizardData *wd)
{
  wd->completed = TRUE;
  gtk_widget_destroy (GTK_WIDGET (assistant));
}



static void
cb_wizard_destroy (GtkWidget *widget, WizardData *wd)
{
  if (g_main_loop_is_running (wd->loop))
    g_main_loop_quit (wd->loop);
}



static GtkWidget *
create_welcome_page (WizardData *wd)
{
  GtkWidget *label;

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
      _("Welcome to Screenshooter.\n\n"
        "Let's set up your cloud services for uploading screenshots.\n"
        "You can skip this and configure later via --setup-wizard."));
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_margin_start (label, 12);
  gtk_widget_set_margin_end (label, 12);
  gtk_widget_set_margin_top (label, 12);
  gtk_widget_set_margin_bottom (label, 12);

  return label;
}



static void
add_grid_entry (GtkGrid *grid, gint row, const gchar *label_text, GtkWidget **entry, gboolean visible)
{
  GtkWidget *label;

  label = gtk_label_new (label_text);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_grid_attach (grid, label, 0, row, 1, 1);

  *entry = gtk_entry_new ();
  gtk_entry_set_visibility (GTK_ENTRY (*entry), visible);
  gtk_widget_set_hexpand (*entry, TRUE);
  gtk_grid_attach (grid, *entry, 1, row, 1, 1);
}



static GtkWidget *
create_r2_page (WizardData *wd)
{
  GtkWidget *grid;
  GtkWidget *test_button;
  gint row = 0;

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_widget_set_margin_start (grid, 12);
  gtk_widget_set_margin_end (grid, 12);
  gtk_widget_set_margin_top (grid, 12);
  gtk_widget_set_margin_bottom (grid, 12);

  add_grid_entry (GTK_GRID (grid), row++, _("Account ID:"),        &wd->r2_account_id,        TRUE);
  add_grid_entry (GTK_GRID (grid), row++, _("Access Key ID:"),     &wd->r2_access_key_id,     TRUE);
  add_grid_entry (GTK_GRID (grid), row++, _("Secret Access Key:"), &wd->r2_secret_access_key, FALSE);
  add_grid_entry (GTK_GRID (grid), row++, _("Bucket Name:"),       &wd->r2_bucket,            TRUE);
  add_grid_entry (GTK_GRID (grid), row++, _("Public URL:"),        &wd->r2_public_url,        TRUE);

  test_button = gtk_button_new_with_label (_("Test Connection"));
  gtk_grid_attach (GTK_GRID (grid), test_button, 0, row, 1, 1);

  wd->r2_result_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (wd->r2_result_label), 0.0);
  gtk_grid_attach (GTK_GRID (grid), wd->r2_result_label, 1, row, 1, 1);

  g_signal_connect (test_button, "clicked", G_CALLBACK (cb_r2_test_connection), wd);

  return grid;
}



static void
add_workspace_row (WizardData *wd, const gchar *label,
                   const gchar *url, const gchar *project)
{
  WizardWorkspaceEntry *e = g_new0 (WizardWorkspaceEntry, 1);

  GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  e->label_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (e->label_entry), "label");
  gtk_entry_set_width_chars (GTK_ENTRY (e->label_entry), 10);
  if (label)
    gtk_entry_set_text (GTK_ENTRY (e->label_entry), label);

  e->url_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (e->url_entry), "https://team.atlassian.net");
  gtk_widget_set_hexpand (e->url_entry, TRUE);
  if (url)
    gtk_entry_set_text (GTK_ENTRY (e->url_entry), url);

  e->project_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (e->project_entry), "PROJ");
  gtk_entry_set_width_chars (GTK_ENTRY (e->project_entry), 8);
  if (project)
    gtk_entry_set_text (GTK_ENTRY (e->project_entry), project);

  gtk_box_pack_start (GTK_BOX (hbox), e->label_entry, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), e->url_entry, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), e->project_entry, FALSE, FALSE, 0);

  e->row = gtk_list_box_row_new ();
  gtk_container_add (GTK_CONTAINER (e->row), hbox);
  gtk_list_box_insert (GTK_LIST_BOX (wd->jira_ws_list_box), e->row, -1);
  gtk_widget_show_all (e->row);

  g_ptr_array_add (wd->jira_ws_entries, e);
}


static void
cb_add_workspace (GtkButton *button, WizardData *wd)
{
  add_workspace_row (wd, NULL, NULL, NULL);
}


static void
cb_remove_workspace (GtkButton *button, WizardData *wd)
{
  if (wd->jira_ws_entries->len <= 1)
    return;

  GtkListBoxRow *selected = gtk_list_box_get_selected_row (
    GTK_LIST_BOX (wd->jira_ws_list_box));
  if (selected == NULL)
    return;

  for (guint i = 0; i < wd->jira_ws_entries->len; i++)
    {
      WizardWorkspaceEntry *e = g_ptr_array_index (wd->jira_ws_entries, i);
      if (e->row == GTK_WIDGET (selected))
        {
          gtk_widget_destroy (e->row);
          g_free (e);
          g_ptr_array_remove_index (wd->jira_ws_entries, i);
          break;
        }
    }
}


static GtkWidget *
create_jira_page (WizardData *wd)
{
  GtkWidget *box, *grid, *label, *scrolled, *btn_box, *add_btn, *rm_btn;
  GtkWidget *test_button;
  gint row = 0;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);

  /* Credentials section */
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), "<b>Credentials</b>");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

  add_grid_entry (GTK_GRID (grid), row++, _("Email:"),     &wd->jira_email,     TRUE);
  add_grid_entry (GTK_GRID (grid), row++, _("API Token:"), &wd->jira_api_token, FALSE);

  test_button = gtk_button_new_with_label (_("Test Connection"));
  gtk_grid_attach (GTK_GRID (grid), test_button, 0, row, 1, 1);

  wd->jira_result_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (wd->jira_result_label), 0.0);
  gtk_grid_attach (GTK_GRID (grid), wd->jira_result_label, 1, row, 1, 1);

  g_signal_connect (test_button, "clicked",
                    G_CALLBACK (cb_jira_test_connection), wd);

  /* Workspaces section */
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), "<b>Workspaces</b>");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_margin_top (label, 8);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height (
    GTK_SCROLLED_WINDOW (scrolled), 120);
  gtk_widget_set_vexpand (scrolled, TRUE);
  gtk_box_pack_start (GTK_BOX (box), scrolled, TRUE, TRUE, 0);

  wd->jira_ws_list_box = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (wd->jira_ws_list_box),
    GTK_SELECTION_SINGLE);
  gtk_container_add (GTK_CONTAINER (scrolled), wd->jira_ws_list_box);

  btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (box), btn_box, FALSE, FALSE, 0);

  add_btn = gtk_button_new_with_label (_("Add"));
  rm_btn = gtk_button_new_with_label (_("Remove"));
  gtk_box_pack_start (GTK_BOX (btn_box), add_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (btn_box), rm_btn, FALSE, FALSE, 0);

  g_signal_connect (add_btn, "clicked", G_CALLBACK (cb_add_workspace), wd);
  g_signal_connect (rm_btn, "clicked", G_CALLBACK (cb_remove_workspace), wd);

  return box;
}



static GtkWidget *
create_summary_page (WizardData *wd)
{
  wd->summary_label = gtk_label_new ("");
  gtk_label_set_line_wrap (GTK_LABEL (wd->summary_label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (wd->summary_label), 0.0);
  gtk_widget_set_margin_start (wd->summary_label, 12);
  gtk_widget_set_margin_end (wd->summary_label, 12);
  gtk_widget_set_margin_top (wd->summary_label, 12);
  gtk_widget_set_margin_bottom (wd->summary_label, 12);

  return wd->summary_label;
}



gboolean
screenshooter_wizard_run (GtkWindow *parent, const gchar *config_dir)
{
  WizardData wd = { 0 };
  GtkWidget *page;

  wd.config_dir = config_dir;
  wd.completed  = FALSE;
  wd.loop       = g_main_loop_new (NULL, FALSE);
  wd.jira_ws_entries = g_ptr_array_new ();

  wd.assistant = GTK_ASSISTANT (gtk_assistant_new ());
  gtk_window_set_title (GTK_WINDOW (wd.assistant), _("Screenshooter Setup Wizard"));
  gtk_window_set_default_size (GTK_WINDOW (wd.assistant), 500, 400);

  if (parent)
    gtk_window_set_transient_for (GTK_WINDOW (wd.assistant), parent);

  /* Page 1: Welcome */
  page = create_welcome_page (&wd);
  gtk_assistant_append_page (wd.assistant, page);
  gtk_assistant_set_page_title (wd.assistant, page, _("Welcome"));
  gtk_assistant_set_page_type (wd.assistant, page, GTK_ASSISTANT_PAGE_INTRO);
  gtk_assistant_set_page_complete (wd.assistant, page, TRUE);

  /* Page 2: R2 Setup */
  page = create_r2_page (&wd);
  gtk_assistant_append_page (wd.assistant, page);
  gtk_assistant_set_page_title (wd.assistant, page, _("R2 Cloud Storage"));
  gtk_assistant_set_page_type (wd.assistant, page, GTK_ASSISTANT_PAGE_CONTENT);
  gtk_assistant_set_page_complete (wd.assistant, page, TRUE);

  /* Page 3: Jira Setup */
  page = create_jira_page (&wd);
  gtk_assistant_append_page (wd.assistant, page);
  gtk_assistant_set_page_title (wd.assistant, page, _("Jira Integration"));
  gtk_assistant_set_page_type (wd.assistant, page, GTK_ASSISTANT_PAGE_CONTENT);
  gtk_assistant_set_page_complete (wd.assistant, page, TRUE);

  /* Page 4: Summary */
  page = create_summary_page (&wd);
  gtk_assistant_append_page (wd.assistant, page);
  gtk_assistant_set_page_title (wd.assistant, page, _("Summary"));
  gtk_assistant_set_page_type (wd.assistant, page, GTK_ASSISTANT_PAGE_CONFIRM);
  gtk_assistant_set_page_complete (wd.assistant, page, TRUE);

  /* Signals */
  g_signal_connect (wd.assistant, "prepare", G_CALLBACK (cb_wizard_prepare), &wd);
  g_signal_connect (wd.assistant, "apply",   G_CALLBACK (cb_wizard_apply),   &wd);
  g_signal_connect (wd.assistant, "cancel",  G_CALLBACK (cb_wizard_cancel),  &wd);
  g_signal_connect (wd.assistant, "close",   G_CALLBACK (cb_wizard_close),   &wd);
  g_signal_connect (wd.assistant, "destroy", G_CALLBACK (cb_wizard_destroy), &wd);

  gtk_widget_show_all (GTK_WIDGET (wd.assistant));

  /* Pre-populate from existing config if available */
  {
    GError *load_err = NULL;
    CloudConfig *existing = sc_cloud_config_load (config_dir, &load_err);
    if (existing)
      {
        if (existing->jira.email[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.jira_email), existing->jira.email);
        if (existing->jira.api_token[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.jira_api_token), existing->jira.api_token);

        for (gsize i = 0; i < existing->jira.n_workspaces; i++)
          add_workspace_row (&wd, existing->jira.workspaces[i].label,
                             existing->jira.workspaces[i].base_url,
                             existing->jira.workspaces[i].default_project);

        if (existing->r2.account_id[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_account_id), existing->r2.account_id);
        if (existing->r2.access_key_id[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_access_key_id), existing->r2.access_key_id);
        if (existing->r2.secret_access_key[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_secret_access_key), existing->r2.secret_access_key);
        if (existing->r2.bucket[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_bucket), existing->r2.bucket);
        if (existing->r2.public_url[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_public_url), existing->r2.public_url);

        sc_cloud_config_free (existing);
      }
    g_clear_error (&load_err);
  }

  /* Ensure at least one workspace row */
  if (wd.jira_ws_entries->len == 0)
    add_workspace_row (&wd, NULL, NULL, NULL);

  g_main_loop_run (wd.loop);
  g_main_loop_unref (wd.loop);

  for (guint i = 0; i < wd.jira_ws_entries->len; i++)
    g_free (g_ptr_array_index (wd.jira_ws_entries, i));
  g_ptr_array_free (wd.jira_ws_entries, TRUE);

  return wd.completed;
}
