#include "sc-platform.h"

#include <glib.h>
#include <sys/stat.h>


gchar *
sc_platform_config_dir (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "xfce4-screenshooter", NULL);
}


void
sc_platform_restrict_file (const gchar *path)
{
  chmod (path, 0600);
}


GdkPixbuf *
sc_platform_capture (ScCaptureMode mode, ScRegion *region)
{
  g_warning ("sc_platform_capture: not yet implemented");
  return NULL;
}


gboolean
sc_platform_select_region (ScRegion *out_region)
{
  g_warning ("sc_platform_select_region: not yet implemented");
  return FALSE;
}


gchar **
sc_platform_recorder_args (ScCaptureMode mode, ScRegion *region)
{
  g_warning ("sc_platform_recorder_args: not yet implemented");
  return NULL;
}


void
sc_platform_recorder_args_free (gchar **args)
{
  if (args == NULL)
    return;
  g_strfreev (args);
}


gboolean
sc_platform_clipboard_copy_image (GdkPixbuf *pixbuf)
{
  g_warning ("sc_platform_clipboard_copy_image: not yet implemented");
  return FALSE;
}


void
sc_platform_notify (const gchar *title, const gchar *body)
{
  g_warning ("sc_platform_notify: not yet implemented");
}
