#ifndef __SCREENSHOOTER_WIZARD_H__
#define __SCREENSHOOTER_WIZARD_H__

#include <gtk/gtk.h>

/* Returns TRUE if wizard completed, FALSE if skipped/cancelled */
gboolean screenshooter_wizard_run (GtkWindow *parent, const gchar *config_dir);

#endif
