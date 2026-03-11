# Video Recording Feature — Design Spec

**Date:** 2026-03-11
**Status:** Approved

## Overview

Add video screen recording to xfce4-screenshooter using FFmpeg as a subprocess. Recordings go through the same action flow as screenshots (Save/R2/Jira). X11 only for v1.

## Architecture

Recording adds a parallel capture mode alongside screenshots. Key addition: a `recording` boolean on `ScreenshotData` that switches the capture pipeline from single-frame to continuous recording.

### Flow

```
CLI flag (--record-fullscreen/--record-window/--record-region) or GUI toggle
  → Select region (reuse existing region selection)
  → Spawn ffmpeg subprocess
  → Show floating stop button with timer
  → User clicks Stop (or presses Escape)
  → ffmpeg receives SIGINT, writes MP4 trailer, exits
  → MP4 file path fed into action_idle()
  → Same action dialog: Save / Upload to R2 / Post to Jira
```

## Components

### 1. Recorder Module (`screenshooter-recorder.c/.h`)

```c
typedef struct {
  GPid    pid;           // FFmpeg process ID
  gchar  *output_path;  // /tmp/recording-YYYY-MM-DD_HH-MM-SS.mp4
  gint    region;        // FULLSCREEN, ACTIVE_WINDOW, SELECT
  gint    x, y, w, h;   // Capture area
} RecorderState;

RecorderState *screenshooter_recorder_start (gint region, gint x, gint y,
                                              gint w, gint h, GError **error);
gchar         *screenshooter_recorder_stop  (RecorderState *state, GError **error);
void           screenshooter_recorder_free  (RecorderState *state);
```

- `_start()`: Builds ffmpeg argv, spawns with `g_spawn_async()`, returns state
- `_stop()`: Sends `SIGINT` to ffmpeg, waits for clean exit, returns output file path
- `_free()`: Frees state struct and output_path string

**FFmpeg command:**
```
ffmpeg -y -f x11grab -framerate 30 -video_size WxH -i :DISPLAY+X,Y
       -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p
       /tmp/recording-YYYY-MM-DD_HH-MM-SS.mp4
```

- No audio capture (v1)
- 30fps, ultrafast preset, CRF 23 (~5-10 MB/min at 1080p)
- Output to `/tmp/` with timestamp filename

### 2. Floating Stop Button (`screenshooter-recorder-dialog.c/.h`)

```c
GtkWidget *screenshooter_recorder_dialog_new (RecorderState *state);
```

- Small undecorated window with red "Stop Recording" button and elapsed time label
- `gtk_window_set_keep_above(TRUE)` — always on top
- `gtk_window_set_decorated(FALSE)` — no title bar
- Positioned in top-right corner of screen
- Timer updated every second via `g_timeout_add(1000, ...)`
- Clicking Stop or pressing Escape → `screenshooter_recorder_stop()` → destroy dialog → feed MP4 into `action_idle()`
- Window close via WM treated same as Stop click

### 3. ScreenshotData Changes (`screenshooter-global.h`)

Add field:
```c
gboolean recording;  // TRUE = video mode, FALSE = screenshot mode
```

No new action enum values — SAVE, UPLOAD_R2, POST_JIRA all apply to recordings.

### 4. CLI Flags (`src/main.c`)

New options:
- `--record-fullscreen` / `-R` — record full screen
- `--record-window` / `-W` — record active window
- `--record-region` / `-E` — record selected region

These set `sd->recording = TRUE` and the appropriate region.

### 5. Action Flow Changes (`screenshooter-actions.c`)

Modified `take_screenshot_idle()`:
- If `sd->recording == TRUE`:
  1. Get region coordinates (reuse existing region selection for SELECT/ACTIVE_WINDOW)
  2. Call `screenshooter_recorder_start()`
  3. Show floating stop dialog (blocks via `gtk_dialog_run` or main loop)
  4. On stop: get MP4 path, set as `save_location`
  5. Fall through to `action_idle()` for Save/R2/Jira
- If `sd->recording == FALSE`: existing screenshot flow unchanged

### 6. GUI Changes (`screenshooter-dialogs.c`)

Add recording toggle in the region selection dialog so users can switch between screenshot and recording mode from the GUI (not just CLI).

### 7. R2 Upload (`screenshooter-r2.c`)

Already handles `video/mp4` content type. Only change: increase `CURLOPT_TIMEOUT` to 120s when uploading video files (vs 30s for images).

## Dependencies

- **FFmpeg**: Runtime dependency only (spawned as subprocess). Not a build dependency.
- No new library dependencies in `meson.build`.

## Error Handling

| Scenario | Handling |
|----------|----------|
| FFmpeg not installed | `g_find_program_in_path("ffmpeg")` check before start. Error dialog with install instructions. |
| FFmpeg crashes during recording | `g_child_watch_add()` monitors child process. Error dialog + temp file cleanup. |
| Stop dialog closed via WM | Treated as Stop click — SIGINT to ffmpeg. |
| Large video files | ultrafast + CRF 23 keeps size reasonable. R2 timeout extended to 120s. |
| Wayland display | Detect with `GDK_IS_WAYLAND_DISPLAY()`. Show error: "Video recording is currently supported on X11 only." |
| Multi-monitor | Capture the monitor where the active window is, using `gdk_screen_get_width/height()` for dimensions. |

## Out of Scope (v1)

- Audio recording (system or microphone)
- Wayland video recording (PipeWire-based, future version)
- Video format options (WebM, etc.)
- In-app video playback/preview
