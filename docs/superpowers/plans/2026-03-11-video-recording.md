# Video Recording Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add video screen recording via FFmpeg subprocess, with the same Save/R2/Jira action flow as screenshots.

**Architecture:** FFmpeg is spawned as a child process using `x11grab` to capture the screen. A floating stop button with timer controls recording. After stopping, the MP4 file goes through a dedicated `action_idle_recording()` that reuses the cloud upload infrastructure.

**Tech Stack:** C, GTK3, GLib (process spawning), FFmpeg (runtime dependency), existing R2/Jira modules.

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `lib/screenshooter-recorder.c` | Create | FFmpeg subprocess management (start/stop/available) |
| `lib/screenshooter-recorder.h` | Create | Public API for recorder module |
| `lib/screenshooter-recorder-dialog.c` | Create | Floating stop button with timer |
| `lib/screenshooter-recorder-dialog.h` | Create | Public API for stop dialog |
| `lib/screenshooter-global.h` | Modify | Add `recording` field to `ScreenshotData` |
| `lib/screenshooter-actions.c` | Modify | Branch recording flow, add `action_idle_recording()` |
| `lib/screenshooter-actions.h` | Modify | Export `screenshooter_start_recording()` |
| `lib/screenshooter-dialogs.c` | Modify | Add "Record Video" checkbox to region dialog |
| `lib/screenshooter-r2.c` | Modify | Dynamic timeout for video uploads |
| `lib/libscreenshooter.h` | Modify | Add recorder includes |
| `lib/meson.build` | Modify | Add new source files |
| `src/main.c` | Modify | Add `--record-*` CLI flags |

---

## Chunk 1: Core Recorder Module

### Task 1: Add `recording` field to ScreenshotData

**Files:**
- Modify: `lib/screenshooter-global.h:47-73`

- [ ] **Step 1: Add recording field to ScreenshotData struct**

In `lib/screenshooter-global.h`, add after line 68 (`gchar *jira_issue_key;`):

```c
  gboolean recording;
```

- [ ] **Step 2: Verify build**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 3: Commit**

```bash
git add lib/screenshooter-global.h
git commit -m "feat: add recording field to ScreenshotData struct"
```

---

### Task 2: Create recorder module (`screenshooter-recorder.c/.h`)

**Files:**
- Create: `lib/screenshooter-recorder.h`
- Create: `lib/screenshooter-recorder.c`
- Modify: `lib/meson.build`

- [ ] **Step 1: Create the header file**

Create `lib/screenshooter-recorder.h`:

```c
#ifndef __HAVE_RECORDER_H__
#define __HAVE_RECORDER_H__

#include <glib.h>

typedef struct {
  GPid    pid;
  gchar  *output_path;
  gint    region;
  gint    x, y, w, h;
  guint   child_watch_id;
  gboolean stopped;
} RecorderState;

gboolean       screenshooter_recorder_available (void);

RecorderState *screenshooter_recorder_start     (gint     region,
                                                  gint     x,
                                                  gint     y,
                                                  gint     w,
                                                  gint     h,
                                                  GError **error);

gchar         *screenshooter_recorder_stop      (RecorderState  *state,
                                                  GError        **error);

void           screenshooter_recorder_free      (RecorderState *state);

#endif
```

- [ ] **Step 2: Create the implementation file**

Create `lib/screenshooter-recorder.c`:

