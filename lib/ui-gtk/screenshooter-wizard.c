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

  /* Jira entries */
  GtkWidget *jira_base_url;
  GtkWidget *jira_email;
  GtkWidget *jira_api_token;
  GtkWidget *jira_default_project;
  GtkWidget *jira_result_label;

  /* Summary label */
  GtkWidget *summary_label;
} WizardData;



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
  const gchar *base_url  = gtk_entry_get_text (GTK_ENTRY (wd->jira_base_url));
  const gchar *email     = gtk_entry_get_text (GTK_ENTRY (wd->jira_email));
  const gchar *api_token = gtk_entry_get_text (GTK_ENTRY (wd->jira_api_token));

  return (base_url && *base_url &&
          email && *email &&
          api_token && *api_token);
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

  g_free (config->jira.base_url);
  g_free (config->jira.email);
  g_free (config->jira.api_token);
  g_free (config->jira.default_project);

  config->jira.base_url        = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->jira_base_url)));
  config->jira.email           = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->jira_email)));
  config->jira.api_token       = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->jira_api_token)));
  config->jira.default_project = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->jira_default_project)));

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
  CloudConfig *config;
  GError *error = NULL;
  gboolean success;

  config = build_config_from_entries (wd);
  success = sc_jira_test_connection (config, &error);

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

  sc_cloud_config_free (config);
}



static void
cb_wizard_prepare (GtkAssistant *assistant, GtkWidget *page, WizardData *wd)
{
  gint current_page = gtk_assistant_get_current_page (assistant);
  gint num_pages    = gtk_assistant_get_n_pages (assistant);

  /* Update summary on the last page */
  if (current_page == num_pages - 1)
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



static GtkWidget *
create_jira_page (WizardData *wd)
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

  add_grid_entry (GTK_GRID (grid), row++, _("Base URL:"),            &wd->jira_base_url,        TRUE);
  add_grid_entry (GTK_GRID (grid), row++, _("Email:"),               &wd->jira_email,           TRUE);
  add_grid_entry (GTK_GRID (grid), row++, _("API Token:"),           &wd->jira_api_token,       FALSE);
  add_grid_entry (GTK_GRID (grid), row++, _("Default Project Key:"), &wd->jira_default_project, TRUE);

  test_button = gtk_button_new_with_label (_("Test Connection"));
  gtk_grid_attach (GTK_GRID (grid), test_button, 0, row, 1, 1);

  wd->jira_result_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (wd->jira_result_label), 0.0);
  gtk_grid_attach (GTK_GRID (grid), wd->jira_result_label, 1, row, 1, 1);

  g_signal_connect (test_button, "clicked", G_CALLBACK (cb_jira_test_connection), wd);

  return grid;
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
  g_main_loop_run (wd.loop);
  g_main_loop_unref (wd.loop);

  return wd.completed;
}
