# Video Blur Editor Design

## Overview

Add a standalone video editor to xfce4-screenshooter that allows users to open any MP4 file, draw blur regions on a frame preview with per-region timeline control, and export the blurred video. Uses FFmpeg subprocess for all media operations, matching the project's existing architecture. Intentionally restricted to MP4 format to limit scope.

## Entry Point

A new "Edit Video" button in the main screenshooter dialog's action area (alongside existing Save/Upload actions). Clicking it:
1. Checks FFmpeg/ffprobe availability (extends `screenshooter_recorder_is_available()`)
2. Opens a file chooser filtered to `*.mp4`
3. Extracts video metadata (duration, resolution, fps) via ffprobe
4. Launches the editor window

## New Files

| File | Purpose |
|------|---------|
| `lib/screenshooter-video-editor.c/.h` | Main editor window: GTK window lifecycle, layout, coordination |
| `lib/screenshooter-video-editor-canvas.c/.h` | Frame preview widget (GtkDrawingArea) with rectangle drawing overlay, coordinate mapping |
| `lib/screenshooter-video-editor-timeline.c/.h` | Timeline slider + per-region start/end time controls |
| `lib/screenshooter-video-editor-blur.c/.h` | Blur region data model + FFmpeg filter chain builder |

No new build dependencies. All frame extraction and blur processing via FFmpeg subprocess.

## Data Model

```c
typedef struct {
  gint x, y, w, h;       /* video-space pixel coordinates (not widget-space) */
  gdouble start_time;     /* seconds */
  gdouble end_time;       /* seconds */
  gboolean full_frame;    /* if TRUE, x/y/w/h are ignored */
} BlurRegion;

typedef struct {
  gchar *input_path;
  gint video_width;       /* original video resolution */
  gint video_height;
  gdouble duration;       /* seconds */
  gdouble fps;
  gboolean has_audio;     /* whether input has an audio stream */
  GList *regions;         /* list of BlurRegion* */
  gint blur_radius;       /* boxblur luma_radius, 2-30 */
} VideoEditorState;
```

## Editor Window Layout

```
+-----------------------------------------------------+
|  Edit Video - filename.mp4                     [X]   |
+------------------------------------------------------+
|                                                      |
|   +--------------------------------------------+    |
|   |                                            |    |
|   |         Video Frame Preview                |    |
|   |      (GtkDrawingArea with pixbuf)          |    |
|   |   (aspect-ratio preserved, letterboxed)    |    |
|   |                                            |    |
|   |    +----------+  <- drawn blur region      |    |
|   |    |  blurred |                            |    |
|   |    +----------+                            |    |
|   |                                            |    |
|   +--------------------------------------------+    |
|                                                      |
|   <----------*------------------------> 00:15/01:23  |
|              ^ timeline slider (GtkScale)            |
|                                                      |
|   +- Blur Regions ----------------------------+     |
|   | #1  [120,40 300x80]  00:05 -> 00:30  [x]  |     |
|   | #2  [Full Frame]     00:45 -> 01:00  [x]  |     |
|   |                          [+ Add Region]    |     |
|   +--------------------------------------------+     |
|                                                      |
|   Blur intensity: <----------*---------->            |
|                                                      |
|   [ ] Full frame blur                                |
|                                                      |
|           [Cancel]              [Apply & Save]       |
+------------------------------------------------------+
```

## Interactions

- **Draw region**: Click and drag on the frame preview to create a rectangle. Widget-space coordinates are transformed to video-space coordinates using the scale factor and offset from letterboxing. Added to the region list with current slider time as default start, video end as default end.
- **Edit region timing**: Click start/end values in the region list to edit via spinbuttons (seconds).
- **Delete region**: Trash button per region.
- **Full frame blur checkbox**: Adds a single full-frame region, disables rectangle drawing.
- **Blur intensity slider**: Controls FFmpeg `boxblur` strength (maps to `luma_radius` 2-30).
- **Scrub timeline**: Dragging the slider extracts the frame at that timestamp. Drawn regions overlay in real-time on the canvas.
- **Apply & Save**: Opens save dialog for output path, then runs FFmpeg.
- **Maximum 10 blur regions** to cap FFmpeg memory usage from stream splitting.

## Coordinate System Mapping

The canvas widget displays the video frame scaled to fit with preserved aspect ratio (letterboxing if needed). Coordinate transformation between widget-space and video-space:

```
scale = min(widget_width / video_width, widget_height / video_height)
offset_x = (widget_width - video_width * scale) / 2
offset_y = (widget_height - video_height * scale) / 2

/* widget-space -> video-space */
video_x = (widget_x - offset_x) / scale
video_y = (widget_y - offset_y) / scale

/* video-space -> widget-space (for drawing overlays) */
widget_x = video_x * scale + offset_x
widget_y = video_y * scale + offset_y
```

Mouse events outside the frame area (in letterbox regions) are ignored. All stored `BlurRegion` coordinates are in video pixel space, clamped to `[0, video_width)` and `[0, video_height)`.

## FFmpeg Integration

### Metadata Extraction (on file open)

Use `ffprobe` for structured JSON output (more reliable than parsing ffmpeg stderr):
```
ffprobe -v quiet -print_format json -show_streams -show_format input.mp4
```

