# Windows Port Phase 4-5: Win32 UI & NSIS Installer

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Windows Win32 UI (main capture dialog, first-run wizard, video editor) and NSIS installer, completing the Windows port.

**Architecture:** Create `lib/ui-win32/` with Win32 API dialogs defined in `.rc` resource files and implemented in C. The Win32 UI links against `libscreenshooter-core` and `libscreenshooter-platform` (same as GTK UI). Replace the minimal `main-win32.c` with a full Windows app. Bundle everything with an NSIS installer. All code is written on Linux, validated by GitHub Actions MSYS2 CI.

**Tech Stack:** C (gnu11), Win32 API (User32, ComCtl32, Shell32), `.rc` resource files, Meson, NSIS, MSYS2/MinGW64

**Spec:** `docs/superpowers/specs/2026-03-15-windows-port-wizard-design.md` (Sections 1, 3, 4, 5)

---

## File Map

### Files to create

| File | Responsibility |
|------|---------------|
| `lib/ui-win32/resource.h` | Dialog/control IDs for .rc file |
| `lib/ui-win32/resources.rc` | Win32 dialog templates (main dialog, wizard pages) |
| `lib/ui-win32/win-main-dialog.c` | Main capture dialog (fullscreen/window/region, action selection) |
| `lib/ui-win32/win-main-dialog.h` | Main dialog API |
| `lib/ui-win32/win-wizard.c` | Property Sheet wizard (4 pages: Welcome, R2, Jira, Done) |
| `lib/ui-win32/win-wizard.h` | Wizard API |
| `lib/ui-win32/win-video-editor.c` | Video editor window (stub — launches ffmpeg CLI, full UI deferred) |
| `lib/ui-win32/win-video-editor.h` | Video editor API |
| `lib/ui-win32/meson.build` | Build for Win32 UI static library |
| `installer/screenshooter.nsi` | NSIS installer script |

### Files to modify

| File | Change |
|------|--------|
| `src/main-win32.c` | Replace validation stub with full Windows app (CLI parsing, wizard trigger, capture flow) |
| `src/meson.build` | Link Windows executable against `libscreenshooter_win32` |
| `meson.build` | Add `subdir('lib/ui-win32')` for Windows builds |
| `.github/workflows/ci-windows.yml` | Add NSIS install, build installer |

---

## Chunk 1: Win32 UI Library

### Task 1: Create resource header and dialog resources

**Files:**
- Create: `lib/ui-win32/resource.h`
- Create: `lib/ui-win32/resources.rc`

- [ ] **Step 1: Create resource header with dialog/control IDs**

Create `lib/ui-win32/resource.h`:

```c
#ifndef __WIN_RESOURCE_H__
#define __WIN_RESOURCE_H__

/* Main capture dialog */
#define IDD_MAIN_DIALOG         100
#define IDC_RADIO_FULLSCREEN    1001
#define IDC_RADIO_WINDOW        1002
#define IDC_RADIO_REGION        1003
#define IDC_CHECK_MOUSE         1004
#define IDC_SPIN_DELAY          1005
#define IDC_COMBO_ACTION        1006
#define IDC_BTN_CAPTURE         1007
#define IDC_BTN_PREFERENCES     1008
#define IDC_BTN_CLOUD_SETUP     1009
#define IDC_GROUP_REGION        1010
#define IDC_GROUP_OPTIONS       1011
#define IDC_GROUP_ACTION        1012
#define IDC_LABEL_DELAY         1013

/* Wizard pages */
#define IDD_WIZARD_WELCOME      200
#define IDD_WIZARD_R2           201
#define IDD_WIZARD_JIRA         202
#define IDD_WIZARD_DONE         203

#define IDC_WIZARD_LABEL        2001
#define IDC_R2_ACCOUNT_ID       2010
#define IDC_R2_ACCESS_KEY       2011
#define IDC_R2_SECRET_KEY       2012
#define IDC_R2_BUCKET           2013
#define IDC_R2_PUBLIC_URL       2014
#define IDC_R2_TEST_BTN         2015
#define IDC_R2_TEST_RESULT      2016
#define IDC_JIRA_BASE_URL       2020
#define IDC_JIRA_EMAIL          2021
#define IDC_JIRA_API_TOKEN      2022
#define IDC_JIRA_PROJECT        2023
#define IDC_JIRA_TEST_BTN       2024
#define IDC_JIRA_TEST_RESULT    2025
#define IDC_DONE_SUMMARY        2030

/* App icon */
#define IDI_APP_ICON            300

#endif
```

