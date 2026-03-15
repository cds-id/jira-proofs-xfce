# Windows Port & First-Run Wizard Design

**Date:** 2026-03-15
**Status:** Approved

## Overview

Cross-platform rewrite of xfce4-screenshooter to support both Linux and Windows from a shared codebase. The existing app is refactored into three layers: a platform-independent core library, a platform abstraction layer with per-OS backends, and separate UI layers (GTK3 on Linux, Win32 + resource dialogs on Windows). A first-run wizard launches when no cloud config file exists, guiding the user through R2 and Jira credential setup.

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Cross-platform strategy | Shared core + platform-specific UI | Native feel on each OS, clean separation |
| Windows screenshot API | BitBlt/PrintWindow (initial), Graphics Capture API (future) | C-friendly, DPI-aware via SetProcessDpiAwarenessContext, Win10+ |
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
# Core and platform built first (UI depends on them)
subdir('lib/core')
subdir('lib/platform')

if host_machine.system() == 'windows'
  subdir('lib/ui-win32')
  subdir('src')  # builds main-win32.c
else
  subdir('lib/ui-gtk')
  subdir('src')  # builds main.c
  subdir('panel-plugin')  # Linux only
endif
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

- **Capture:** `BitBlt` / `PrintWindow` for screenshots (C-friendly, no COM/WinRT needed). Per-monitor DPI handled via `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`. The Windows Graphics Capture API (WinRT/COM) is deferred as a future upgrade — it requires C++ interop which conflicts with the project's C-only codebase.
- **Recorder args:** Returns `-f gdigrab` for FFmpeg (upgradeable to DXGI pipe later)
- **Config:** Reads/writes `%APPDATA%\xfce4-screenshooter\cloud.toml` via GKeyFile. On save, restrictive ACLs are set via `SetFileSecurity()` to match Linux's `chmod 0600` behavior (owner-only access). See also Section 3 for credential protection details.
- **Clipboard:** Win32 `SetClipboardData()` with `CF_BITMAP`
- **Notifications:** Win32 toast notifications (`Shell_NotifyIcon`)

**Note:** GdkPixbuf is available as a standalone library on Windows (via MSYS2) and does not require GTK. This keeps the platform layer UI-toolkit-agnostic.

## 3. First-Run Wizard

### Trigger

On app startup, call `sc_platform_config_exists()`. If it returns `FALSE`, launch the wizard instead of the normal capture dialog. The wizard can also be re-launched manually via:
- **CLI flag:** `--setup-wizard` (forces wizard regardless of config state)
- **Menu item:** "Reconfigure Cloud Services" in the main dialog's menu (both GTK and Win32)

If the config file exists but fails to parse (corrupt file), the app logs a warning, deletes the corrupt file, and triggers the wizard.

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

- `sc_cloud_config_create_default()` — creates empty `CloudConfig` struct with NULL/empty fields
- `sc_r2_test_connection(const CloudConfig *config)` — validates R2 credentials via HEAD request to bucket (new, wraps existing curl logic). Returns `TRUE` on success, `FALSE` with `GError` on failure.
- `sc_jira_test_connection(const CloudConfig *config)` — validates Jira credentials via `GET /rest/api/3/myself` (new). Returns `TRUE` on success, `FALSE` with `GError` on failure.
- `sc_cloud_config_save(const CloudConfig *config)` — new function that serializes config to GKeyFile INI format (not strict TOML — GKeyFile natively writes INI which is a compatible subset for the key-value pairs used here). Creates the config directory if it doesn't exist (`g_mkdir_with_parents`). On Linux, sets file permissions to `0600`. On Windows, sets restrictive ACLs via `SetFileSecurity()` (owner-only read/write). Overwrites any existing config file.

### Config Format Clarification

The config file uses GKeyFile's native INI format (group headers with `[section]`, key-value pairs with `key=value`). The existing `cloud.toml` file happens to be compatible with GKeyFile because it only uses simple string values — no TOML-specific features (arrays, nested tables, datetime). The file extension remains `.toml` for backward compatibility but the format is technically INI.

### Relationship to Platform Config Abstraction

The `sc_platform_config_*()` functions in the platform layer handle general app preferences (capture mode, save location, etc.) and route through xfconf on Linux or a separate INI file on Windows. The `sc_cloud_config_*()` functions in the core layer handle cloud credentials specifically and always use direct file I/O (GKeyFile) on both platforms — they do NOT go through the platform abstraction, since the cloud config format and path are the same on both platforms (only the directory root differs, handled by `sc_platform_config_path()`).

### Config Paths

- Linux: `~/.config/xfce4-screenshooter/cloud.toml` (existing)
- Windows: `%APPDATA%\xfce4-screenshooter\cloud.toml`

## 4. Windows Capture & Recording

### Screenshots (BitBlt / PrintWindow)

- Uses `BitBlt` for fullscreen/region and `PrintWindow` for individual window capture
- DPI awareness set at process startup via `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`
- **Fullscreen:** `GetDC(NULL)` + `BitBlt` to capture entire desktop (multi-monitor via `EnumDisplayMonitors`)
- **Window:** enumerate via `EnumWindows`, user clicks to select, `PrintWindow` captures the target HWND (handles occluded windows)
- **Region:** overlay transparent fullscreen window, user drag-selects, `BitBlt` captures that rect
- Result converted to GdkPixbuf for the core library
- **Future upgrade:** Windows Graphics Capture API for HDR and hardware-accelerated capture (requires C++ interop layer)

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

- Win32 API (system) — GDI, Shell, User32
- Minimum: Windows 10 (for DPI awareness V2)
- MSYS2 MinGW packages for GLib, GdkPixbuf, libcurl, json-glib

## 7. CI

Add a Windows build job to GitHub Actions using MSYS2 + MinGW alongside the existing Linux CI. The Windows job compiles the app and runs core library tests (platform-independent tests only).

## 8. Testing Strategy

- **Core library tests:** Run on both platforms (blur filter chain, config parsing, etc.)
- **Platform backend tests:** Per-platform (mock or integration tests for capture, clipboard)
- **UI tests:** Manual testing per platform
- **Wizard tests:** Unit test the shared validation functions (`sc_r2_test_connection`, `sc_jira_test_connection`), manual test the UI flow