Parse from JSON:
- `streams[0].width`, `streams[0].height` for resolution
- `streams[0].r_frame_rate` for fps
- `format.duration` for duration
- Check if any stream has `codec_type == "audio"` to set `has_audio`

Requires extending `screenshooter_recorder_is_available()` to also check for `ffprobe` in PATH.

### Frame Extraction (on scrub)

```
ffmpeg -ss <timestamp> -i input.mp4 -frames:v 1 /tmp/xfce-preview-XXXXXX.png
```

Use a temp file (consistent with existing `build_output_path()` pattern in `screenshooter-recorder.c`) and load with `gdk_pixbuf_new_from_file()`. Delete temp file after loading.

Extraction runs via `g_spawn_async()` with a `g_child_watch_add()` callback that loads the pixbuf and updates the canvas on the main loop. This prevents blocking the UI.

Note: `-ss` before `-i` uses input seeking (fast, keyframe-accurate). For screen recordings this is acceptable; frame-exact seeking is not critical for preview.

Debounce: on slider `value-changed`, reset a 200ms `g_timeout_add()`. Only trigger extraction when the timeout fires (user stopped dragging).

### Blur Filter Chain (on Apply)

**Algorithm for N regions:**

The filter chain uses `split` to create N+1 copies of the video stream (1 base + N for cropping), then sequentially overlays each blurred crop onto the base.

For N regions (max 10):
```
ffmpeg -i input.mp4 -filter_complex "
  [0:v]split=N+1[base][s0][s1]...[sN-1];
  [s0]crop=W0:H0:X0:Y0,boxblur=R[r0];
  [s1]crop=W1:H1:X1:Y1,boxblur=R[r1];
  ...
  [base][r0]overlay=X0:Y0:enable='between(t,S0,E0)'[t0];
  [t0][r1]overlay=X1:Y1:enable='between(t,S1,E1)'[t1];
  ...
" -map "[tN-1]" -map 0:a? -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -c:a copy output.mp4
```

Where:
- `R` = blur_radius from slider (2-30)
- `Xi,Yi,Wi,Hi` = region coordinates in video-space
- `Si,Ei` = region start/end time in seconds
- `-map 0:a?` conditionally maps audio (the `?` makes it optional, no error if no audio)

**Single region (simplified, uses `-vf` instead of `-filter_complex`):**
```
ffmpeg -i input.mp4 -vf "
  split[base][blur1];
  [blur1]crop=W:H:X:Y,boxblur=R[blurred1];
  [base][blurred1]overlay=X:Y:enable='between(t,S,E)'
" -map 0:a? -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -c:a copy output.mp4
```

**Full-frame blur:**
```
ffmpeg -i input.mp4 -vf "boxblur=R:enable='between(t,S,E)'" -map 0:a? -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -c:a copy output.mp4
```

**Re-encoding parameters** (matching existing recorder settings in `screenshooter-recorder.c`):
- `-c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p`
- `-c:a copy` (passthrough audio, no re-encode)
- `-map 0:a?` (optional audio mapping — handles videos with no audio stream)

### Progress Tracking

Use FFmpeg's `-progress pipe:2` flag to get machine-readable progress on stderr:
```
out_time_ms=15000000
```

Parse `out_time_ms` values, divide by total duration to compute percentage. Update progress bar via `g_io_watch` on the stderr fd. Spawn FFmpeg with `g_spawn_async_with_pipes()` to capture stderr.

## Error Handling

- **FFmpeg/ffprobe not found**: Error dialog before opening editor.
- **Invalid file**: Verify ffprobe returns valid metadata on open; reject with error message.
- **No audio stream**: Handled transparently via `-map 0:a?` (no special UI needed).
- **Frame extraction throttling**: Debounce slider scrub via `g_timeout_add(200, ...)`. Show spinner on canvas while extracting.
- **Large files**: Progress dialog with Cancel button. Cancel sends SIGINT to FFmpeg (same pattern as recording stop). On cancel, delete partial output.
- **Region validation**: Minimum 10x10 video-space pixels. Clamp to video dimensions. At least one region required for Apply. Maximum 10 regions.
- **Output file safety**: Default name `<original>_blurred.mp4`. Warn on overwrite. Never overwrite source file (append `_blurred` if same path chosen).

## Testing

### Unit Tests

`test-video-editor-blur.c`:
- FFmpeg filter chain string generation: single region, multiple regions (2, 5, 10), full-frame, overlapping time ranges, boundary values (blur at t=0, blur at t=duration)
- Coordinate clamping and minimum region size validation
- Audio stream conditional mapping (with and without audio)
- Region count cap enforcement

### Integration Tests

`test-video-editor-integration.c`:
- Generate short test video with FFmpeg (with and without audio)
- Apply blur via filter chain builder
- Verify output is valid MP4 (ffprobe can read it, duration preserved, resolution unchanged)

### Manual Testing

- Draw regions, scrub timeline, verify overlay matches
- Apply blur, verify placement and timing in video player
- Cancel mid-processing, verify cleanup
- Open invalid files, verify error dialogs
- Test with no-audio screen recordings