- [ ] **Step 2: Create Win32 resource file**

Create `lib/ui-win32/resources.rc`:

```rc
#include <windows.h>
#include "resource.h"

/* Main capture dialog */
IDD_MAIN_DIALOG DIALOGEX 0, 0, 280, 230
STYLE DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER
CAPTION "Screenshooter"
FONT 9, "Segoe UI"
BEGIN
    GROUPBOX "Capture Region", IDC_GROUP_REGION, 7, 7, 266, 65
    AUTORADIOBUTTON "Entire screen", IDC_RADIO_FULLSCREEN, 15, 20, 120, 10, WS_GROUP
    AUTORADIOBUTTON "Active window", IDC_RADIO_WINDOW, 15, 35, 120, 10
    AUTORADIOBUTTON "Select region", IDC_RADIO_REGION, 15, 50, 120, 10

    GROUPBOX "Options", IDC_GROUP_OPTIONS, 7, 78, 266, 40
    AUTOCHECKBOX "Include mouse pointer", IDC_CHECK_MOUSE, 15, 93, 120, 10
    LTEXT "Delay (seconds):", IDC_LABEL_DELAY, 150, 93, 65, 10
    EDITTEXT IDC_SPIN_DELAY, 220, 91, 40, 14, ES_NUMBER | WS_BORDER

    GROUPBOX "Action", IDC_GROUP_ACTION, 7, 123, 266, 40
    COMBOBOX IDC_COMBO_ACTION, 15, 138, 248, 100, CBS_DROPDOWNLIST | WS_VSCROLL

    PUSHBUTTON "Cloud Services...", IDC_BTN_CLOUD_SETUP, 7, 175, 90, 14
    DEFPUSHBUTTON "Capture", IDC_BTN_CAPTURE, 165, 175, 50, 14
    PUSHBUTTON "Cancel", IDCANCEL, 220, 175, 50, 14
END

/* Wizard: Welcome page */
IDD_WIZARD_WELCOME DIALOGEX 0, 0, 317, 143
STYLE DS_CONTROL | WS_CHILD
FONT 9, "Segoe UI"
BEGIN
    LTEXT "Welcome to Screenshooter.\n\nLet's set up your cloud services for uploading screenshots and posting to Jira.\n\nAll fields are optional - you can skip this wizard and configure later using --setup-wizard.", IDC_WIZARD_LABEL, 7, 7, 303, 80
END

/* Wizard: R2 page */
IDD_WIZARD_R2 DIALOGEX 0, 0, 317, 143
STYLE DS_CONTROL | WS_CHILD
FONT 9, "Segoe UI"
BEGIN
    LTEXT "Account ID:", -1, 7, 10, 80, 10
    EDITTEXT IDC_R2_ACCOUNT_ID, 95, 8, 215, 14, WS_BORDER

    LTEXT "Access Key ID:", -1, 7, 30, 80, 10
    EDITTEXT IDC_R2_ACCESS_KEY, 95, 28, 215, 14, WS_BORDER

    LTEXT "Secret Access Key:", -1, 7, 50, 80, 10
    EDITTEXT IDC_R2_SECRET_KEY, 95, 48, 215, 14, WS_BORDER | ES_PASSWORD

    LTEXT "Bucket:", -1, 7, 70, 80, 10
    EDITTEXT IDC_R2_BUCKET, 95, 68, 215, 14, WS_BORDER

    LTEXT "Public URL:", -1, 7, 90, 80, 10
    EDITTEXT IDC_R2_PUBLIC_URL, 95, 88, 215, 14, WS_BORDER

    PUSHBUTTON "Test Connection", IDC_R2_TEST_BTN, 7, 112, 80, 14
    LTEXT "", IDC_R2_TEST_RESULT, 95, 114, 215, 10
END

/* Wizard: Jira page */
IDD_WIZARD_JIRA DIALOGEX 0, 0, 317, 143
STYLE DS_CONTROL | WS_CHILD
FONT 9, "Segoe UI"
BEGIN
    LTEXT "Base URL:", -1, 7, 10, 80, 10
    EDITTEXT IDC_JIRA_BASE_URL, 95, 8, 215, 14, WS_BORDER

    LTEXT "Email:", -1, 7, 30, 80, 10
    EDITTEXT IDC_JIRA_EMAIL, 95, 28, 215, 14, WS_BORDER

    LTEXT "API Token:", -1, 7, 50, 80, 10
    EDITTEXT IDC_JIRA_API_TOKEN, 95, 48, 215, 14, WS_BORDER | ES_PASSWORD

    LTEXT "Default Project:", -1, 7, 70, 80, 10
    EDITTEXT IDC_JIRA_PROJECT, 95, 68, 215, 14, WS_BORDER

    PUSHBUTTON "Test Connection", IDC_JIRA_TEST_BTN, 7, 92, 80, 14
    LTEXT "", IDC_JIRA_TEST_RESULT, 95, 94, 215, 10
END

/* Wizard: Done page */
IDD_WIZARD_DONE DIALOGEX 0, 0, 317, 143
STYLE DS_CONTROL | WS_CHILD
FONT 9, "Segoe UI"
BEGIN
    LTEXT "Setup complete!", -1, 7, 7, 303, 10
    LTEXT "", IDC_DONE_SUMMARY, 7, 25, 303, 110
END
```

