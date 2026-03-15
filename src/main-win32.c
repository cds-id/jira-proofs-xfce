#include <glib.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <sc-cloud-config.h>
#include <sc-platform.h>
#include <sc-r2.h>
#include <sc-jira.h>
#include <sc-recorder.h>
#include <sc-video-editor-blur.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <commctrl.h>
#include "win-wizard.h"
#include "win-main-dialog.h"
#endif

#include <stdio.h>
#include <stdlib.h>

static gboolean version = FALSE;
static gboolean fullscreen = FALSE;
static gboolean window_mode = FALSE;
static gboolean region = FALSE;
static gboolean clipboard = FALSE;
static gboolean upload_r2 = FALSE;
static gboolean setup_wizard = FALSE;
static gchar *screenshot_dir = NULL;

static GOptionEntry entries[] =
{
  { "version", 'V', 0, G_OPTION_ARG_NONE, &version, "Version information", NULL },
  { "fullscreen", 'f', 0, G_OPTION_ARG_NONE, &fullscreen, "Capture entire screen", NULL },
  { "window", 'w', 0, G_OPTION_ARG_NONE, &window_mode, "Capture active window", NULL },
  { "region", 'r', 0, G_OPTION_ARG_NONE, &region, "Capture selected region", NULL },
  { "clipboard", 'c', 0, G_OPTION_ARG_NONE, &clipboard, "Copy to clipboard", NULL },
  { "save", 's', 0, G_OPTION_ARG_FILENAME, &screenshot_dir, "Save to file/directory", "PATH" },
  { "upload-r2", 'u', 0, G_OPTION_ARG_NONE, &upload_r2, "Upload to R2", NULL },
  { "setup-wizard", 0, 0, G_OPTION_ARG_NONE, &setup_wizard, "Launch cloud setup wizard", NULL },
  { NULL }
};


int
main (int argc, char **argv)
{
  GError *error = NULL;
  GOptionContext *context;
  gchar *config_dir;

#ifdef G_OS_WIN32
  SetProcessDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  InitCommonControls ();
#endif

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      g_error_free (error);
      return EXIT_FAILURE;
    }
  g_option_context_free (context);

  if (version)
    {
      g_print ("xfce4-screenshooter %s\n", PACKAGE_VERSION);
      return EXIT_SUCCESS;
    }

  config_dir = sc_platform_config_dir ();

  /* Wizard: manual trigger or first-run */
  {
    gboolean need_wizard = setup_wizard;

    if (!need_wizard && !sc_cloud_config_exists (config_dir))
      need_wizard = TRUE;

    /* Corrupt config detection */
    if (!need_wizard && sc_cloud_config_exists (config_dir))
      {
        GError *load_err = NULL;
        CloudConfig *test = sc_cloud_config_load (config_dir, &load_err);
        if (test == NULL && load_err != NULL &&
            load_err->code != G_IO_ERROR_NOT_FOUND)
          {
            g_warning ("Corrupt config, resetting: %s", load_err->message);
            gchar *path = sc_cloud_config_get_path (config_dir);
            g_unlink (path);
            g_free (path);
            need_wizard = TRUE;
          }
        g_clear_error (&load_err);
        sc_cloud_config_free (test);
      }

    if (need_wizard && !fullscreen && !window_mode && !region)
      {
#ifdef G_OS_WIN32
        win_wizard_run (config_dir);
#endif
      }
  }

  /* CLI capture mode */
  if (fullscreen || window_mode || region)
    {
      ScCaptureMode mode = SC_CAPTURE_FULLSCREEN;
      ScRegion sel = { 0 };
      GdkPixbuf *pixbuf;

      if (window_mode)
        mode = SC_CAPTURE_WINDOW;
      else if (region)
        {
          mode = SC_CAPTURE_REGION;
          if (!sc_platform_select_region (&sel))
            {
              g_printerr ("Region selection cancelled or not supported.\n");
              g_free (config_dir);
              return EXIT_FAILURE;
            }
        }

      pixbuf = sc_platform_capture (mode, region ? &sel : NULL);
      if (pixbuf == NULL)
        {
          g_printerr ("Screenshot capture failed.\n");
          g_free (config_dir);
          return EXIT_FAILURE;
        }

      /* Execute action */
      if (clipboard)
        sc_platform_clipboard_copy_image (pixbuf);

      if (screenshot_dir != NULL)
        {
          GError *save_err = NULL;
          gdk_pixbuf_save (pixbuf, screenshot_dir, "png", &save_err, NULL);
          if (save_err)
            {
              g_printerr ("Save failed: %s\n", save_err->message);
              g_error_free (save_err);
            }
          else
            g_print ("Saved to %s\n", screenshot_dir);
        }

      if (upload_r2)
        {
          /* Save to temp, upload, delete temp */
          gchar *tmp = g_build_filename (g_get_tmp_dir (), "screenshot.png", NULL);
          GError *r2_err = NULL;
          gdk_pixbuf_save (pixbuf, tmp, "png", &r2_err, NULL);
          if (!r2_err)
            {
              CloudConfig *cc = sc_cloud_config_load (config_dir, &r2_err);
              if (cc)
                {
                  gchar *url = sc_r2_upload (cc, tmp, NULL, NULL, &r2_err);
                  if (url)
                    {
                      g_print ("Uploaded: %s\n", url);
                      g_free (url);
                    }
                  sc_cloud_config_free (cc);
                }
            }
          if (r2_err)
            {
              g_printerr ("R2 upload failed: %s\n", r2_err->message);
              g_error_free (r2_err);
            }
          g_unlink (tmp);
          g_free (tmp);
        }

      g_object_unref (pixbuf);
    }
  else if (!setup_wizard)
    {
      /* No CLI flags — show main dialog */
#ifdef G_OS_WIN32
      win_main_dialog_run ();
#endif
    }

  g_free (config_dir);
  g_free (screenshot_dir);
  return EXIT_SUCCESS;
}