```c
#include "screenshooter-recorder.h"

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>


gboolean
screenshooter_recorder_available (void)
{
  gchar *path = g_find_program_in_path ("ffmpeg");
  if (path)
    {
      g_free (path);
      return TRUE;
    }
  return FALSE;
}


static gchar *
get_display_string (void)
{
  const gchar *display = g_getenv ("DISPLAY");
  if (display == NULL || display[0] == '\0')
    return g_strdup (":0.0");

  /* Strip hostname prefix: "localhost:10.0" -> ":10.0" */
  const gchar *colon = strchr (display, ':');
  if (colon && colon != display)
    return g_strdup (colon);

  return g_strdup (display);
}


static gchar *
build_output_path (void)
{
  time_t now = time (NULL);
  struct tm *tm = localtime (&now);
  gchar timestamp[64];
  strftime (timestamp, sizeof (timestamp), "%Y-%m-%d_%H-%M-%S", tm);
  return g_strdup_printf ("%s/recording-%s.mp4", g_get_tmp_dir (), timestamp);
}


RecorderState *
screenshooter_recorder_start (gint region, gint x, gint y, gint w, gint h,
                               GError **error)
{
  RecorderState *state;
  gchar *display_str;
  gchar *input_str;
  gchar *video_size;
  gchar *ffmpeg_path;
  GPid pid;
  gboolean spawned;

  ffmpeg_path = g_find_program_in_path ("ffmpeg");
  if (ffmpeg_path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "FFmpeg is required for video recording. "
                   "Install with: sudo apt install ffmpeg");
      return NULL;
    }

  display_str = get_display_string ();
  input_str = g_strdup_printf ("%s+%d,%d", display_str, x, y);
  video_size = g_strdup_printf ("%dx%d", w, h);

  state = g_new0 (RecorderState, 1);
  state->output_path = build_output_path ();
  state->region = region;
  state->x = x;
  state->y = y;
  state->w = w;
  state->h = h;
  state->stopped = FALSE;

  gchar *argv[] = {
    ffmpeg_path,
    "-y",
    "-f", "x11grab",
    "-framerate", "30",
    "-video_size", video_size,
    "-i", input_str,
    "-c:v", "libx264",
    "-preset", "ultrafast",
    "-crf", "23",
    "-pix_fmt", "yuv420p",
    state->output_path,
    NULL
  };

  spawned = g_spawn_async (NULL, argv, NULL,
                            G_SPAWN_DO_NOT_REAP_CHILD,
                            NULL, NULL, &pid, error);

  g_free (ffmpeg_path);
  g_free (display_str);
  g_free (input_str);
  g_free (video_size);

  if (!spawned)
    {
      screenshooter_recorder_free (state);
      return NULL;
    }

  state->pid = pid;
  return state;
}


gchar *
screenshooter_recorder_stop (RecorderState *state, GError **error)
{
  gchar *result;
  gint status;

  if (state == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Recorder state is NULL");
      return NULL;
    }

  if (state->stopped)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Recording already stopped");
      return NULL;
    }

  state->stopped = TRUE;

  /* Send SIGINT for clean shutdown (writes MP4 trailer) */
  kill (state->pid, SIGINT);

  /* Remove child watch before waitpid to prevent race condition */
  if (state->child_watch_id > 0)
    {
      g_source_remove (state->child_watch_id);
      state->child_watch_id = 0;
    }

  /* Wait for ffmpeg to finish */
  waitpid (state->pid, &status, 0);
  g_spawn_close_pid (state->pid);

  if (!g_file_test (state->output_path, G_FILE_TEST_EXISTS))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Recording file was not created");
      return NULL;
    }

  result = g_strdup (state->output_path);
  return result;
}


void
screenshooter_recorder_free (RecorderState *state)
{
  if (state == NULL)
    return;

  g_free (state->output_path);
  g_free (state);
}
```

- [ ] **Step 3: Add to meson.build**

In `lib/meson.build`, add to `libscreenshooter_sources` list after the jira-dialog entries:

```meson
  'screenshooter-recorder.c',
  'screenshooter-recorder.h',
```

- [ ] **Step 4: Verify build**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add lib/screenshooter-recorder.h lib/screenshooter-recorder.c lib/meson.build
git commit -m "feat: add recorder module with FFmpeg subprocess management"
```

---

### Task 3: Create floating stop dialog (`screenshooter-recorder-dialog.c/.h`)

**Files:**
- Create: `lib/screenshooter-recorder-dialog.h`
- Create: `lib/screenshooter-recorder-dialog.c`
- Modify: `lib/meson.build`

- [ ] **Step 1: Create the header file**

Create `lib/screenshooter-recorder-dialog.h`:

```c
#ifndef __HAVE_RECORDER_DIALOG_H__
#define __HAVE_RECORDER_DIALOG_H__

#include "screenshooter-recorder.h"
#include <gtk/gtk.h>

typedef void (*RecorderStopCallback) (const gchar *output_path,
                                       gpointer     user_data);

void screenshooter_recorder_dialog_run (RecorderState        *state,
                                         RecorderStopCallback  callback,
                                         gpointer              user_data);

#endif
```

- [ ] **Step 2: Create the implementation file**

Create `lib/screenshooter-recorder-dialog.c`:

```c
#include "screenshooter-recorder-dialog.h"

#include <glib.h>
#include <gtk/gtk.h>

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#endif

typedef struct {
  RecorderState        *state;
  RecorderStopCallback  callback;
  gpointer              user_data;
  GtkWidget            *window;
  GtkWidget            *timer_label;
  guint                 timer_id;
  guint                 elapsed;
  gboolean              finished;
} RecorderDialogData;