- [ ] **Step 3: Commit**

```bash
git add lib/ui-win32/resource.h lib/ui-win32/resources.rc
git commit -m "feat: add Win32 resource definitions (dialogs, wizard pages)"
```

---

### Task 2: Implement Win32 wizard

**Files:**
- Create: `lib/ui-win32/win-wizard.h`
- Create: `lib/ui-win32/win-wizard.c`

- [ ] **Step 1: Create wizard header**

```c
#ifndef __WIN_WIZARD_H__
#define __WIN_WIZARD_H__

#include <glib.h>

/* Returns TRUE if wizard completed, FALSE if skipped/cancelled */
gboolean win_wizard_run (const gchar *config_dir);

#endif
```

- [ ] **Step 2: Implement Property Sheet wizard**

Create `lib/ui-win32/win-wizard.c`. Implementation using Win32 Property Sheet API:

- 4 pages via `PROPSHEETPAGE` + `PropertySheet()`:
  - Welcome: `IDD_WIZARD_WELCOME`, `PSP_HIDEHEADER` for wizard intro style
  - R2: `IDD_WIZARD_R2`, page proc handles `IDC_R2_TEST_BTN` click → `sc_r2_test_connection()` → update `IDC_R2_TEST_RESULT` label
  - Jira: `IDD_WIZARD_JIRA`, same pattern with `sc_jira_test_connection()`
  - Done: `IDD_WIZARD_DONE`, `PSN_SETACTIVE` builds summary text from config state

- Property sheet flags: `PSH_WIZARD | PSH_PROPSHEETPAGE`
- On `PSN_WIZFINISH`: build CloudConfig from page controls, call `sc_cloud_config_save(config, config_dir, &error)`
- On cancel: save empty default config so wizard doesn't re-trigger
- Use `GetDlgItemTextW` + `g_utf16_to_utf8` to read entries
- Store `config_dir` and a shared `CloudConfig*` in page `lParam`/`GWLP_USERDATA`

Includes:
```c
#include "win-wizard.h"
#include "resource.h"
#include <sc-cloud-config.h>
#include <sc-r2.h>
#include <sc-jira.h>
#include <sc-platform.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <commctrl.h>
#include <prsht.h>
#endif
```

