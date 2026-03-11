#ifndef __SCREENSHOOTER_JIRA_H__
#define __SCREENSHOOTER_JIRA_H__

#include <glib.h>
#include "screenshooter-cloud-config.h"

typedef struct {
  gchar *key;
  gchar *summary;
} JiraIssue;

GList    *screenshooter_jira_search       (const CloudConfig *config,
                                            const gchar *query,
                                            GError **error);
gboolean  screenshooter_jira_post_comment (const CloudConfig *config,
                                            const gchar *issue_key,
                                            const gchar *preset_title,
                                            const gchar *description,
                                            const gchar *image_url,
                                            GError **error);
void      screenshooter_jira_issue_free   (JiraIssue *issue);
void      screenshooter_jira_issue_list_free (GList *issues);

#endif