static void
do_stop (RecorderDialogData *data)
{
  GError *error = NULL;
  gchar *output_path;

  if (data->finished)
    return;
  data->finished = TRUE;

  if (data->timer_id > 0)
    {
      g_source_remove (data->timer_id);
      data->timer_id = 0;
    }

  output_path = screenshooter_recorder_stop (data->state, &error);

  if (error)
    {
      GtkWidget *err_dlg = gtk_message_dialog_new (NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Recording failed: %s", error->message);
      gtk_dialog_run (GTK_DIALOG (err_dlg));
      gtk_widget_destroy (err_dlg);
      g_error_free (error);

      /* Clean up temp file */
      if (data->state->output_path)
        g_unlink (data->state->output_path);
    }

  gtk_widget_destroy (data->window);

  if (data->callback)
    data->callback (output_path, data->user_data);

  g_free (output_path);
  screenshooter_recorder_free (data->state);
  g_free (data);
}


static gboolean
cb_timer_tick (gpointer user_data)
{
  RecorderDialogData *data = user_data;
  gchar *text;

  data->elapsed++;
  text = g_strdup_printf ("%02u:%02u", data->elapsed / 60, data->elapsed % 60);
  gtk_label_set_text (GTK_LABEL (data->timer_label), text);
  g_free (text);

  return TRUE;
}


static void
cb_stop_clicked (GtkButton *button, RecorderDialogData *data)
{
  do_stop (data);
}


static gboolean
cb_key_press (GtkWidget *widget, GdkEventKey *event, RecorderDialogData *data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      do_stop (data);
      return TRUE;
    }
  return FALSE;
}


static gboolean
cb_delete_event (GtkWidget *widget, GdkEvent *event, RecorderDialogData *data)
{
  do_stop (data);
  return TRUE;
}


static void
cb_child_watch (GPid pid, gint status, gpointer user_data)
{
  RecorderDialogData *data = user_data;

  g_spawn_close_pid (pid);
  data->state->child_watch_id = 0;

  /* FFmpeg exited unexpectedly */
  if (!data->finished)
    {
      data->state->stopped = TRUE;
      data->finished = TRUE;

      if (data->timer_id > 0)
        {
          g_source_remove (data->timer_id);
          data->timer_id = 0;
        }

      GtkWidget *err_dlg = gtk_message_dialog_new (NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "FFmpeg exited unexpectedly during recording.");
      gtk_dialog_run (GTK_DIALOG (err_dlg));
      gtk_widget_destroy (err_dlg);

      /* Clean up temp file */
      if (data->state->output_path)
        g_unlink (data->state->output_path);

      gtk_widget_destroy (data->window);

      if (data->callback)
        data->callback (NULL, data->user_data);

      screenshooter_recorder_free (data->state);
      g_free (data);
    }
}


void
screenshooter_recorder_dialog_run (RecorderState        *state,
                                    RecorderStopCallback  callback,
                                    gpointer              user_data)
{
  RecorderDialogData *data;
  GtkWidget *window, *hbox, *stop_btn, *timer_label;
  GdkDisplay *display;
  GdkMonitor *monitor;
  GdkRectangle geometry;

  data = g_new0 (RecorderDialogData, 1);
  data->state = state;
  data->callback = callback;
  data->user_data = user_data;
  data->elapsed = 0;
  data->finished = FALSE;

  /* Create floating window */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "Recording");
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (window), 8);
  data->window = window;

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_container_add (GTK_CONTAINER (window), hbox);

  /* Red circle indicator */
  timer_label = gtk_label_new ("00:00");
  gtk_widget_set_margin_start (timer_label, 4);
  gtk_widget_set_margin_end (timer_label, 4);
  gtk_box_pack_start (GTK_BOX (hbox), timer_label, FALSE, FALSE, 0);
  data->timer_label = timer_label;

  stop_btn = gtk_button_new_with_label ("Stop Recording");
  gtk_box_pack_start (GTK_BOX (hbox), stop_btn, FALSE, FALSE, 0);

  /* Style the stop button red */
  GtkCssProvider *css = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (css,
    "button { background: #e74c3c; color: white; font-weight: bold; }", -1, NULL);
  gtk_style_context_add_provider (
    gtk_widget_get_style_context (stop_btn),
    GTK_STYLE_PROVIDER (css),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (css);

  /* Position in top-right corner */
  display = gdk_display_get_default ();
  monitor = gdk_display_get_primary_monitor (display);
  if (monitor == NULL)
    monitor = gdk_display_get_monitor (display, 0);
  gdk_monitor_get_geometry (monitor, &geometry);

  gtk_widget_show_all (window);

  /* Move after show to get the allocated size */
  GtkRequisition req;
  gtk_widget_get_preferred_size (window, NULL, &req);
  gtk_window_move (GTK_WINDOW (window),
                   geometry.x + geometry.width - req.width - 20,
                   geometry.y + 20);

  /* Connect signals */
  g_signal_connect (stop_btn, "clicked",
                    G_CALLBACK (cb_stop_clicked), data);
  g_signal_connect (window, "key-press-event",
                    G_CALLBACK (cb_key_press), data);
  g_signal_connect (window, "delete-event",
                    G_CALLBACK (cb_delete_event), data);

  /* Start timer */
  data->timer_id = g_timeout_add (1000, cb_timer_tick, data);

  /* Watch ffmpeg child process */
  state->child_watch_id = g_child_watch_add (state->pid,
                                              cb_child_watch, data);
}
```

- [ ] **Step 3: Add to meson.build**

In `lib/meson.build`, add to `libscreenshooter_sources` list after the recorder entries:

```meson
  'screenshooter-recorder-dialog.c',
  'screenshooter-recorder-dialog.h',
