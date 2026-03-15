# Windows Port & First-Run Wizard Design

**Date:** 2026-03-15
**Status:** Approved

## Overview

Cross-platform rewrite of xfce4-screenshooter to support both Linux and Windows from a shared codebase. The existing app is refactored into three layers: a platform-independent core library, a platform abstraction layer with per-OS backends, and separate UI layers (GTK3 on Linux, Win32 + resource dialogs on Windows). A first-run wizard launches when no cloud config file exists, guiding the user through R2 and Jira credential setup.

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Cross-platform strategy | Shared core + platform-specific UI | Native feel on each OS, clean separation |
| Windows screenshot API | Windows Graphics Capture API | Modern, DPI-aware, HDR support, Win10 1803+ |
| Windows video recording | Start with `-f gdigrab`, upgrade to DXGI later | Simpler initial implementation, FFmpeg handles it |
| Windows UI toolkit | Win32 + .rc resource dialogs | No extra dependencies, native Windows look |
| Platform dependency handling | Abstraction layer | Clean interface, no `#ifdef` scattered through business logic |
| Windows packaging | NSIS installer | Traditional .exe installer, familiar to users |
| Wizard scope | Cloud config only (R2 + Jira) | Focused, app works without cloud features |

## 1. Repository & Build Structure

### Directory Layout

Additions to the existing repository:

```
├── lib/
│   ├── core/                          # libscreenshooter-core (platform-independent)
│   │   ├── sc-cloud-config.c/h        # TOML config parse/save
│   │   ├── sc-r2.c/h                  # R2 upload (libcurl)
│   │   ├── sc-jira.c/h                # Jira API (libcurl + json-glib)
│   │   ├── sc-recorder.c/h            # FFmpeg subprocess management
│   │   ├── sc-video-editor-blur.c/h   # Blur data model + filter chain
│   │   ├── sc-format.c/h              # Image format utils
│   │   └── meson.build
│   ├── platform/                      # libscreenshooter-platform (abstraction)
│   │   ├── sc-platform.h              # Abstract interface declarations
│   │   ├── sc-platform-linux.c        # Linux backend (X11/Wayland/xfconf)
│   │   ├── sc-platform-windows.c      # Windows backend (GCA/DXGI/file config)
│   │   └── meson.build
│   ├── ui-gtk/                        # Linux GTK3 UI (moved from current lib/)
│   │   ├── screenshooter-dialogs.c/h
│   │   ├── screenshooter-wizard.c/h   # GtkAssistant first-run wizard (new)
│   │   ├── screenshooter-video-editor*.c/h
│   │   └── meson.build
│   └── ui-win32/                      # Windows Win32 UI (new)
│       ├── win-main-dialog.c          # Main capture dialog
│       ├── win-wizard.c               # First-run wizard (Property Sheet)
│       ├── win-video-editor.c         # Video editor window
│       ├── resources.rc               # Dialog resource definitions
│       └── meson.build
├── src/
│   ├── main.c                         # Linux entry point (existing, updated)
│   └── main-win32.c                   # Windows entry point (new)
```

### Meson Build System

Meson has native Windows/MSVC support. The top-level `meson.build` uses `host_machine.system()` to conditionally build the correct UI and entry point:

```meson
if host_machine.system() == 'windows'
  subdir('lib/ui-win32')
  subdir('src')  # builds main-win32.c
else
  subdir('lib/ui-gtk')
  subdir('src')  # builds main.c
  subdir('panel-plugin')  # Linux only
endif

# Always built on both platforms
subdir('lib/core')
subdir('lib/platform')
```

## 2. Platform Abstraction Layer

### Interface (`sc-platform.h`)

```c
/* --- Capture --- */
typedef enum {
    SC_CAPTURE_FULLSCREEN,
    SC_CAPTURE_WINDOW,
    SC_CAPTURE_REGION
} ScCaptureMode;

typedef struct {
    gint x, y, width, height;
} ScRegion;

GdkPixbuf *sc_platform_capture (ScCaptureMode mode, ScRegion *region);
gboolean   sc_platform_select_region (ScRegion *out_region);

/* --- Screen Recording --- */
gchar    **sc_platform_recorder_args (ScCaptureMode mode, ScRegion *region);

/* --- Configuration --- */
gboolean   sc_platform_config_load (const gchar *channel, GKeyFile *out);
gboolean   sc_platform_config_save (const gchar *channel, GKeyFile *data);
gchar     *sc_platform_config_path (void);
gboolean   sc_platform_config_exists (void);

/* --- Clipboard --- */
gboolean   sc_platform_clipboard_copy_image (GdkPixbuf *pixbuf);

/* --- Notifications --- */
void       sc_platform_notify (const gchar *title, const gchar *body);
```

### Linux Backend (`sc-platform-linux.c`)

- **Capture:** Delegates to existing X11/Wayland capture code
- **Recorder args:** Returns `-f x11grab` or Wayland pipewire args
- **Config:** Wraps xfconf for load/save
- **Clipboard:** GTK clipboard (existing code)
- **Notifications:** libnotify or xfce4-notifyd

### Windows Backend (`sc-platform-windows.c`)

- **Capture:** Windows Graphics Capture API (`IGraphicsCaptureItem` + `Direct3D11CaptureFramePool`)
- **Recorder args:** Returns `-f gdigrab` for FFmpeg (upgradeable to DXGI pipe later)
- **Config:** Reads/writes `%APPDATA%\xfce4-screenshooter\cloud.toml` via GKeyFile
- **Clipboard:** Win32 `SetClipboardData()` with `CF_BITMAP`
- **Notifications:** Win32 toast notifications (`Shell_NotifyIcon`)