- [ ] **Step 3: Commit**

```bash
git add lib/ui-win32/win-wizard.h lib/ui-win32/win-wizard.c
git commit -m "feat: add Win32 Property Sheet wizard for cloud config"
```

---

### Task 3: Implement Win32 main capture dialog

**Files:**
- Create: `lib/ui-win32/win-main-dialog.h`
- Create: `lib/ui-win32/win-main-dialog.c`

- [ ] **Step 1: Create main dialog header**

```c
#ifndef __WIN_MAIN_DIALOG_H__
#define __WIN_MAIN_DIALOG_H__

#include <glib.h>

/* Show the main capture dialog. Returns the chosen action. */
gint win_main_dialog_run (void);

#endif
```

- [ ] **Step 2: Implement main dialog**

Create `lib/ui-win32/win-main-dialog.c`. Uses `DialogBoxParam(IDD_MAIN_DIALOG, ...)`:

- Init: set radio button `IDC_RADIO_FULLSCREEN` checked, populate action combo box (Save, Clipboard, Open, Upload R2, Post Jira)
- `IDC_BTN_CAPTURE` click: read selected region mode + action, end dialog with result
- `IDC_BTN_CLOUD_SETUP` click: call `win_wizard_run(config_dir)`
- `IDCANCEL`: end dialog with 0

The dialog returns a struct or encoded int with capture settings. The caller (`main-win32.c`) uses these to call `sc_platform_capture()` and execute the action.

- [ ] **Step 3: Commit**

```bash
git add lib/ui-win32/win-main-dialog.h lib/ui-win32/win-main-dialog.c
git commit -m "feat: add Win32 main capture dialog"
```

---

### Task 3.5: Create Win32 video editor stub

**Files:**
- Create: `lib/ui-win32/win-video-editor.h`
- Create: `lib/ui-win32/win-video-editor.c`

The spec requires `win-video-editor.c`. For Phase 4, implement a minimal stub that launches the video editor via `sc_recorder_*` / `video_editor_*` core APIs. The full interactive canvas UI is deferred — for now, provide a file-open dialog and pass the file to the core blur pipeline.

- [ ] **Step 1: Create video editor header**

```c
#ifndef __WIN_VIDEO_EDITOR_H__
#define __WIN_VIDEO_EDITOR_H__

#include <glib.h>

/* Launch the video editor for the given file (or file-open dialog if NULL) */
void win_video_editor_run (const gchar *filepath);

/* Returns TRUE if ffmpeg/ffprobe are available */
gboolean win_video_editor_available (void);

#endif
```

- [ ] **Step 2: Implement video editor stub**

Create `lib/ui-win32/win-video-editor.c`:
- `win_video_editor_available()`: calls `sc_recorder_available()`
- `win_video_editor_run(filepath)`:
  - If filepath is NULL, show `GetOpenFileName` dialog filtered to `*.mp4`
  - Probe metadata via `video_editor_probe_metadata()`
  - Show a `MessageBox` with video info and "Video editor UI coming soon" message
  - For now, no interactive editing — just validates the pipeline works

- [ ] **Step 3: Commit**

```bash
git add lib/ui-win32/win-video-editor.h lib/ui-win32/win-video-editor.c
git commit -m "feat: add Win32 video editor stub"
```

---

### Task 4: Create Win32 UI meson.build

**Files:**
- Create: `lib/ui-win32/meson.build`
- Modify: `meson.build` (top-level)

- [ ] **Step 1: Write Win32 UI meson build**

Create `lib/ui-win32/meson.build`. Note: resource compilation (.rc) is done here but the compiled resource object is exported via a variable for `src/meson.build` to link into the executable (Meson propagates variables from `subdir()` to the parent scope).