```

- [ ] **Step 4: Add includes to libscreenshooter.h**

In `lib/libscreenshooter.h`, add after the jira-dialog include:

```c
#include "screenshooter-recorder.h"
#include "screenshooter-recorder-dialog.h"
```

- [ ] **Step 5: Verify build**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 6: Commit**

```bash
git add lib/screenshooter-recorder-dialog.h lib/screenshooter-recorder-dialog.c lib/meson.build lib/libscreenshooter.h
git commit -m "feat: add floating stop button dialog for video recording"
```

---

## Chunk 2: Integration with Action Flow and CLI

### Task 4: Integrate recording flow into actions (`screenshooter-actions.c/.h`)

**Files:**
- Modify: `lib/screenshooter-actions.c`
- Modify: `lib/screenshooter-actions.h`

- [ ] **Step 1: Add recording includes to screenshooter-actions.c**

In `lib/screenshooter-actions.c`, add after line 30 (`#include "screenshooter-jira-dialog.h"`):

```c
#include "screenshooter-recorder.h"
#include "screenshooter-recorder-dialog.h"
#include "screenshooter-select.h"
#ifdef ENABLE_X11
#include "screenshooter-utils-x11.h"
#endif
```

- [ ] **Step 2: Add `action_idle_recording()` function**

In `lib/screenshooter-actions.c`, add before the `take_screenshot_idle` function (before line 305):

