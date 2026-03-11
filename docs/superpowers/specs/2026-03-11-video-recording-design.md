# Video Recording Feature — Design Spec

**Date:** 2026-03-11
**Status:** Approved

## Overview

Add video screen recording to xfce4-screenshooter using FFmpeg as a subprocess. Recordings go through a dedicated action flow that reuses the cloud upload infrastructure (Save/R2/Jira). X11 only for v1.

## Architecture

Recording adds a parallel capture mode alongside screenshots. Key addition: a `recording` boolean on `ScreenshotData` that switches the capture pipeline from single-frame to continuous recording.

### Flow

```
CLI flag (--record-fullscreen/--record-window/--record-region) or GUI toggle
  → Check ffmpeg is installed
  → Check X11 (reject Wayland with error)
  → Select region (reuse existing region selection)
  → Spawn ffmpeg subprocess
  → Show floating stop button with timer
  → User clicks Stop (or presses Escape)
  → ffmpeg receives SIGINT, writes MP4 trailer, exits
  → MP4 file path set as save_location
  → action_idle_recording() handles Save/R2/Jira (skips GdkPixbuf code)
```

## Components

### 1. Recorder Module (`screenshooter-recorder.c/.h`)

```c
typedef struct {
  GPid    pid;           // FFmpeg process ID
  gchar  *output_path;  // /tmp/recording-YYYY-MM-DD_HH-MM-SS.mp4
  gint    region;        // Uses existing enum: FULLSCREEN, ACTIVE_WINDOW, SELECT
  gint    x, y, w, h;   // Capture area
} RecorderState;

RecorderState *screenshooter_recorder_start (gint region, gint x, gint y,
                                              gint w, gint h, GError **error);
gchar         *screenshooter_recorder_stop  (RecorderState *state, GError **error);
void           screenshooter_recorder_free  (RecorderState *state);
gboolean       screenshooter_recorder_available (void);
                                              // Checks ffmpeg in PATH
```

- `_start()`: Builds ffmpeg argv, spawns with `g_spawn_async()`, returns state
- `_stop()`: Sends `SIGINT` to ffmpeg, waits for clean exit with `waitpid()`, returns output file path
- `_free()`: Frees state struct and output_path string
- `_available()`: Returns `g_find_program_in_path("ffmpeg") != NULL`

**FFmpeg command:**
```
ffmpeg -y -f x11grab -framerate 30 -video_size WxH -i :0.0+X,Y
       -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p
       /tmp/recording-YYYY-MM-DD_HH-MM-SS.mp4
```

**Display string:** Parsed from `$DISPLAY` env var. Use `g_getenv("DISPLAY")`, default to `:0.0`. Strip any hostname prefix (e.g. `localhost:10.0` → `:10.0`). The `-i` value is `$DISPLAY+X,Y` where X,Y are the region offset.

- No audio capture (v1)
- 30fps, ultrafast preset, CRF 23 (~5-10 MB/min at 1080p)
- Output to `/tmp/` with timestamp filename
- `region` field uses the same enum values from `screenshooter-global.h`

### 2. Floating Stop Button (`screenshooter-recorder-dialog.c/.h`)

```c
typedef void (*RecorderStopCallback) (const gchar *output_path, gpointer user_data);

void screenshooter_recorder_dialog_run (RecorderState *state,
                                         RecorderStopCallback callback,
                                         gpointer user_data);
```

- Small undecorated GtkWindow (not GtkDialog) with red "Stop Recording" button and elapsed time label (MM:SS)
- `gtk_window_set_keep_above(TRUE)` — always on top
- `gtk_window_set_decorated(FALSE)` — no title bar
- Positioned in top-right corner using `gdk_monitor_get_geometry()` on the primary monitor
- Timer updated every second via `g_timeout_add(1000, ...)`
- Stop triggers: clicking Stop button, pressing Escape key, WM close — all call `screenshooter_recorder_stop()` then invoke the callback with the MP4 path
- Uses `g_child_watch_add()` to monitor ffmpeg process — if it exits unexpectedly, show error dialog, clean up temp file, invoke callback with NULL
- Runs in the main GTK event loop (not nested `gtk_dialog_run`)

### 3. ScreenshotData Changes (`screenshooter-global.h`)

Add field:
```c
gboolean recording;  // TRUE = video mode, FALSE = screenshot mode
```

No new action enum values — SAVE, UPLOAD_R2, POST_JIRA apply to recordings.