```meson
windows = import('windows')

# Compiled .rc resource — used by src/meson.build for the final executable
win32_resources = windows.compile_resources(
  'resources.rc',
  include_directories: include_directories('.'),
)

libscreenshooter_win32_sources = [
  'win-wizard.c',
  'win-wizard.h',
  'win-main-dialog.c',
  'win-main-dialog.h',
  'win-video-editor.c',
  'win-video-editor.h',
  'resource.h',
]

libscreenshooter_win32 = static_library(
  'screenshooter-win32',
  libscreenshooter_win32_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: [
    glib,
    libscreenshooter_core_dep,
    libscreenshooter_platform_dep,
    cc.find_library('comctl32'),
    cc.find_library('comdlg32'),
  ],
  install: false,
)

libscreenshooter_win32_dep = declare_dependency(
  link_with: libscreenshooter_win32,
  include_directories: include_directories('.'),
)
```

- [ ] **Step 2: Update top-level meson.build**

In the existing top-level `meson.build`, add a Windows-only block between `subdir('lib/core')` and the Linux conditional. The structure should be:

```meson
subdir('lib/platform')
subdir('lib/core')

if host_machine.system() == 'windows'
  subdir('lib/ui-win32')
endif

if host_machine.system() != 'windows'
  subdir('icons')
  subdir('po')
  subdir('protocols')
  subdir('lib/ui-gtk')
  subdir('panel-plugin')
endif

subdir('src')
```

The `win32_resources` variable defined in `lib/ui-win32/meson.build` will be visible in the parent scope (and thus in `src/meson.build`) because Meson propagates variables upward from `subdir()`.

- [ ] **Step 3: Commit**

```bash
git add lib/ui-win32/meson.build meson.build
git commit -m "build: add Win32 UI library to meson build system"
```

---

### Task 5: Update main-win32.c to full Windows app

**Files:**
- Modify: `src/main-win32.c`
- Modify: `src/meson.build`

- [ ] **Step 1: Replace validation stub with full app**

Rewrite `src/main-win32.c`:

```c
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
  { "save", 's', 0, G_OPTION_ARG_FILENAME, &screenshot_dir, "Save to file/directory", NULL },
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
```

- [ ] **Step 2: Update src/meson.build Windows block**

Update the Windows executable to link against the Win32 UI library and include the compiled resources. `win32_resources` is defined in `lib/ui-win32/meson.build` and propagated to parent scope via Meson's `subdir()` scoping rules:

```meson
if host_machine.system() == 'windows'
  executable(
    'xfce4-screenshooter',
    'main-win32.c',
    win32_resources,
    include_directories: [
      include_directories('..'),
      include_directories('..' / 'lib' / 'ui-win32'),
    ],
    dependencies: [
      glib,
      dependency('gdk-pixbuf-2.0'),
      dependency('gobject-2.0'),
      libscreenshooter_core_dep,
      libscreenshooter_platform_dep,
      libscreenshooter_win32_dep,
    ],
    install: true,
  )
else
  # ... existing Linux code ...
endif
```

- [ ] **Step 3: Verify Linux build unaffected**

Run: `ninja -C builddir`

- [ ] **Step 4: Commit**

```bash
git add src/main-win32.c src/meson.build
git commit -m "feat: replace Windows validation stub with full app"
```

---

## Chunk 2: NSIS Installer & CI

### Task 6: Create NSIS installer script

**Files:**
- Create: `installer/screenshooter.nsi`

- [ ] **Step 1: Create installer directory and NSIS script**

```bash
mkdir -p installer
```

Create `installer/screenshooter.nsi`:

