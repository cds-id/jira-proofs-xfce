#ifndef __SC_CLOUD_CONFIG_H__
#define __SC_CLOUD_CONFIG_H__

#include <glib.h>

typedef struct {
  gchar *base_url;
  gchar *email;
  gchar *api_token;
  gchar *default_project;
} JiraCloudConfig;

typedef struct {
  gchar *account_id;
  gchar *access_key_id;
  gchar *secret_access_key;
  gchar *bucket;
  gchar *public_url;
} R2CloudConfig;

typedef struct {
  gchar *bug_evidence;
  gchar *work_evidence;
} PresetsConfig;

typedef struct {
  JiraCloudConfig jira;
  R2CloudConfig r2;
  PresetsConfig presets;
  gboolean loaded;
} CloudConfig;

CloudConfig *sc_cloud_config_load           (const gchar *config_dir, GError **error);
gboolean     sc_cloud_config_save           (const CloudConfig *config, const gchar *config_dir, GError **error);
void         sc_cloud_config_free           (CloudConfig *config);
CloudConfig *sc_cloud_config_create_default (void);
gboolean     sc_cloud_config_valid_r2       (const CloudConfig *config);
gboolean     sc_cloud_config_valid_jira     (const CloudConfig *config);
gchar       *sc_cloud_config_get_path       (const gchar *config_dir);
gboolean     sc_cloud_config_exists         (const gchar *config_dir);

#endif
