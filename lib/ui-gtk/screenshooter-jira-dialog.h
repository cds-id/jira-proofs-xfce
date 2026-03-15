#ifndef __SCREENSHOOTER_JIRA_DIALOG_H__
#define __SCREENSHOOTER_JIRA_DIALOG_H__

#include <gtk/gtk.h>
#include <sc-cloud-config.h>

gboolean screenshooter_jira_dialog_run (GtkWindow *parent,
                                         const CloudConfig *config,
                                         const gchar *image_url);

#endif
