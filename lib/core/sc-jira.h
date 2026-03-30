#ifndef __SC_JIRA_H__
#define __SC_JIRA_H__

#include <glib.h>
#include "sc-cloud-config.h"

typedef struct {
  gchar *key;
  gchar *summary;
} JiraIssue;

typedef struct {
  JiraWorkspace *workspace;
  GList *issues;
} JiraSearchGroup;

GList    *sc_jira_search               (const gchar *email, const gchar *api_token,
                                        const JiraWorkspace *workspace,
                                        const gchar *query, GError **error);
GList    *sc_jira_search_all           (const JiraCloudConfig *jira,
                                        const gchar *query, GError **error);
gboolean  sc_jira_post_comment         (const gchar *email, const gchar *api_token,
                                        const JiraWorkspace *workspace,
                                        const gchar *issue_key,
                                        const gchar *preset_title,
                                        const gchar *description,
                                        const gchar *media_url,
                                        gboolean is_video,
                                        GError **error);
gboolean  sc_jira_test_connection      (const gchar *email, const gchar *api_token,
                                        const JiraWorkspace *workspace,
                                        GError **error);
void      sc_jira_issue_free           (JiraIssue *issue);
void      sc_jira_issue_list_free      (GList *issues);
void      sc_jira_search_group_free    (JiraSearchGroup *group);
void      sc_jira_search_group_list_free (GList *groups);

#endif