```nsis
!include "MUI2.nsh"

Name "Screenshooter"
OutFile "screenshooter-setup.exe"
InstallDir "$PROGRAMFILES\Screenshooter"
RequestExecutionLevel admin

; UI
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"

    ; Main executable
    File "${BUILD_DIR}\src\xfce4-screenshooter.exe"

    ; FFmpeg binaries
    File "${FFMPEG_DIR}\ffmpeg.exe"
    File "${FFMPEG_DIR}\ffprobe.exe"

    ; GLib/GdkPixbuf/libcurl DLLs from MSYS2
    File "${MINGW_BIN}\libglib-2.0-0.dll"
    File "${MINGW_BIN}\libgio-2.0-0.dll"
    File "${MINGW_BIN}\libgobject-2.0-0.dll"
    File "${MINGW_BIN}\libgmodule-2.0-0.dll"
    File "${MINGW_BIN}\libgdk_pixbuf-2.0-0.dll"
    File "${MINGW_BIN}\libcurl-4.dll"
    File "${MINGW_BIN}\libjson-glib-1.0-0.dll"
    File "${MINGW_BIN}\libintl-8.dll"
    File "${MINGW_BIN}\libiconv-2.dll"
    File "${MINGW_BIN}\libffi-8.dll"
    File "${MINGW_BIN}\libpcre2-8-0.dll"
    File "${MINGW_BIN}\zlib1.dll"
    File "${MINGW_BIN}\libpng16-16.dll"
    File "${MINGW_BIN}\libgcc_s_seh-1.dll"
    File "${MINGW_BIN}\libwinpthread-1.dll"
    File "${MINGW_BIN}\libstdc++-6.dll"
    File "${MINGW_BIN}\libssp-0.dll"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Start menu shortcut
    CreateDirectory "$SMPROGRAMS\Screenshooter"
    CreateShortcut "$SMPROGRAMS\Screenshooter\Screenshooter.lnk" "$INSTDIR\xfce4-screenshooter.exe"
    CreateShortcut "$SMPROGRAMS\Screenshooter\Uninstall.lnk" "$INSTDIR\uninstall.exe"

    ; Registry for Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter" \
                     "DisplayName" "Screenshooter"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter" \
                     "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter" \
                     "InstallLocation" "$INSTDIR"
SectionEnd

Section "Uninstall"
    ; Remove all files and subdirectories
    RMDir /r "$INSTDIR"

    ; Remove start menu shortcuts
    RMDir /r "$SMPROGRAMS\Screenshooter"

    ; Remove registry entry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter"
SectionEnd
```

**Note:** The `${BUILD_DIR}`, `${FFMPEG_DIR}`, and `${MINGW_BIN}` variables are passed at build time. The exact DLL list may need adjustment based on actual MSYS2 dependencies — use `ldd build/src/xfce4-screenshooter.exe` in the CI to discover the full list.

- [ ] **Step 2: Commit**

```bash
git add installer/screenshooter.nsi
git commit -m "feat: add NSIS installer script"
```

---

### Task 7: Update CI for installer build

**Files:**
- Modify: `.github/workflows/ci-windows.yml`

- [ ] **Step 1: Add NSIS and installer build to CI**

Add to the MSYS2 install list: `mingw-w64-x86_64-nsis`

Add steps after build:

```yaml
      - name: List DLL dependencies
        run: ldd build/src/xfce4-screenshooter.exe | grep mingw

      # Installer build is a future step — for now just verify the app runs
      # - name: Build installer
      #   run: |
      #     makensis -DBUILD_DIR=build -DFFMPEG_DIR=ffmpeg -DMINGW_BIN=/mingw64/bin installer/screenshooter.nsi
```

For now, keep the installer step commented out until FFmpeg binaries are bundled. The NSIS script is ready but building the full installer requires downloading FFmpeg Windows binaries.

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/ci-windows.yml
git commit -m "ci: add NSIS to Windows CI, prepare for installer build"
```

---

### Task 8: Push and verify CI

- [ ] **Step 1: Push and monitor**

```bash
git push origin master
```

Monitor both Linux and Windows CI. Fix any issues iteratively.

Common Windows CI issues:
- Missing `comctl32` link (needed for Property Sheet)
- `.rc` file compilation issues with `windres`
- Missing `PACKAGE_VERSION` define in Windows build
- `win32_resources` variable scoping between meson.build files

- [ ] **Step 2: Fix CI failures and push again**

Iterate until both workflows pass.

- [ ] **Step 3: Final commit**

```bash
git add -A
git commit -m "fix: resolve Windows CI issues for Phase 4-5"
```