**Unsupported actions in recording mode:** CLIPBOARD (requires GdkPixbuf), OPEN (not meaningful for video). These actions are ignored when `recording == TRUE`. The actions dialog hides/disables them in recording mode.

### 4. CLI Flags (`src/main.c`)

New long-only options (no short flags, following `--no-border` and `--supported-formats` precedent):
- `--record-fullscreen` — record full screen
- `--record-window` — record active window
- `--record-region` — record selected region

These set `sd->recording = TRUE` and the appropriate region, with `sd->action_specified = TRUE`.

**Conflict handling:** Record flags are mutually exclusive with screenshot flags (`--fullscreen`, `--window`, `--region`). If both are specified, print error and exit. Record flags are also mutually exclusive with each other.

### 5. Action Flow Changes (`screenshooter-actions.c`)

The recording flow branches in `screenshooter_take_screenshot()`, **before** `take_screenshot_idle()` is called:

```c
void screenshooter_take_screenshot (ScreenshotData *sd, gboolean immediate)
{
  if (sd->recording)
    {
      // Branch to recording flow — never enters take_screenshot_idle
      screenshooter_start_recording (sd);
      return;
    }
  // ... existing screenshot flow unchanged
}
```

New function `screenshooter_start_recording()`:
1. Check `screenshooter_recorder_available()` — error dialog if not
2. Check X11 display — error dialog if Wayland
3. For SELECT region: run existing region selection dialog to get coordinates
4. For ACTIVE_WINDOW: get focused window geometry
5. For FULLSCREEN: use `gdk_monitor_get_geometry()` on current monitor
6. Call `screenshooter_recorder_start(region, x, y, w, h)`
7. Show stop dialog via `screenshooter_recorder_dialog_run()`
8. On stop callback: call `action_idle_recording(save_location, sd)`

New function `action_idle_recording()`:
- Handles only file-based actions (no GdkPixbuf involvement)
- If no action specified: show action dialog (with CLIPBOARD/OPEN disabled)
- SAVE: move temp file to user-chosen location
- UPLOAD_R2 / POST_JIRA: same cloud upload flow using the file path directly
- Clean up temp file after upload if not saved

### 6. GUI Changes (`screenshooter-dialogs.c`)

Add a "Record Video" checkbox in the region selection dialog:
- Placed below the region radio buttons (Fullscreen/Active Window/Rectangle)
- When checked, sets `sd->recording = TRUE`
- Disabled with tooltip if ffmpeg is not installed
- Disabled with tooltip if running on Wayland

### 7. R2 Upload (`screenshooter-r2.c`)

Already handles `video/mp4` content type via `screenshooter_r2_content_type()`. Changes:
- Detect file extension, if video (`mp4`/`webm`) set `CURLOPT_TIMEOUT` to 120s instead of 30s

### 8. Temp File Cleanup

- On successful save: temp file in `/tmp/` is removed after copying to save location
- On successful R2 upload (without save): temp file removed after upload completes
- On ffmpeg crash: temp file removed in error handler
- On application exit during recording: `SIGINT` sent to ffmpeg in cleanup, temp file removed
- Known limitation: if the process is killed (SIGKILL), temp files may remain in `/tmp/` (OS cleans `/tmp/` on reboot)

## Dependencies

- **FFmpeg**: Runtime dependency only (spawned as subprocess). Not a build dependency.
- No new library dependencies in `meson.build`.

## Error Handling

| Scenario | Handling |
|----------|----------|
| FFmpeg not installed | `screenshooter_recorder_available()` check. Error dialog: "FFmpeg is required for video recording. Install with: sudo apt install ffmpeg" |
| FFmpeg crashes during recording | `g_child_watch_add()` monitors child. Error dialog + temp file cleanup. |
| Stop dialog closed via WM | Treated as Stop click — SIGINT to ffmpeg. |
| Large video files | ultrafast + CRF 23 keeps size reasonable. R2 timeout extended to 120s. |
| Wayland display | `GDK_IS_WAYLAND_DISPLAY()` check before starting. Error: "Video recording is currently supported on X11 only." |
| Multi-monitor | Use `gdk_monitor_get_geometry()` on the primary/current monitor for correct per-monitor dimensions. |
| Conflicting CLI flags | Print usage error and exit if screenshot + record flags mixed. |

## Out of Scope (v1)

- Audio recording (system or microphone)
- Wayland video recording (PipeWire-based, future version)
- Video format options (WebM, etc.)
- In-app video playback/preview
- Maximum recording duration limit