```c
static void
action_idle_recording (const gchar *save_location, ScreenshotData *sd)
{
  if (save_location == NULL)
    {
      sd->finalize_callback (FALSE, sd->finalize_callback_data);
      return;
    }

  if (!sd->action_specified)
    {
      /* Create a placeholder pixbuf so the actions dialog preview doesn't crash.
       * The actions dialog calls screenshot_get_thumbnail(sd->screenshot, ...)
       * which would segfault on NULL. */
      sd->screenshot = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 320, 240);
      gdk_pixbuf_fill (sd->screenshot, 0x333333FF);

      GtkWidget *dialog = screenshooter_actions_dialog_new (sd);
      gint response;

      g_signal_connect (dialog, "response",
                        G_CALLBACK (cb_help_response), NULL);

      response = gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      g_object_unref (sd->screenshot);
      sd->screenshot = NULL;

      if (response == GTK_RESPONSE_CANCEL ||
          response == GTK_RESPONSE_DELETE_EVENT ||
          response == GTK_RESPONSE_CLOSE)
        {
          g_unlink (save_location);
          sd->finalize_callback (FALSE, sd->finalize_callback_data);
          return;
        }
    }

  if (sd->action & SAVE)
    {
      /* Move temp file to user-chosen location via save dialog */
      GFile *src = g_file_new_for_path (save_location);
      gchar *dest_path = NULL;

      GtkWidget *chooser = gtk_file_chooser_dialog_new (
        "Save Recording", NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT, NULL);
      gtk_file_chooser_set_do_overwrite_confirmation (
        GTK_FILE_CHOOSER (chooser), TRUE);
      {
        gchar *basename = g_path_get_basename (save_location);
        gtk_file_chooser_set_current_name (
          GTK_FILE_CHOOSER (chooser), basename);
        g_free (basename);
      }

      if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT)
        dest_path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
      gtk_widget_destroy (chooser);

      if (dest_path)
        {
          GFile *dst = g_file_new_for_path (dest_path);
          GError *err = NULL;
          g_file_move (src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err);
          if (err)
            {
              g_warning ("Failed to save recording: %s", err->message);
              g_error_free (err);
            }
          g_object_unref (dst);
          g_free (dest_path);
        }

      g_object_unref (src);
    }

  /* Cloud actions: reuse existing R2/Jira flow */
  if (sd->action & (UPLOAD_R2 | POST_JIRA))
    {
      GError *cloud_error = NULL;
      CloudConfig *cloud_config = screenshooter_cloud_config_load (&cloud_error);

      if (cloud_config == NULL)
        {
          GtkWidget *warn = gtk_message_dialog_new (NULL,
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Cloud config error: %s",
            cloud_error ? cloud_error->message : "unknown");
          gtk_dialog_run (GTK_DIALOG (warn));
          gtk_widget_destroy (warn);
          g_clear_error (&cloud_error);
        }
      else
        {
          gchar *public_url = screenshooter_r2_upload (cloud_config,
            save_location, NULL, NULL, &cloud_error);

          if (public_url == NULL)
            {
              GtkWidget *warn = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "R2 upload failed: %s",
                cloud_error ? cloud_error->message : "Unknown error");
              gtk_dialog_run (GTK_DIALOG (warn));
              gtk_widget_destroy (warn);
              g_clear_error (&cloud_error);
            }
          else if (sd->action & POST_JIRA)
            {
              if (sd->jira_issue_key && sd->jira_issue_key[0] != '\0')
                {
                  GError *jira_err = NULL;
                  screenshooter_jira_post_comment (cloud_config,
                    sd->jira_issue_key,
                    cloud_config->presets.bug_evidence
                      ? cloud_config->presets.bug_evidence : "Recording",
                    "", public_url, &jira_err);
                  if (jira_err)
                    {
                      GtkWidget *warn = gtk_message_dialog_new (NULL,
                        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                        "Jira post failed: %s", jira_err->message);
                      gtk_dialog_run (GTK_DIALOG (warn));
                      gtk_widget_destroy (warn);
                      g_error_free (jira_err);
                    }
                  else
                    {
                      GtkClipboard *clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
                      gtk_clipboard_set_text (clip, public_url, -1);

                      GtkWidget *info = gtk_message_dialog_new (NULL,
                        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                        "Posted to %s!\n\n%s\n\n(URL copied to clipboard)",
                        sd->jira_issue_key, public_url);
                      gtk_dialog_run (GTK_DIALOG (info));
                      gtk_widget_destroy (info);
                    }
                }
              else
                {
                  screenshooter_jira_dialog_run (NULL, cloud_config,
                                                  public_url);
                }
            }
          else
            {
              GtkClipboard *clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
              gtk_clipboard_set_text (clip, public_url, -1);

              GtkWidget *info = gtk_message_dialog_new (NULL,
                GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                "Uploaded to R2!\n\n%s\n\n(Copied to clipboard)", public_url);
              gtk_dialog_run (GTK_DIALOG (info));
              gtk_widget_destroy (info);
            }

          g_free (public_url);
          screenshooter_cloud_config_free (cloud_config);
        }
    }

  /* Clean up temp file if not saved */
  if (!(sd->action & SAVE))
    g_unlink (save_location);

  sd->finalize_callback (TRUE, sd->finalize_callback_data);
}
```

- [ ] **Step 3: Add recording stop callback**

In `lib/screenshooter-actions.c`, add after `action_idle_recording`:

```c
static void
cb_recording_stopped (const gchar *output_path, gpointer user_data)
{
  ScreenshotData *sd = user_data;
  action_idle_recording (output_path, sd);
}
```

- [ ] **Step 4: Add `screenshooter_start_recording()` function**

In `lib/screenshooter-actions.c`, add after `cb_recording_stopped`:

