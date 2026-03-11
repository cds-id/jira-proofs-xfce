#include "screenshooter-cloud-config.h"

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <sys/stat.h>


static gchar *
cloud_config_dir (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "xfce4-screenshooter", NULL);
}


gchar *
screenshooter_cloud_config_get_path (void)
{
  gchar *dir = cloud_config_dir ();
  gchar *path = g_build_filename (dir, "cloud.toml", NULL);
  g_free (dir);
  return path;
}


static gchar *
strip_toml_quotes (gchar *str)
{
  if (str == NULL)
    return str;
  gsize len = strlen (str);
  if (len >= 2 && str[0] == '"' && str[len - 1] == '"')
    {
      gchar *stripped = g_strndup (str + 1, len - 2);
      g_free (str);
      return stripped;
    }
  return str;
}


static GHashTable *
parse_toml_section (GKeyFile *kf, const gchar *section)
{
  GHashTable *ht = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_free);
  gchar **keys = g_key_file_get_keys (kf, section, NULL, NULL);
  if (keys == NULL)
    return ht;

  for (gint i = 0; keys[i] != NULL; i++)
    {
      gchar *val = g_key_file_get_string (kf, section, keys[i], NULL);
      if (val)
        g_hash_table_insert (ht, g_strdup (keys[i]), val);
    }
  g_strfreev (keys);
  return ht;
}


static gchar *
ht_take_string (GHashTable *ht, const gchar *key)
{
  gchar *val = g_hash_table_lookup (ht, key);
  if (val)
    {
      val = g_strdup (val);
      return strip_toml_quotes (val);
    }
  return g_strdup ("");
}


CloudConfig *
screenshooter_cloud_config_load (GError **error)
{
  gchar *path = screenshooter_cloud_config_get_path ();
  CloudConfig *config;
  GKeyFile *kf;
  GHashTable *ht;

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Cloud config not found: %s", path);
      g_free (path);
      return NULL;
    }

  kf = g_key_file_new ();
  if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, error))
    {
      g_key_file_free (kf);
      g_free (path);
      return NULL;
    }

  config = g_new0 (CloudConfig, 1);

  ht = parse_toml_section (kf, "jira");
  config->jira.base_url = ht_take_string (ht, "base_url");
  config->jira.email = ht_take_string (ht, "email");
  config->jira.api_token = ht_take_string (ht, "api_token");
  config->jira.default_project = ht_take_string (ht, "default_project");
  g_hash_table_destroy (ht);

  ht = parse_toml_section (kf, "r2");
  config->r2.account_id = ht_take_string (ht, "account_id");
  config->r2.access_key_id = ht_take_string (ht, "access_key_id");
  config->r2.secret_access_key = ht_take_string (ht, "secret_access_key");
  config->r2.bucket = ht_take_string (ht, "bucket");
  config->r2.public_url = ht_take_string (ht, "public_url");
  g_hash_table_destroy (ht);

  ht = parse_toml_section (kf, "presets");
  config->presets.bug_evidence = ht_take_string (ht, "bug_evidence");
  config->presets.work_evidence = ht_take_string (ht, "work_evidence");
  g_hash_table_destroy (ht);

  config->loaded = TRUE;

  g_key_file_free (kf);
  g_free (path);
  return config;
}


void
screenshooter_cloud_config_free (CloudConfig *config)
{
  if (config == NULL)
    return;

  g_free (config->jira.base_url);
  g_free (config->jira.email);
  g_free (config->jira.api_token);
  g_free (config->jira.default_project);

  g_free (config->r2.account_id);
  g_free (config->r2.access_key_id);
  g_free (config->r2.secret_access_key);
  g_free (config->r2.bucket);
  g_free (config->r2.public_url);

  g_free (config->presets.bug_evidence);
  g_free (config->presets.work_evidence);

  g_free (config);
}


gboolean
screenshooter_cloud_config_valid_r2 (const CloudConfig *config)
{
  if (config == NULL || !config->loaded)
    return FALSE;
  return (config->r2.account_id[0] != '\0' &&
          config->r2.access_key_id[0] != '\0' &&
          config->r2.secret_access_key[0] != '\0' &&
          config->r2.bucket[0] != '\0' &&
          config->r2.public_url[0] != '\0');
}


gboolean
screenshooter_cloud_config_valid_jira (const CloudConfig *config)
{
  if (config == NULL || !config->loaded)
    return FALSE;
  return (config->jira.base_url[0] != '\0' &&
          config->jira.email[0] != '\0' &&
          config->jira.api_token[0] != '\0' &&
          config->jira.default_project[0] != '\0');
}


void
screenshooter_cloud_config_create_default (const gchar *path,
                                            GError **error)
{
  gchar *dir;
  const gchar *content =
    "[jira]\n"
    "base_url = \"https://yourteam.atlassian.net\"\n"
    "email = \"you@example.com\"\n"
    "api_token = \"your-jira-api-token\"\n"
    "default_project = \"PROJ\"\n"
    "\n"
    "[r2]\n"
    "account_id = \"your-cf-account-id\"\n"
    "access_key_id = \"your-r2-access-key\"\n"
    "secret_access_key = \"your-r2-secret\"\n"
    "bucket = \"screenshots\"\n"
    "public_url = \"https://assets.yourdomain.com\"\n"
    "\n"
    "[presets]\n"
    "bug_evidence = \"Bug Evidence\"\n"
    "work_evidence = \"Work Evidence\"\n";

  dir = g_path_get_dirname (path);
  g_mkdir_with_parents (dir, 0700);
  g_free (dir);

  if (!g_file_set_contents (path, content, -1, error))
    return;

  chmod (path, 0600);
}
