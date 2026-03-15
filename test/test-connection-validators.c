#include <glib.h>
#include "../lib/core/sc-cloud-config.h"
#include "../lib/core/sc-r2.h"
#include "../lib/core/sc-jira.h"

static void
test_r2_test_connection_missing_fields (void)
{
  CloudConfig *config = sc_cloud_config_create_default ();
  GError *error = NULL;
  gboolean result = sc_r2_test_connection (config, &error);
  g_assert_false (result);
  g_assert_nonnull (error);
  g_assert_true (g_str_has_prefix (error->message, "R2 configuration"));
  g_error_free (error);
  sc_cloud_config_free (config);
}

static void
test_jira_test_connection_missing_fields (void)
{
  CloudConfig *config = sc_cloud_config_create_default ();
  GError *error = NULL;
  gboolean result = sc_jira_test_connection (config, &error);
  g_assert_false (result);
  g_assert_nonnull (error);
  g_assert_true (g_str_has_prefix (error->message, "Jira configuration"));
  g_error_free (error);
  sc_cloud_config_free (config);
}

int main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/r2/test-connection-missing-fields", test_r2_test_connection_missing_fields);
  g_test_add_func ("/jira/test-connection-missing-fields", test_jira_test_connection_missing_fields);
  return g_test_run ();
}
