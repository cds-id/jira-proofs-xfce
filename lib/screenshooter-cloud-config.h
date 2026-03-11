#ifndef __SCREENSHOOTER_CLOUD_CONFIG_H__
#define __SCREENSHOOTER_CLOUD_CONFIG_H__

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

CloudConfig *screenshooter_cloud_config_load   (GError **error);
void         screenshooter_cloud_config_free   (CloudConfig *config);
gboolean     screenshooter_cloud_config_valid_r2   (const CloudConfig *config);
gboolean     screenshooter_cloud_config_valid_jira (const CloudConfig *config);
gchar       *screenshooter_cloud_config_get_path   (void);
void         screenshooter_cloud_config_create_default (const gchar *path,
                                                         GError **error);

#endif
