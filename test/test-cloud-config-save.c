#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include "sc-cloud-config.h"


static gchar *
create_temp_dir (void)
{
  gchar *tmpl = g_build_filename (g_get_tmp_dir (),
                                   "test-cloud-config-XXXXXX", NULL);
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
test_save_and_load_round_trip (void)
{
  gchar *dir = create_temp_dir ();
  CloudConfig *config;
  CloudConfig *loaded;
  GError *error = NULL;
  gboolean ok;

  config = sc_cloud_config_create_default ();

  /* Fill in values */
  g_free (config->jira.base_url);
  config->jira.base_url = g_strdup ("https://test.atlassian.net");
  g_free (config->jira.email);
  config->jira.email = g_strdup ("user@example.com");
  g_free (config->jira.api_token);
  config->jira.api_token = g_strdup ("token123");
  g_free (config->jira.default_project);
  config->jira.default_project = g_strdup ("TEST");

  g_free (config->r2.account_id);
  config->r2.account_id = g_strdup ("acct-id");
  g_free (config->r2.access_key_id);
  config->r2.access_key_id = g_strdup ("access-key");
  g_free (config->r2.secret_access_key);
  config->r2.secret_access_key = g_strdup ("secret-key");
  g_free (config->r2.bucket);
  config->r2.bucket = g_strdup ("mybucket");
  g_free (config->r2.public_url);
  config->r2.public_url = g_strdup ("https://assets.example.com");

  g_free (config->presets.bug_evidence);
  config->presets.bug_evidence = g_strdup ("Bug Evidence");
  g_free (config->presets.work_evidence);
  config->presets.work_evidence = g_strdup ("Work Evidence");

  ok = sc_cloud_config_save (config, dir, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  loaded = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (loaded);
  g_assert_true (loaded->loaded);

  g_assert_cmpstr (loaded->jira.base_url, ==, "https://test.atlassian.net");
  g_assert_cmpstr (loaded->jira.email, ==, "user@example.com");
  g_assert_cmpstr (loaded->jira.api_token, ==, "token123");
  g_assert_cmpstr (loaded->jira.default_project, ==, "TEST");

  g_assert_cmpstr (loaded->r2.account_id, ==, "acct-id");
  g_assert_cmpstr (loaded->r2.access_key_id, ==, "access-key");
  g_assert_cmpstr (loaded->r2.secret_access_key, ==, "secret-key");
  g_assert_cmpstr (loaded->r2.bucket, ==, "mybucket");
  g_assert_cmpstr (loaded->r2.public_url, ==, "https://assets.example.com");

  g_assert_cmpstr (loaded->presets.bug_evidence, ==, "Bug Evidence");
  g_assert_cmpstr (loaded->presets.work_evidence, ==, "Work Evidence");

  sc_cloud_config_free (config);
  sc_cloud_config_free (loaded);
  remove_temp_dir (dir);
  g_free (dir);
}


static void
test_save_preserves_presets (void)
{
  gchar *dir = create_temp_dir ();
  CloudConfig *config;
  CloudConfig *loaded;
  GError *error = NULL;
  gboolean ok;

  /* First save: include presets */
  config = sc_cloud_config_create_default ();
  g_free (config->jira.base_url);
  config->jira.base_url = g_strdup ("https://test.atlassian.net");
  g_free (config->presets.bug_evidence);
  config->presets.bug_evidence = g_strdup ("My Bug Preset");
  g_free (config->presets.work_evidence);
  config->presets.work_evidence = g_strdup ("My Work Preset");

  ok = sc_cloud_config_save (config, dir, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  sc_cloud_config_free (config);

  /* Second save: empty presets (should preserve existing) */
  config = sc_cloud_config_create_default ();
  g_free (config->jira.base_url);
  config->jira.base_url = g_strdup ("https://updated.atlassian.net");
  /* presets are empty strings from create_default */

  ok = sc_cloud_config_save (config, dir, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  sc_cloud_config_free (config);

  /* Load and verify presets survived */
  loaded = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (loaded);

  g_assert_cmpstr (loaded->jira.base_url, ==, "https://updated.atlassian.net");
  g_assert_cmpstr (loaded->presets.bug_evidence, ==, "My Bug Preset");
  g_assert_cmpstr (loaded->presets.work_evidence, ==, "My Work Preset");

  sc_cloud_config_free (loaded);
  remove_temp_dir (dir);
  g_free (dir);
}


static void
test_config_exists (void)
{
  gchar *dir = create_temp_dir ();
  CloudConfig *config;
  GError *error = NULL;

  g_assert_false (sc_cloud_config_exists (dir));

  config = sc_cloud_config_create_default ();
  g_free (config->jira.base_url);
  config->jira.base_url = g_strdup ("https://test.atlassian.net");

  sc_cloud_config_save (config, dir, &error);
  g_assert_no_error (error);

  g_assert_true (sc_cloud_config_exists (dir));

  sc_cloud_config_free (config);
  remove_temp_dir (dir);
  g_free (dir);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/cloud-config/save-and-load-round-trip",
                    test_save_and_load_round_trip);
  g_test_add_func ("/cloud-config/save-preserves-presets",
                    test_save_preserves_presets);
  g_test_add_func ("/cloud-config/config-exists",
                    test_config_exists);

  return g_test_run ();
}