```c
void
screenshooter_start_recording (ScreenshotData *sd)
{
  GError *error = NULL;
  gint x = 0, y = 0, w = 0, h = 0;
  GdkDisplay *display;
  GdkMonitor *monitor;
  GdkRectangle geometry;

  /* Check ffmpeg availability */
  if (!screenshooter_recorder_available ())
    {
      GtkWidget *warn = gtk_message_dialog_new (NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "FFmpeg is required for video recording.\n"
        "Install with: sudo apt install ffmpeg");
      gtk_dialog_run (GTK_DIALOG (warn));
      gtk_widget_destroy (warn);
      sd->finalize_callback (FALSE, sd->finalize_callback_data);
      return;
    }

#ifdef ENABLE_WAYLAND
  display = gdk_display_get_default ();
  if (GDK_IS_WAYLAND_DISPLAY (display))
    {
      GtkWidget *warn = gtk_message_dialog_new (NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Video recording is currently supported on X11 only.\n"
        "Wayland support coming in a future version.");
      gtk_dialog_run (GTK_DIALOG (warn));
      gtk_widget_destroy (warn);
      sd->finalize_callback (FALSE, sd->finalize_callback_data);
      return;
    }
#endif

  /* Get capture region coordinates */
  display = gdk_display_get_default ();
  monitor = gdk_display_get_primary_monitor (display);
  if (monitor == NULL)
    monitor = gdk_display_get_monitor (display, 0);
  gdk_monitor_get_geometry (monitor, &geometry);

  if (sd->region == FULLSCREEN)
    {
      x = geometry.x;
      y = geometry.y;
      w = geometry.width;
      h = geometry.height;
    }
  else if (sd->region == ACTIVE_WINDOW)
    {
#ifdef ENABLE_X11
      {
        GdkScreen *screen = gdk_display_get_default_screen (display);
        gboolean needs_unref = FALSE, border = FALSE;
        GdkWindow *active = screenshooter_get_active_window (screen,
                                                               &needs_unref,
                                                               &border);
        if (active)
          {
            gdk_window_get_origin (active, &x, &y);
            w = gdk_window_get_width (active);
            h = gdk_window_get_height (active);
            if (needs_unref)
              g_object_unref (active);
          }
        else
          {
            /* Fallback to fullscreen */
            x = geometry.x;
            y = geometry.y;
            w = geometry.width;
            h = geometry.height;
          }
      }
#else
      {
        /* Fallback to fullscreen (non-X11 build) */
        x = geometry.x;
        y = geometry.y;
        w = geometry.width;
        h = geometry.height;
      }
#endif
    }
  else if (sd->region == SELECT)
    {
      /* Reuse existing region selection from screenshooter-select.h */
      GdkRectangle sel;
      if (!screenshooter_select_region (&sel))
        {
          sd->finalize_callback (FALSE, sd->finalize_callback_data);
          return;
        }
      x = sel.x;
      y = sel.y;
      w = sel.width;
      h = sel.height;
    }

  /* Ensure dimensions are even (required by libx264) */
  w = w & ~1;
  h = h & ~1;

  RecorderState *state = screenshooter_recorder_start (
    sd->region, x, y, w, h, &error);

  if (state == NULL)
    {
      GtkWidget *warn = gtk_message_dialog_new (NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "Failed to start recording: %s", error->message);
      gtk_dialog_run (GTK_DIALOG (warn));
      gtk_widget_destroy (warn);
      g_error_free (error);
      sd->finalize_callback (FALSE, sd->finalize_callback_data);
      return;
    }

  screenshooter_recorder_dialog_run (state, cb_recording_stopped, sd);
}
```

- [ ] **Step 5: Add recording branch in `screenshooter_take_screenshot()`**

In `lib/screenshooter-actions.c`, modify `screenshooter_take_screenshot()`. Add at the very beginning of the function, **before** the `gint delay;` declaration:

```c
  if (sd->recording)
    {
      screenshooter_start_recording (sd);
      return;
    }
```

This must go before `gint delay` to avoid an unused variable warning when the recording branch is taken.

- [ ] **Step 6: Export in header**

In `lib/screenshooter-actions.h`, add after the existing declaration:

```c
void screenshooter_start_recording     (ScreenshotData *sd);
```

- [ ] **Step 7: Verify build**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 8: Commit**

```bash
git add lib/screenshooter-actions.c lib/screenshooter-actions.h
git commit -m "feat: integrate recording flow into action system"
```

---

### Task 5: Add CLI flags (`src/main.c`)

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Add CLI variables**

In `src/main.c`, add after line 37 (`gchar *jira_issue = NULL;`):

```c
gboolean record_fullscreen = FALSE;
gboolean record_window = FALSE;
gboolean record_region = FALSE;
```

- [ ] **Step 2: Add GOptionEntry entries**

In `src/main.c`, add to the `entries[]` array before the NULL terminator:

```c
  {
    "record-fullscreen", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE,
    &record_fullscreen,
    N_("Record a video of the entire screen"),
    NULL
  },
  {
    "record-window", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE,
    &record_window,
    N_("Record a video of the active window"),
    NULL
  },
  {
    "record-region", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE,
    &record_region,
    N_("Record a video of a selected region"),
    NULL
  },
```

- [ ] **Step 3: Add conflict checking and fix warning conditions**

