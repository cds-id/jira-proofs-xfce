#include <glib.h>
#include <sc-cloud-config.h>
#include <sc-platform.h>
#include <sc-r2.h>
#include <sc-jira.h>
#include <sc-recorder.h>
#include <sc-video-editor-blur.h>
#include <stdio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

int
main (int argc, char **argv)
{
#ifdef G_OS_WIN32
  SetProcessDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

  gchar *config_dir = sc_platform_config_dir ();
  g_print ("Config dir: %s\n", config_dir);

  CloudConfig *config = sc_cloud_config_create_default ();
  g_print ("Cloud config created (loaded=%d)\n", config->loaded);

  gboolean exists = sc_cloud_config_exists (config_dir);
  g_print ("Config exists: %s\n", exists ? "yes" : "no");

  VideoEditorState *state = video_editor_state_new ("test.mp4");
  g_print ("Video editor state created\n");
  video_editor_state_free (state);

  sc_cloud_config_free (config);
  g_free (config_dir);

  g_print ("Windows build validation OK\n");
  return 0;
}
