#ifndef __WIN_WIZARD_H__
#define __WIN_WIZARD_H__

#include <glib.h>

/* Returns TRUE if wizard completed, FALSE if skipped/cancelled */
gboolean win_wizard_run (const gchar *config_dir);

#endif