In `src/main.c`, add the `any_record` and `any_screenshot` variables **at function scope** (near the top of `main()`, where other local variables are declared):

```c
  gboolean any_record, any_screenshot;
```

Then add the conflict checks after the existing region conflict checks (after line 193):

```c
  /* Check record flag conflicts */
  any_record = record_fullscreen || record_window || record_region;
  any_screenshot = fullscreen || window || region;

  if (any_record && any_screenshot)
    {
      g_printerr (_("Conflicting options: screenshot and record flags "
                     "cannot be used at the same time.\n"));
      return EXIT_FAILURE;
    }

  if ((record_fullscreen && record_window) ||
      (record_fullscreen && record_region) ||
      (record_window && record_region))
    {
      g_printerr (_("Conflicting options: only one --record-* flag "
                     "can be used at a time.\n"));
      return EXIT_FAILURE;
    }
```

Then update the existing warning conditions (lines 205-217) to also exclude recording mode. Replace the `!(fullscreen || window || region)` checks with `!(fullscreen || window || region) && !any_record`:

```c
  if ((application != NULL) && !(fullscreen || window || region) && !any_record)
    g_printerr (ignore_error, "open");
  if ((screenshot_dir != NULL) && !(fullscreen || window || region) && !any_record)
    {
      g_printerr (ignore_error, "save");
      screenshot_dir = NULL;
    }
  if (clipboard && !(fullscreen || window || region) && !any_record)
    g_printerr (ignore_error, "clipboard");
  if (delay && !(fullscreen || window || region) && !any_record)
    g_printerr (ignore_error, "delay");
  if (mouse && !(fullscreen || window || region) && !any_record)
    g_printerr (ignore_error, "mouse");
```

- [ ] **Step 4: Handle record CLI flags**

In `src/main.c`, add after the `if (fullscreen || window || region)` block (after line 346), before the `else` block:

```c
  else if (any_record)
    {
      sd->recording = TRUE;
      sd->region_specified = TRUE;
      sd->action_specified = FALSE;

      if (record_window)
        sd->region = ACTIVE_WINDOW;
      else if (record_fullscreen)
        sd->region = FULLSCREEN;
      else
        sd->region = SELECT;

      if (upload_r2)
        {
          sd->action = UPLOAD_R2;
          sd->action_specified = TRUE;
        }

      if (jira_issue != NULL)
        {
          sd->action |= POST_JIRA | UPLOAD_R2;
          sd->action_specified = TRUE;
          sd->jira_issue_key = g_strdup (jira_issue);
        }

      if (screenshot_dir != NULL)
        {
          sd->action |= SAVE;
          sd->action_specified = TRUE;
          GFile *save_dir = g_file_new_for_commandline_arg (screenshot_dir);
          g_free (sd->screenshot_dir);
          sd->screenshot_dir = g_file_get_uri (save_dir);
          g_object_unref (save_dir);
        }

      screenshooter_take_screenshot (sd, TRUE);
      gtk_main ();
    }
```

- [ ] **Step 5: Verify build**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 6: Test CLI recording**

Run: `./build/src/xfce4-screenshooter --record-fullscreen`
Expected: Recording starts, floating stop button appears in top-right corner with timer. Click "Stop Recording" → action dialog appears. Select "Upload to R2" → upload succeeds, URL shown.

- [ ] **Step 7: Commit**

```bash
git add src/main.c
git commit -m "feat: add --record-fullscreen/window/region CLI flags"
```

---

## Chunk 3: GUI Integration and Polish

### Task 6: Add "Record Video" checkbox to region dialog

**Files:**
- Modify: `lib/screenshooter-dialogs.c`

- [ ] **Step 1: Add recorder include**

In `lib/screenshooter-dialogs.c`, add after the cloud-config include:

```c
#include "screenshooter-recorder.h"
```

- [ ] **Step 2: Find the region dialog function**

Read `lib/screenshooter-dialogs.c` and find the `screenshooter_region_dialog_new()` or `screenshooter_region_dialog_show()` function. Look for where the region radio buttons (Fullscreen/Active Window/Rectangle) are added.

- [ ] **Step 3: Add "Record Video" checkbox**

After the Rectangle radio button `gtk_toggle_button_set_active` call (after line 997), **before** the "Options" label section starts (before line 999 where `box` gets reassigned), add to the region `box`:

