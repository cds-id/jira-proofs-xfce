# Video Blur Editor Design

## Overview

Add a standalone video editor to xfce4-screenshooter that allows users to open any MP4 file, draw blur regions on a frame preview with per-region timeline control, and export the blurred video. Uses FFmpeg subprocess for all media operations, matching the project's existing architecture.

## Entry Point

A new "Edit Video" button in the main screenshooter dialog. Clicking it:
1. Checks FFmpeg availability (reuses `screenshooter_recorder_is_available()`)
2. Opens a file chooser filtered to `*.mp4`
3. Extracts video metadata (duration, resolution, fps) via FFmpeg
4. Launches the editor window

## New Files

| File | Purpose |
|------|---------|
| `lib/screenshooter-video-editor.c/.h` | Main editor window: GTK window lifecycle, layout, coordination |
| `lib/screenshooter-video-editor-canvas.c/.h` | Frame preview widget (GtkDrawingArea) with rectangle drawing overlay |
| `lib/screenshooter-video-editor-timeline.c/.h` | Timeline slider + per-region start/end time controls |
| `lib/screenshooter-video-editor-blur.c/.h` | Blur region data model + FFmpeg filter chain builder |

No new build dependencies. All frame extraction and blur processing via FFmpeg subprocess.

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

- **Draw region**: Click and drag on the frame preview to create a rectangle. Added to the region list with current slider time as default start, video end as default end.
- **Edit region timing**: Click start/end values in the region list to edit via spinbuttons (seconds).
- **Delete region**: Trash button per region.
- **Full frame blur checkbox**: Adds a single full-frame region, disables rectangle drawing.
- **Blur intensity slider**: Controls FFmpeg `boxblur` strength (maps to `luma_radius` 2-30).
- **Scrub timeline**: Dragging the slider extracts the frame at that timestamp. Drawn regions overlay in real-time on the canvas.
- **Apply & Save**: Opens save dialog for output path, then runs FFmpeg.

## FFmpeg Integration

### Frame Extraction (on scrub)

```
ffmpeg -ss <timestamp> -i input.mp4 -frames:v 1 -f image2pipe -vcodec png pipe:1
```

Output piped to stdout, loaded into GdkPixbuf directly. No temp files.

### Metadata Extraction (on file open)

```
ffmpeg -i input.mp4 2>&1
```

Parse duration, resolution, fps from stderr output.

### Blur Filter Chain (on Apply)

Single region:
```
ffmpeg -i input.mp4 -vf "
  split[base][blur1];
  [blur1]crop=300:80:120:40,boxblur=15[blurred1];
  [base][blurred1]overlay=120:40:enable='between(t,5,30)'
" -c:a copy output.mp4
```

Multiple regions:
```
ffmpeg -i input.mp4 -filter_complex "
  [0:v]split=3[base][b1][b2];
  [b1]crop=300:80:120:40,boxblur=15[r1];
  [b2]crop=W:H:0:0,boxblur=15[r2];
  [base][r1]overlay=120:40:enable='between(t,5,30)'[tmp];
  [tmp][r2]overlay=0:0:enable='between(t,45,60)'
" -c:a copy output.mp4
```

Full-frame blur:
```
ffmpeg -i input.mp4 -vf "boxblur=15:enable='between(t,45,60)'" -c:a copy output.mp4
```

Key details:
- `-c:a copy` preserves audio stream untouched
- Blur intensity slider maps to boxblur radius (2-30)
- Processing runs async with a progress dialog (parse FFmpeg `time=` output from stderr)
- On failure, show error dialog with stderr output

## Error Handling

- **FFmpeg not found**: Error dialog before opening editor.
- **Invalid file**: Verify FFmpeg can read metadata on open; reject with error message.
- **Frame extraction throttling**: Debounce slider scrub, extract frame after 200ms idle. Show spinner while extracting.
- **Large files**: Progress dialog with Cancel button. Cancel sends SIGINT to FFmpeg. On cancel, delete partial output.
- **Region validation**: Minimum 10x10 pixels. Clamp to video dimensions. At least one region required for Apply.
- **Output file safety**: Default name `<original>_blurred.mp4`. Warn on overwrite. Never overwrite source file (append `_blurred` if same path chosen).

## Testing

### Unit Tests

`test-video-editor-blur.c`:
- FFmpeg filter chain generation: single region, multiple regions, full-frame, overlapping time ranges, boundary values
- Coordinate clamping and minimum region size validation

### Integration Tests

`test-video-editor-integration.c`:
- Generate short test video with FFmpeg
- Apply blur via filter chain builder
- Verify output is valid MP4 (FFmpeg can read it, duration preserved)

### Manual Testing

- Draw regions, scrub timeline, verify overlay matches
- Apply blur, verify placement and timing in video player
- Cancel mid-processing, verify cleanup
- Open invalid files, verify error dialogs