**Note:** GdkPixbuf is available as a standalone library on Windows (via MSYS2) and does not require GTK. This keeps the platform layer UI-toolkit-agnostic.

## 3. First-Run Wizard

### Trigger

On app startup, call `sc_platform_config_exists()`. If it returns `FALSE`, launch the wizard instead of the normal capture dialog.

### Wizard Pages

1. **Welcome** — "Welcome to Screenshooter. Let's set up your cloud services." Skip button exits wizard with empty config (app works without cloud features).

2. **R2 Setup** — Fields: Account ID, Access Key ID, Secret Access Key, Bucket Name, Public URL. "Test Connection" button validates credentials via HEAD request. All fields optional.

3. **Jira Setup** — Fields: Base URL, Email, API Token, Default Project Key. "Test Connection" button validates via `/rest/api/3/myself`. All fields optional.

4. **Done** — Summary of what was configured. "Finish" saves `cloud.toml` to the platform config path.

### Platform Implementations

- **Linux:** `GtkAssistant` widget (built-in multi-page wizard with Back/Next/Finish). File: `lib/ui-gtk/screenshooter-wizard.c`.
- **Windows:** Property Sheet wizard (`PSN_WIZNEXT` / `PSN_WIZBACK`) defined in `resources.rc`. File: `lib/ui-win32/win-wizard.c`.

### Shared Core Functions

Both UIs call the same core functions:

- `sc_cloud_config_create_default()` — creates empty config struct
- `sc_r2_test_connection()` — validates R2 credentials (new, wraps existing curl logic)
- `sc_jira_test_connection()` — validates Jira credentials (new, calls REST API)
- `sc_cloud_config_save()` — writes TOML to disk via platform config path

### Config Paths

- Linux: `~/.config/xfce4-screenshooter/cloud.toml` (existing)
- Windows: `%APPDATA%\xfce4-screenshooter\cloud.toml`

## 4. Windows Capture & Recording

### Screenshots (Windows Graphics Capture API)

- Uses `IGraphicsCaptureItem` + `Direct3D11CaptureFramePool`
- Supports per-monitor DPI awareness and multi-monitor setups
- **Fullscreen:** capture primary monitor (or let user pick)
- **Window:** enumerate via `EnumWindows`, user clicks to select, capture via HWND
- **Region:** overlay transparent fullscreen window, user drag-selects, capture rect
- Result converted to GdkPixbuf for the core library

### Video Recording

- Start with FFmpeg's built-in `-f gdigrab` input (simple, works out of the box)
- Upgrade path to DXGI Desktop Duplication pipe if performance is insufficient
- Audio capture via `-f dshow` with system audio loopback

### Region Selection Overlay (Windows)

- Transparent layered window (`WS_EX_LAYERED | WS_EX_TRANSPARENT`) covering full screen
- `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` / `WM_LBUTTONUP` for rubber-band selection
- GDI+ or Direct2D for the selection rectangle visual
- Returns `ScRegion` to caller

### FFmpeg on Windows

- Bundle `ffmpeg.exe` and `ffprobe.exe` with the NSIS installer
- Installed to app directory, found via relative path
- No user setup required

## 5. Migration Strategy

The existing Linux app stays functional at all times. Migration is phased:

### Phase 1 — Extract Core

Move platform-independent code from `lib/` into `lib/core/`. Move existing GTK UI code to `lib/ui-gtk/`. Update Meson build files. Verify Linux build passes.

### Phase 2 — Platform Abstraction

Create `lib/platform/sc-platform.h` and the Linux backend `sc-platform-linux.c` wrapping existing capture/config/clipboard code. Wire GTK UI to call through the abstraction. Verify Linux still works.

### Phase 3 — Windows Platform Backend

Implement `sc-platform-windows.c` with gdigrab capture, file-based config, Win32 clipboard. Get a minimal Windows build compiling.

### Phase 4 — Windows UI

Build Win32 dialogs: main capture dialog, first-run wizard, video editor. Wire to core + platform libraries.

### Phase 5 — Installer

NSIS installer bundling the app, FFmpeg, and runtime DLLs (GLib, GdkPixbuf, libcurl, json-glib).

## 6. Dependencies

### Both Platforms

- GLib >= 2.66.0
- GdkPixbuf
- libcurl >= 7.68.0
- json-glib >= 1.4.0
- FFmpeg/FFprobe binaries (subprocess)

### Linux Only

- GTK3 >= 3.24.0
- libxfce4util >= 4.18.0
- libxfce4ui >= 4.18.0
- libxfconf >= 4.18.0
- libxfce4panel >= 4.18.0 (panel plugin)
- X11 libs (libx11, xinput, xext, xfixes)
- Wayland libs (wayland-client, gtk-layer-shell, wayland-protocols)

### Windows Only

- Win32 API (system)
- Windows Graphics Capture API (Win10 1803+)
- MSYS2 MinGW packages for GLib, GdkPixbuf, libcurl, json-glib

## 7. CI

Add a Windows build job to GitHub Actions using MSYS2 + MinGW alongside the existing Linux CI. The Windows job compiles the app and runs core library tests (platform-independent tests only).

## 8. Testing Strategy

- **Core library tests:** Run on both platforms (blur filter chain, config parsing, etc.)
- **Platform backend tests:** Per-platform (mock or integration tests for capture, clipboard)
- **UI tests:** Manual testing per platform
- **Wizard tests:** Unit test the shared validation functions (`sc_r2_test_connection`, `sc_jira_test_connection`), manual test the UI flow
