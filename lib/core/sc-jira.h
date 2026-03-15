#ifndef __SC_JIRA_H__
#define __SC_JIRA_H__

#include <glib.h>
#include "sc-cloud-config.h"

typedef struct {
  gchar *key;
  gchar *summary;
} JiraIssue;

GList    *sc_jira_search          (const CloudConfig *config, const gchar *query, GError **error);
gboolean  sc_jira_post_comment    (const CloudConfig *config, const gchar *issue_key, const gchar *preset_title, const gchar *description, const gchar *image_url, GError **error);
gboolean  sc_jira_test_connection (const CloudConfig *config, GError **error);
void      sc_jira_issue_free      (JiraIssue *issue);
void      sc_jira_issue_list_free (GList *issues);

#endif
