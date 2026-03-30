#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include "sc-cloud-config.h"


static gchar *
create_temp_dir (void)
{
  gchar *tmpl = g_build_filename (g_get_tmp_dir (),
                                   "test-multi-ws-XXXXXX", NULL);
  gchar *dir = g_mkdtemp (tmpl);
  g_assert_nonnull (dir);
  return dir;
}


static void
remove_temp_dir (const gchar *dir)
{
  gchar *path = sc_cloud_config_get_path (dir);
  if (g_file_test (path, G_FILE_TEST_EXISTS))
    g_unlink (path);
  g_free (path);
  g_rmdir (dir);
}


static void
test_new_struct_has_workspaces (void)
{
  CloudConfig *config = sc_cloud_config_create_default ();

  /* New struct should have shared credentials and zero workspaces */
  g_assert_cmpstr (config->jira.email, ==, "");
  g_assert_cmpstr (config->jira.api_token, ==, "");
  g_assert_cmpuint (config->jira.n_workspaces, ==, 0);
  g_assert_null (config->jira.workspaces);

  sc_cloud_config_free (config);
}


static void
test_load_multi_workspace_format (void)
{
  gchar *dir = create_temp_dir ();
  gchar *path = sc_cloud_config_get_path (dir);
  GError *error = NULL;

  /* Write a multi-workspace config file */
  const gchar *content =
    "[jira]\n"
    "email=user@example.com\n"
    "api_token=tok123\n"
    "\n"
    "[jira.workspace.team-a]\n"
    "base_url=https://team-a.atlassian.net\n"
    "default_project=BNS\n"
    "\n"
    "[jira.workspace.team-b]\n"
    "base_url=https://team-b.atlassian.net\n"
    "default_project=OPS\n"
    "\n"
    "[r2]\n"
    "account_id=\n"
    "access_key_id=\n"
    "secret_access_key=\n"
    "bucket=\n"
    "public_url=\n";

  g_mkdir_with_parents (dir, 0700);
  g_file_set_contents (path, content, -1, &error);
  g_assert_no_error (error);

  CloudConfig *config = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (config);

  g_assert_cmpstr (config->jira.email, ==, "user@example.com");
  g_assert_cmpstr (config->jira.api_token, ==, "tok123");
  g_assert_cmpuint (config->jira.n_workspaces, ==, 2);

  /* Workspaces may be in any order, find team-a and team-b */
  gboolean found_a = FALSE, found_b = FALSE;
  for (gsize i = 0; i < config->jira.n_workspaces; i++)
    {
      if (g_strcmp0 (config->jira.workspaces[i].label, "team-a") == 0)
        {
          g_assert_cmpstr (config->jira.workspaces[i].base_url, ==,
                           "https://team-a.atlassian.net");
          g_assert_cmpstr (config->jira.workspaces[i].default_project, ==, "BNS");
          found_a = TRUE;
        }
      else if (g_strcmp0 (config->jira.workspaces[i].label, "team-b") == 0)
        {
          g_assert_cmpstr (config->jira.workspaces[i].base_url, ==,
                           "https://team-b.atlassian.net");
          g_assert_cmpstr (config->jira.workspaces[i].default_project, ==, "OPS");
          found_b = TRUE;
        }
    }
  g_assert_true (found_a);
  g_assert_true (found_b);

  sc_cloud_config_free (config);
  remove_temp_dir (dir);
  g_free (path);
  g_free (dir);
}


static void
test_save_and_load_multi_workspace (void)
{
  gchar *dir = create_temp_dir ();
  GError *error = NULL;

  CloudConfig *config = sc_cloud_config_create_default ();

  g_free (config->jira.email);
  config->jira.email = g_strdup ("user@example.com");
  g_free (config->jira.api_token);
  config->jira.api_token = g_strdup ("tok123");

  config->jira.n_workspaces = 2;
  config->jira.workspaces = g_new0 (JiraWorkspace, 2);
  config->jira.workspaces[0].label = g_strdup ("team-a");
  config->jira.workspaces[0].base_url = g_strdup ("https://team-a.atlassian.net");
  config->jira.workspaces[0].default_project = g_strdup ("BNS");
  config->jira.workspaces[1].label = g_strdup ("team-b");
  config->jira.workspaces[1].base_url = g_strdup ("https://team-b.atlassian.net");
  config->jira.workspaces[1].default_project = g_strdup ("OPS");

  config->loaded = TRUE;

  gboolean ok = sc_cloud_config_save (config, dir, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  sc_cloud_config_free (config);

  CloudConfig *loaded = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (loaded);

  g_assert_cmpstr (loaded->jira.email, ==, "user@example.com");
  g_assert_cmpstr (loaded->jira.api_token, ==, "tok123");
  g_assert_cmpuint (loaded->jira.n_workspaces, ==, 2);

  sc_cloud_config_free (loaded);
  remove_temp_dir (dir);
  g_free (dir);
}


static void
test_auto_migrate_old_format (void)
{
  gchar *dir = create_temp_dir ();
  gchar *path = sc_cloud_config_get_path (dir);
  GError *error = NULL;

  /* Write old single-workspace format */
  const gchar *content =
    "[jira]\n"
    "base_url=https://myteam.atlassian.net\n"
    "email=user@example.com\n"
    "api_token=tok123\n"
    "default_project=PROJ\n"
    "\n"
    "[r2]\n"
    "account_id=acct\n"
    "access_key_id=key\n"
    "secret_access_key=secret\n"
    "bucket=mybucket\n"
    "public_url=https://assets.example.com\n";

  g_mkdir_with_parents (dir, 0700);
  g_file_set_contents (path, content, -1, &error);
  g_assert_no_error (error);

  /* Load should auto-migrate */
  CloudConfig *config = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (config);

  g_assert_cmpstr (config->jira.email, ==, "user@example.com");
  g_assert_cmpstr (config->jira.api_token, ==, "tok123");
  g_assert_cmpuint (config->jira.n_workspaces, ==, 1);
  g_assert_cmpstr (config->jira.workspaces[0].label, ==, "myteam");
  g_assert_cmpstr (config->jira.workspaces[0].base_url, ==,
                   "https://myteam.atlassian.net");
  g_assert_cmpstr (config->jira.workspaces[0].default_project, ==, "PROJ");

  sc_cloud_config_free (config);

  /* Verify the file was rewritten — reload should still work */
  config = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (config);
  g_assert_cmpuint (config->jira.n_workspaces, ==, 1);
  g_assert_cmpstr (config->jira.workspaces[0].label, ==, "myteam");

  sc_cloud_config_free (config);
  remove_temp_dir (dir);
  g_free (path);
  g_free (dir);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/multi-workspace/new-struct-has-workspaces",
                    test_new_struct_has_workspaces);
  g_test_add_func ("/multi-workspace/load-multi-workspace-format",
                    test_load_multi_workspace_format);
  g_test_add_func ("/multi-workspace/save-and-load-multi-workspace",
                    test_save_and_load_multi_workspace);
  g_test_add_func ("/multi-workspace/auto-migrate-old-format",
                    test_auto_migrate_old_format);

  return g_test_run ();
}
