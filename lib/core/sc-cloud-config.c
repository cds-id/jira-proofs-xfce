#include "sc-cloud-config.h"

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <sc-platform.h>


gchar *
sc_cloud_config_get_path (const gchar *config_dir)
{
  return g_build_filename (config_dir, "cloud.toml", NULL);
}


gboolean
sc_cloud_config_exists (const gchar *config_dir)
{
  gchar *path = sc_cloud_config_get_path (config_dir);
  gboolean exists = g_file_test (path, G_FILE_TEST_EXISTS);
  g_free (path);
  return exists;
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
sc_cloud_config_load (const gchar *config_dir, GError **error)
{
  gchar *path = sc_cloud_config_get_path (config_dir);
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

  /* Load shared jira credentials */
  ht = parse_toml_section (kf, "jira");
  config->jira.email = ht_take_string (ht, "email");
  config->jira.api_token = ht_take_string (ht, "api_token");
  g_hash_table_destroy (ht);

  /* Auto-migrate old single-workspace format */
  {
    gchar *old_base_url = g_key_file_get_string (kf, "jira", "base_url", NULL);
    if (old_base_url && old_base_url[0] != '\0')
      {
        gchar *old_project = g_key_file_get_string (kf, "jira", "default_project", NULL);

        /* Extract label from URL: https://myteam.atlassian.net -> myteam */
        gchar *label = NULL;
        {
          gchar *stripped = strip_toml_quotes (g_strdup (old_base_url));
          const gchar *host_start = strstr (stripped, "://");
          if (host_start)
            host_start += 3;
          else
            host_start = stripped;
          const gchar *dot = strchr (host_start, '.');
          if (dot)
            label = g_strndup (host_start, dot - host_start);
          else
            label = g_strdup (host_start);
          g_free (stripped);
        }

        /* Write migrated workspace group into the GKeyFile in memory */
        gchar *group = g_strdup_printf ("jira.workspace.%s", label);
        gchar *clean_url = strip_toml_quotes (g_strdup (old_base_url));
        gchar *clean_project = old_project ? strip_toml_quotes (g_strdup (old_project)) : g_strdup ("");
        g_key_file_set_string (kf, group, "base_url", clean_url);
        g_key_file_set_string (kf, group, "default_project", clean_project);
        g_free (clean_url);
        g_free (clean_project);
        g_free (group);

        /* Remove old keys from [jira] */
        g_key_file_remove_key (kf, "jira", "base_url", NULL);
        g_key_file_remove_key (kf, "jira", "default_project", NULL);

        /* Save migrated file */
        {
          gchar *data = g_key_file_to_data (kf, NULL, NULL);
          g_file_set_contents (path, data, -1, NULL);
          g_free (data);
        }

        g_free (label);
        g_free (old_base_url);
        g_free (old_project);
      }
    else
      {
        g_free (old_base_url);
      }
  }

  /* Load workspaces from [jira.workspace.*] groups */
  {
    gchar **groups = g_key_file_get_groups (kf, NULL);
    GPtrArray *ws_arr = g_ptr_array_new ();
    const gchar *prefix = "jira.workspace.";
    gsize prefix_len = strlen (prefix);

    for (gint i = 0; groups[i] != NULL; i++)
      {
        if (g_str_has_prefix (groups[i], prefix))
          {
            const gchar *label = groups[i] + prefix_len;
            JiraWorkspace ws;
            GHashTable *ws_ht = parse_toml_section (kf, groups[i]);

            ws.label = g_strdup (label);
            ws.base_url = ht_take_string (ws_ht, "base_url");
            ws.default_project = ht_take_string (ws_ht, "default_project");
            g_hash_table_destroy (ws_ht);

            JiraWorkspace *ws_copy = g_new (JiraWorkspace, 1);
            *ws_copy = ws;
            g_ptr_array_add (ws_arr, ws_copy);
          }
      }

    config->jira.n_workspaces = ws_arr->len;
    if (ws_arr->len > 0)
      {
        config->jira.workspaces = g_new (JiraWorkspace, ws_arr->len);
        for (guint i = 0; i < ws_arr->len; i++)
          {
            config->jira.workspaces[i] = *(JiraWorkspace *) ws_arr->pdata[i];
            g_free (ws_arr->pdata[i]);
          }
      }
    else
      {
        config->jira.workspaces = NULL;
      }

    g_ptr_array_free (ws_arr, TRUE);
    g_strfreev (groups);
  }

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


gboolean
sc_cloud_config_save (const CloudConfig *config, const gchar *config_dir,
                      GError **error)
{
  gchar *path;
  GKeyFile *kf;
  gchar *data;
  gsize length;
  gboolean result;

  if (config == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Config is NULL");
      return FALSE;
    }

  g_mkdir_with_parents (config_dir, 0700);

  path = sc_cloud_config_get_path (config_dir);

  /* Load existing file to preserve sections we don't overwrite */
  kf = g_key_file_new ();
  if (g_file_test (path, G_FILE_TEST_EXISTS))
    g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL);

  /* Remove old-style jira keys if present */
  g_key_file_remove_key (kf, "jira", "base_url", NULL);
  g_key_file_remove_key (kf, "jira", "default_project", NULL);

  /* Remove all existing [jira.workspace.*] groups */
  {
    gchar **groups = g_key_file_get_groups (kf, NULL);
    for (gint i = 0; groups[i] != NULL; i++)
      {
        if (g_str_has_prefix (groups[i], "jira.workspace."))
          g_key_file_remove_group (kf, groups[i], NULL);
      }
    g_strfreev (groups);
  }

  /* Write shared credentials */
  g_key_file_set_string (kf, "jira", "email",
                         config->jira.email ? config->jira.email : "");
  g_key_file_set_string (kf, "jira", "api_token",
                         config->jira.api_token ? config->jira.api_token : "");

  /* Write each workspace */
  for (gsize i = 0; i < config->jira.n_workspaces; i++)
    {
      gchar *group = g_strdup_printf ("jira.workspace.%s",
                                       config->jira.workspaces[i].label);
      g_key_file_set_string (kf, group, "base_url",
                             config->jira.workspaces[i].base_url
                               ? config->jira.workspaces[i].base_url : "");
      g_key_file_set_string (kf, group, "default_project",
                             config->jira.workspaces[i].default_project
                               ? config->jira.workspaces[i].default_project : "");
      g_free (group);
    }

  /* Overwrite [r2] section */
  g_key_file_set_string (kf, "r2", "account_id", config->r2.account_id ? config->r2.account_id : "");
  g_key_file_set_string (kf, "r2", "access_key_id", config->r2.access_key_id ? config->r2.access_key_id : "");
  g_key_file_set_string (kf, "r2", "secret_access_key", config->r2.secret_access_key ? config->r2.secret_access_key : "");
  g_key_file_set_string (kf, "r2", "bucket", config->r2.bucket ? config->r2.bucket : "");
  g_key_file_set_string (kf, "r2", "public_url", config->r2.public_url ? config->r2.public_url : "");

  /* Only write preset keys if the new value is non-empty */
  if (config->presets.bug_evidence && config->presets.bug_evidence[0] != '\0')
    g_key_file_set_string (kf, "presets", "bug_evidence", config->presets.bug_evidence);
  if (config->presets.work_evidence && config->presets.work_evidence[0] != '\0')
    g_key_file_set_string (kf, "presets", "work_evidence", config->presets.work_evidence);

  data = g_key_file_to_data (kf, &length, NULL);
  result = g_file_set_contents (path, data, length, error);

  if (result)
    sc_platform_restrict_file (path);

  g_free (data);
  g_key_file_free (kf);
  g_free (path);
  return result;
}


void
sc_cloud_config_free (CloudConfig *config)
{
  if (config == NULL)
    return;

  g_free (config->jira.email);
  g_free (config->jira.api_token);
  for (gsize i = 0; i < config->jira.n_workspaces; i++)
    {
      g_free (config->jira.workspaces[i].label);
      g_free (config->jira.workspaces[i].base_url);
      g_free (config->jira.workspaces[i].default_project);
    }
  g_free (config->jira.workspaces);

  g_free (config->r2.account_id);
  g_free (config->r2.access_key_id);
  g_free (config->r2.secret_access_key);
  g_free (config->r2.bucket);
  g_free (config->r2.public_url);

  g_free (config->presets.bug_evidence);
  g_free (config->presets.work_evidence);

  g_free (config);
}


CloudConfig *
sc_cloud_config_create_default (void)
{
  CloudConfig *config = g_new0 (CloudConfig, 1);

  config->jira.email = g_strdup ("");
  config->jira.api_token = g_strdup ("");
  config->jira.workspaces = NULL;
  config->jira.n_workspaces = 0;

  config->r2.account_id = g_strdup ("");
  config->r2.access_key_id = g_strdup ("");
  config->r2.secret_access_key = g_strdup ("");
  config->r2.bucket = g_strdup ("");
  config->r2.public_url = g_strdup ("");

  config->presets.bug_evidence = g_strdup ("");
  config->presets.work_evidence = g_strdup ("");

  config->loaded = FALSE;

  return config;
}


gboolean
sc_cloud_config_valid_r2 (const CloudConfig *config)
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
sc_cloud_config_valid_jira (const CloudConfig *config)
{
  if (config == NULL || !config->loaded)
    return FALSE;
  if (config->jira.email[0] == '\0' ||
      config->jira.api_token[0] == '\0')
    return FALSE;
  if (config->jira.n_workspaces == 0)
    return FALSE;
  for (gsize i = 0; i < config->jira.n_workspaces; i++)
    {
      if (config->jira.workspaces[i].base_url == NULL ||
          config->jira.workspaces[i].base_url[0] == '\0')
        return FALSE;
    }
  return TRUE;
}