```c
  /* Record Video checkbox */
  {
    GtkWidget *record_check = gtk_check_button_new_with_label (
      _("Record Video"));
    gboolean ffmpeg_available = screenshooter_recorder_available ();
    gboolean is_wayland = FALSE;

#ifdef ENABLE_WAYLAND
    {
      GdkDisplay *disp = gdk_display_get_default ();
      is_wayland = GDK_IS_WAYLAND_DISPLAY (disp);
    }
#endif

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (record_check),
                                  sd->recording);
    gtk_widget_set_sensitive (record_check,
                              ffmpeg_available && !is_wayland);

    if (!ffmpeg_available)
      gtk_widget_set_tooltip_text (record_check,
        _("FFmpeg is required for video recording. "
          "Install with: sudo apt install ffmpeg"));
    else if (is_wayland)
      gtk_widget_set_tooltip_text (record_check,
        _("Video recording is currently supported on X11 only."));
    else
      gtk_widget_set_tooltip_text (record_check,
        _("Record a video instead of taking a screenshot"));

    g_signal_connect (G_OBJECT (record_check), "toggled",
      G_CALLBACK (cb_record_toggled), sd);
    gtk_box_pack_start (GTK_BOX (box), record_check, FALSE, FALSE, 0);
  }
```

- [ ] **Step 4: Add the toggle callback**

Add the forward declaration in the static prototypes section at the top of the file (around line 85, near the other `cb_*` prototypes):

```c
static void
cb_record_toggled                  (GtkToggleButton    *tb,
                                    ScreenshotData     *sd);
```

Then add the callback implementation in the static callbacks section of the file (near the other `cb_*` implementations):

```c
static void
cb_record_toggled (GtkToggleButton *tb, ScreenshotData *sd)
{
  sd->recording = gtk_toggle_button_get_active (tb);
}
```

- [ ] **Step 5: Disable CLIPBOARD/OPEN in actions dialog when recording**

In `screenshooter_actions_dialog_new()`, find where the clipboard and open radio buttons are created. Add sensitivity checks:

For the clipboard radio button, after it's created:
```c
  if (sd->recording)
    gtk_widget_set_sensitive (radio, FALSE);
```

For the open radio button, after it's created:
```c
  if (sd->recording)
    gtk_widget_set_sensitive (radio, FALSE);
```

For the custom action radio button, after it's created:
```c
  if (sd->recording)
    gtk_widget_set_sensitive (radio, FALSE);
```

- [ ] **Step 6: Verify build**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 7: Commit**

```bash
git add lib/screenshooter-dialogs.c
git commit -m "feat: add Record Video checkbox to region dialog"
```

---

### Task 7: Increase R2 upload timeout for video files

**Files:**
- Modify: `lib/screenshooter-r2.c`

- [ ] **Step 1: Add dynamic timeout based on file extension**

In `lib/screenshooter-r2.c`, find the line `curl_easy_setopt (curl, CURLOPT_TIMEOUT, 30L);` and replace with:

```c
  {
    const gchar *ct = screenshooter_r2_content_type (extension);
    long timeout = 30L;
    if (g_str_has_prefix (ct, "video/"))
      timeout = 120L;
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, timeout);
  }
```

- [ ] **Step 2: Verify build**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 3: Commit**

```bash
git add lib/screenshooter-r2.c
git commit -m "feat: increase R2 upload timeout to 120s for video files"
```

---

### Task 8: End-to-end test

- [ ] **Step 1: Test CLI fullscreen recording**

Run: `./build/src/xfce4-screenshooter --record-fullscreen`
Expected: Recording starts, stop button appears. Click stop. Action dialog shows (CLIPBOARD/OPEN disabled). Select "Upload to R2" → uploads MP4 → shows URL.

- [ ] **Step 2: Test CLI with Jira**

Run: `./build/src/xfce4-screenshooter --record-fullscreen -j BNS-2727`
Expected: Records, uploads to R2, posts to Jira, shows success dialog.

- [ ] **Step 3: Test GUI flow**

Run: `./build/src/xfce4-screenshooter`
Expected: Region dialog shows "Record Video" checkbox. Check it, select Fullscreen, click OK. Recording starts, stop button appears. Stop → action dialog → Save works.

- [ ] **Step 4: Test ffmpeg not installed**

Run: `PATH=/usr/bin:/bin ./build/src/xfce4-screenshooter --record-fullscreen` (with ffmpeg temporarily removed from PATH)
Expected: Error dialog about ffmpeg not installed.

- [ ] **Step 5: Test conflict detection**

Run: `./build/src/xfce4-screenshooter --record-fullscreen --fullscreen`
Expected: Error message about conflicting options.

- [ ] **Step 6: Final commit and push**

```bash
git push origin master
git tag v1.1.0 -m "v1.1.0: Add video screen recording"
git push origin --tags
```
