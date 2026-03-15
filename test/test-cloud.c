#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include "../lib/core/sc-cloud-config.h"
#include "../lib/core/sc-r2.h"
#include "../lib/core/sc-jira.h"
#include "../lib/platform/sc-platform.h"

int main (int argc, char **argv)
{
  GError *error = NULL;
  gchar *config_dir = sc_platform_config_dir ();

  /* Test 1: Load config */
  printf ("=== Test 1: Loading cloud config ===\n");
  CloudConfig *config = sc_cloud_config_load (config_dir, &error);
  if (config == NULL)
    {
      fprintf (stderr, "Failed to load config: %s\n", error->message);
      g_error_free (error);
      return 1;
    }
  printf ("Jira base_url: %s\n", config->jira.base_url);
  printf ("Jira project: %s\n", config->jira.default_project);
  printf ("R2 bucket: %s\n", config->r2.bucket);
  printf ("R2 valid: %s\n", sc_cloud_config_valid_r2 (config) ? "YES" : "NO");
  printf ("Jira valid: %s\n", sc_cloud_config_valid_jira (config) ? "YES" : "NO");
  printf ("\n");

  /* Test 2: Search Jira for BNS-2727 */
  printf ("=== Test 2: Jira search for BNS-2727 ===\n");
  GList *issues = sc_jira_search (config, "BNS-2727", &error);
  if (error)
    {
      fprintf (stderr, "Jira search error: %s\n", error->message);
      g_clear_error (&error);
    }
  else
    {
      printf ("Found %d issues:\n", g_list_length (issues));
      for (GList *l = issues; l != NULL; l = l->next)
        {
          JiraIssue *issue = l->data;
          printf ("  %s: %s\n", issue->key, issue->summary);
        }
      sc_jira_issue_list_free (issues);
    }
  printf ("\n");

  /* Test 3: Upload a test image to R2 */
  printf ("=== Test 3: R2 upload test ===\n");
  /* Create a small test PNG (1x1 red pixel) */
  const gchar *test_file = "/tmp/screenshooter-test.png";
  /* Minimal valid PNG: 1x1 red pixel */
  {
    const gchar *test_content = "screenshooter cloud integration test";
    if (!g_file_set_contents (test_file, test_content, -1, &error))
      {
        fprintf (stderr, "Failed to create test file: %s\n", error->message);
        g_clear_error (&error);
      }
    else
      {
        gchar *url = sc_r2_upload (config, test_file, NULL, NULL, &error);
        if (error)
          {
            fprintf (stderr, "R2 upload error: %s\n", error->message);
            g_clear_error (&error);
          }
        else if (url)
          {
            printf ("Upload successful!\n");
            printf ("Public URL: %s\n", url);
            g_free (url);
          }
      }
  }

  sc_cloud_config_free (config);
  g_free (config_dir);
  printf ("\nAll tests complete.\n");
  return 0;
}
