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


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/multi-workspace/new-struct-has-workspaces",
                    test_new_struct_has_workspaces);

  return g_test_run ();
}
