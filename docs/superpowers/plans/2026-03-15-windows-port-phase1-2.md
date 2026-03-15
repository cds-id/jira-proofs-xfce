# Windows Port Phase 1-2: Core Extraction & Platform Abstraction

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the existing monolithic `lib/` into three layers (core, platform, ui-gtk) so the Linux build keeps working and a Windows port can be built against the shared core.

**Architecture:** Extract platform-independent code (cloud config, R2, Jira, recorder, blur model) into `lib/core/`. Create `lib/platform/` with an abstract interface and Linux backend. Move GTK/XFCE UI code into `lib/ui-gtk/`. Add `sc_cloud_config_save()`, `sc_r2_test_connection()`, `sc_jira_test_connection()`, and `--setup-wizard` flag. Build a GtkAssistant first-run wizard.

**Tech Stack:** C (gnu11), Meson, GLib, GKeyFile, libcurl, json-glib, GTK3, XFCE libs

**Spec:** `docs/superpowers/specs/2026-03-15-windows-port-wizard-design.md`

**Note on spec deviation:** The spec lists `sc_platform_config_load/save/path/exists` as platform abstraction functions. This plan instead puts cloud config load/save in the core library with an explicit `config_dir` parameter, and uses only `sc_platform_config_dir()` from the platform layer to get the directory. This is simpler — the cloud config format is the same on both platforms, only the directory differs. General app preferences (xfconf on Linux, INI file on Windows) will use the spec's platform config functions when implemented in a future phase.

---

## File Map

### Files to create

| File | Responsibility |
|------|---------------|
| `lib/core/meson.build` | Build `libscreenshooter-core` static library |
| `lib/core/sc-cloud-config.c` | Cloud config load/save/validate (from `lib/screenshooter-cloud-config.c`, plus new `sc_cloud_config_save()`) |
| `lib/core/sc-cloud-config.h` | Cloud config types and API |
| `lib/core/sc-r2.c` | R2 upload + new `sc_r2_test_connection()` (from `lib/screenshooter-r2.c`) |
| `lib/core/sc-r2.h` | R2 API |
| `lib/core/sc-jira.c` | Jira API + new `sc_jira_test_connection()` (from `lib/screenshooter-jira.c`) |
| `lib/core/sc-jira.h` | Jira API |
| `lib/core/sc-recorder.c` | FFmpeg subprocess management (from `lib/screenshooter-recorder.c`) |
| `lib/core/sc-recorder.h` | Recorder API |
| `lib/core/sc-video-editor-blur.c` | Blur data model + filter chain builder (from `lib/screenshooter-video-editor-blur.c`) |
| `lib/core/sc-video-editor-blur.h` | Blur types and API |
| `lib/platform/meson.build` | Build `libscreenshooter-platform` static library |
| `lib/platform/sc-platform.h` | Platform abstraction interface |
| `lib/platform/sc-platform-linux.c` | Linux backend (config dir, stubs for capture/clipboard) |
| `lib/ui-gtk/meson.build` | Build `libscreenshooter-gtk` static library (replaces `lib/meson.build`) |
| `lib/ui-gtk/screenshooter-wizard.c` | GtkAssistant first-run wizard |
| `lib/ui-gtk/screenshooter-wizard.h` | Wizard API |
| `test/test-cloud-config-save.c` | Tests for `sc_cloud_config_save()` |
| `test/test-connection-validators.c` | Tests for `sc_r2_test_connection()` and `sc_jira_test_connection()` |

### Files to move (rename with adaptations)

| From | To | Notes |
|------|----|-------|
| `lib/screenshooter-cloud-config.c/h` | `lib/core/sc-cloud-config.c/h` | Rewritten with new API |
| `lib/screenshooter-r2.c/h` | `lib/core/sc-r2.c/h` | Rename functions, add test_connection |
| `lib/screenshooter-jira.c/h` | `lib/core/sc-jira.c/h` | Rename functions, add test_connection |
| `lib/screenshooter-recorder.c/h` | `lib/core/sc-recorder.c/h` | Rename functions |
| `lib/screenshooter-video-editor-blur.c/h` | `lib/core/sc-video-editor-blur.c/h` | Update include only |

### Files that stay in `lib/` → `lib/ui-gtk/` (GTK/XFCE-dependent)

- `screenshooter-format.c/h` — stays in ui-gtk because it uses `gdk_pixbuf_get_formats()` and `N_()` macro from libxfce4ui
- `screenshooter-dialogs.c/h`
- `screenshooter-actions.c/h`
- `screenshooter-capture.c/h`
- `screenshooter-select.c/h`
- `screenshooter-capture-x11.c/h`, `screenshooter-select-x11.c/h`
- `screenshooter-capture-wayland.c/h`, `screenshooter-select-wayland.c/h`
- `screenshooter-utils.c/h`, `screenshooter-utils-x11.c/h`
- `screenshooter-custom-actions.c/h`
- `screenshooter-jira-dialog.c/h`
- `screenshooter-recorder-dialog.c/h`
- `screenshooter-video-editor.c/h`, `screenshooter-video-editor-canvas.c/h`, `screenshooter-video-editor-timeline.c/h`
- `screenshooter-global.h`, `libscreenshooter.h`, `screenshooter-marshal.list`

---

## Chunk 1: Extract Core Library

### Task 1: Create core library headers

**Files:**
- Create: `lib/core/sc-cloud-config.h`
- Create: `lib/core/sc-r2.h`
- Create: `lib/core/sc-jira.h`
- Create: `lib/core/sc-recorder.h`
- Create: `lib/core/sc-video-editor-blur.h`

- [ ] **Step 1: Create `lib/core/` directory**

Run: `mkdir -p lib/core`

- [ ] **Step 2: Write `lib/core/sc-cloud-config.h`**

```c
#ifndef __SC_CLOUD_CONFIG_H__
#define __SC_CLOUD_CONFIG_H__

#include <glib.h>

typedef struct {
  gchar *base_url;
  gchar *email;
  gchar *api_token;
  gchar *default_project;
} JiraCloudConfig;

typedef struct {
  gchar *account_id;
  gchar *access_key_id;
  gchar *secret_access_key;
  gchar *bucket;
  gchar *public_url;
} R2CloudConfig;

typedef struct {
  gchar *bug_evidence;
  gchar *work_evidence;
} PresetsConfig;

typedef struct {
  JiraCloudConfig jira;
  R2CloudConfig r2;
  PresetsConfig presets;
  gboolean loaded;
} CloudConfig;

CloudConfig *sc_cloud_config_load           (const gchar *config_dir,
                                              GError **error);
gboolean     sc_cloud_config_save           (const CloudConfig *config,
                                              const gchar *config_dir,
                                              GError **error);
void         sc_cloud_config_free           (CloudConfig *config);
CloudConfig *sc_cloud_config_create_default (void);
gboolean     sc_cloud_config_valid_r2       (const CloudConfig *config);
gboolean     sc_cloud_config_valid_jira     (const CloudConfig *config);
gchar       *sc_cloud_config_get_path       (const gchar *config_dir);
gboolean     sc_cloud_config_exists         (const gchar *config_dir);

#endif
```

- [ ] **Step 3: Write `lib/core/sc-r2.h`**

```c
#ifndef __SC_R2_H__
#define __SC_R2_H__

#include <glib.h>
#include "sc-cloud-config.h"

typedef void (*R2ProgressCallback) (gdouble fraction, gpointer user_data);

gchar    *sc_r2_upload            (const CloudConfig *config,
                                    const gchar *file_path,
                                    R2ProgressCallback progress_cb,
                                    gpointer progress_data,
                                    GError **error);
gboolean  sc_r2_test_connection   (const CloudConfig *config,
                                    GError **error);
gchar    *sc_r2_build_object_key  (const gchar *filename);
const gchar *sc_r2_content_type   (const gchar *extension);

#endif
```

- [ ] **Step 4: Write `lib/core/sc-jira.h`**

```c
#ifndef __SC_JIRA_H__
#define __SC_JIRA_H__

#include <glib.h>
#include "sc-cloud-config.h"

typedef struct {
  gchar *key;
  gchar *summary;
} JiraIssue;

GList    *sc_jira_search          (const CloudConfig *config,
                                    const gchar *query,
                                    GError **error);
gboolean  sc_jira_post_comment    (const CloudConfig *config,
                                    const gchar *issue_key,
                                    const gchar *preset_title,
                                    const gchar *description,
                                    const gchar *image_url,
                                    GError **error);
gboolean  sc_jira_test_connection (const CloudConfig *config,
                                    GError **error);
void      sc_jira_issue_free      (JiraIssue *issue);
void      sc_jira_issue_list_free (GList *issues);

#endif
```

- [ ] **Step 5: Write `lib/core/sc-recorder.h`**

Copy from `lib/screenshooter-recorder.h`, rename all `screenshooter_recorder_*` to `sc_recorder_*`.

- [ ] **Step 6: Write `lib/core/sc-video-editor-blur.h`**

Copy `lib/screenshooter-video-editor-blur.h` as-is (functions already use `video_editor_*`/`blur_region_*` prefix).

- [ ] **Step 7: Commit**

```bash
git add lib/core/
git commit -m "feat: add core library headers with sc_ namespace"
```

---

### Task 2: Implement `lib/core/sc-cloud-config.c` with save function

**Files:**
- Create: `lib/core/sc-cloud-config.c`
- Create: `test/test-cloud-config-save.c`

- [ ] **Step 1: Write test for `sc_cloud_config_save()` round-trip**

Create `test/test-cloud-config-save.c`:

```c
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "../lib/core/sc-cloud-config.h"

static void
test_save_and_load_round_trip (void)
{
  gchar *tmpdir = g_dir_make_tmp ("sc-config-test-XXXXXX", NULL);
  g_assert_nonnull (tmpdir);

  CloudConfig *config = sc_cloud_config_create_default ();

  g_free (config->r2.account_id);
  config->r2.account_id = g_strdup ("test-account");
  g_free (config->r2.access_key_id);
  config->r2.access_key_id = g_strdup ("test-key");
  g_free (config->r2.secret_access_key);
  config->r2.secret_access_key = g_strdup ("test-secret");
  g_free (config->r2.bucket);
  config->r2.bucket = g_strdup ("test-bucket");
  g_free (config->r2.public_url);
  config->r2.public_url = g_strdup ("https://test.example.com");

  g_free (config->jira.base_url);
  config->jira.base_url = g_strdup ("https://test.atlassian.net");
  g_free (config->jira.email);
  config->jira.email = g_strdup ("test@example.com");
  g_free (config->jira.api_token);
  config->jira.api_token = g_strdup ("test-token");
  g_free (config->jira.default_project);
  config->jira.default_project = g_strdup ("TEST");

  GError *error = NULL;
  gboolean saved = sc_cloud_config_save (config, tmpdir, &error);
  g_assert_no_error (error);
  g_assert_true (saved);

  gchar *path = sc_cloud_config_get_path (tmpdir);
  g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
  g_free (path);

  CloudConfig *loaded = sc_cloud_config_load (tmpdir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (loaded);

  g_assert_cmpstr (loaded->r2.account_id, ==, "test-account");
  g_assert_cmpstr (loaded->r2.access_key_id, ==, "test-key");
  g_assert_cmpstr (loaded->r2.secret_access_key, ==, "test-secret");
  g_assert_cmpstr (loaded->r2.bucket, ==, "test-bucket");
  g_assert_cmpstr (loaded->r2.public_url, ==, "https://test.example.com");
  g_assert_cmpstr (loaded->jira.base_url, ==, "https://test.atlassian.net");
  g_assert_cmpstr (loaded->jira.email, ==, "test@example.com");
  g_assert_cmpstr (loaded->jira.api_token, ==, "test-token");
  g_assert_cmpstr (loaded->jira.default_project, ==, "TEST");

  sc_cloud_config_free (config);
  sc_cloud_config_free (loaded);

  gchar *cfgpath = sc_cloud_config_get_path (tmpdir);
  g_unlink (cfgpath);
  g_free (cfgpath);
  g_rmdir (tmpdir);
  g_free (tmpdir);
}

static void
test_save_preserves_presets (void)
{
  gchar *tmpdir = g_dir_make_tmp ("sc-config-test-XXXXXX", NULL);

  /* First save: include presets and R2 */
  CloudConfig *config = sc_cloud_config_create_default ();
  g_free (config->presets.bug_evidence);
  config->presets.bug_evidence = g_strdup ("My Bug Preset");
  g_free (config->presets.work_evidence);
  config->presets.work_evidence = g_strdup ("My Work Preset");
  g_free (config->r2.account_id);
  config->r2.account_id = g_strdup ("old-account");

  GError *error = NULL;
  sc_cloud_config_save (config, tmpdir, &error);
  g_assert_no_error (error);
  sc_cloud_config_free (config);

  /* Second save: update r2 only — presets should survive via GKeyFile merge */
  CloudConfig *update = sc_cloud_config_create_default ();
  g_free (update->r2.account_id);
  update->r2.account_id = g_strdup ("new-account");

  sc_cloud_config_save (update, tmpdir, &error);
  g_assert_no_error (error);
  sc_cloud_config_free (update);

  /* Load and verify presets preserved, R2 updated */
  CloudConfig *loaded = sc_cloud_config_load (tmpdir, &error);
  g_assert_no_error (error);
  g_assert_cmpstr (loaded->presets.bug_evidence, ==, "My Bug Preset");
  g_assert_cmpstr (loaded->presets.work_evidence, ==, "My Work Preset");
  g_assert_cmpstr (loaded->r2.account_id, ==, "new-account");
  sc_cloud_config_free (loaded);

  gchar *cfgpath = sc_cloud_config_get_path (tmpdir);
  g_unlink (cfgpath);
  g_free (cfgpath);
  g_rmdir (tmpdir);
  g_free (tmpdir);
}

static void
test_config_exists (void)
{
  gchar *tmpdir = g_dir_make_tmp ("sc-config-test-XXXXXX", NULL);
  g_assert_false (sc_cloud_config_exists (tmpdir));

  CloudConfig *config = sc_cloud_config_create_default ();
  GError *error = NULL;
  sc_cloud_config_save (config, tmpdir, &error);
  g_assert_no_error (error);
  sc_cloud_config_free (config);

  g_assert_true (sc_cloud_config_exists (tmpdir));

  gchar *cfgpath = sc_cloud_config_get_path (tmpdir);
  g_unlink (cfgpath);
  g_free (cfgpath);
  g_rmdir (tmpdir);
  g_free (tmpdir);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/cloud-config/save-load-round-trip",
                    test_save_and_load_round_trip);
  g_test_add_func ("/cloud-config/save-preserves-presets",
                    test_save_preserves_presets);
  g_test_add_func ("/cloud-config/exists",
                    test_config_exists);
  return g_test_run ();
}
```

- [ ] **Step 2: Implement `lib/core/sc-cloud-config.c`**

Port from `lib/screenshooter-cloud-config.c` with these changes:
- Rename all `screenshooter_cloud_config_*` to `sc_cloud_config_*`
- `sc_cloud_config_load(config_dir, error)` takes explicit dir instead of hardcoded path
- `sc_cloud_config_get_path(config_dir)` appends `cloud.toml` to given dir
- `sc_cloud_config_exists(config_dir)` checks for file existence
- `sc_cloud_config_create_default()` returns a `CloudConfig*` with empty strings, no file I/O
- New `sc_cloud_config_save(config, config_dir, error)`:
  1. Creates directory with `g_mkdir_with_parents(config_dir, 0700)`
  2. Loads existing file into GKeyFile if present (preserves `[presets]`)
  3. Overwrites `[r2]` and `[jira]` sections with config values
  4. Only writes preset keys if the new value is non-empty (preserves existing presets when wizard re-runs with empty defaults)
  5. Writes via `g_key_file_to_data()` + `g_file_set_contents()`
  6. Calls `sc_platform_restrict_file(path)` on success (NOT `chmod()` directly — core must stay platform-independent)
  7. Returns `gboolean` with `GError**`
- Includes: `<glib.h>`, `<gio/gio.h>`, `<string.h>`, `<sc-platform.h>` (no GTK, XFCE, or `sys/stat.h`)

- [ ] **Step 3: Compile and run test**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core lib/core/sc-cloud-config.c test/test-cloud-config-save.c -o /tmp/test-cc-save && /tmp/test-cc-save`
Expected: All 3 tests PASS

- [ ] **Step 4: Commit**

```bash
git add lib/core/sc-cloud-config.c test/test-cloud-config-save.c
git commit -m "feat: implement sc_cloud_config with save and preset preservation"
```

---

### Task 3: Move remaining core source files

**Files:**
- Create: `lib/core/sc-r2.c` (from `lib/screenshooter-r2.c`)
- Create: `lib/core/sc-jira.c` (from `lib/screenshooter-jira.c`)
- Create: `lib/core/sc-recorder.c` (from `lib/screenshooter-recorder.c`)
- Create: `lib/core/sc-video-editor-blur.c` (from `lib/screenshooter-video-editor-blur.c`)

- [ ] **Step 1: Copy and adapt R2 source**

Copy `lib/screenshooter-r2.c` to `lib/core/sc-r2.c`:
- Change `#include "screenshooter-r2.h"` to `#include "sc-r2.h"`
- Change `#include "screenshooter-cloud-config.h"` to `#include "sc-cloud-config.h"`
- Rename all `screenshooter_r2_*` functions to `sc_r2_*`
- Add `sc_r2_test_connection()` — see Task 7 for full implementation (added after core lib builds)

- [ ] **Step 2: Copy and adapt Jira source**

Copy `lib/screenshooter-jira.c` to `lib/core/sc-jira.c`:
- Change includes to `sc-jira.h`, `sc-cloud-config.h`
- Rename all `screenshooter_jira_*` to `sc_jira_*`
- Add `sc_jira_test_connection()` — see Task 7

- [ ] **Step 3: Copy and adapt recorder source**

Copy `lib/screenshooter-recorder.c` to `lib/core/sc-recorder.c`:
- Change includes
- Rename `screenshooter_recorder_*` to `sc_recorder_*`
- Note: This file uses `sys/wait.h`, `signal.h`, and hardcoded `x11grab`. Mark with `/* TODO: move platform-specific args to platform layer */`. Keep as-is for now — Linux build still works.

- [ ] **Step 4: Copy blur model source**

Copy `lib/screenshooter-video-editor-blur.c` to `lib/core/sc-video-editor-blur.c` — update include to `"sc-video-editor-blur.h"` only.

- [ ] **Step 5: Commit**

```bash
git add lib/core/sc-r2.c lib/core/sc-jira.c lib/core/sc-recorder.c \
        lib/core/sc-video-editor-blur.c
git commit -m "feat: move platform-independent sources to lib/core/"
```

---

### Task 4: Create `lib/core/meson.build`

**Files:**
- Create: `lib/core/meson.build`

- [ ] **Step 1: Write core meson build file**

```meson
libscreenshooter_core_sources = [
  'sc-cloud-config.c',
  'sc-cloud-config.h',
  'sc-r2.c',
  'sc-r2.h',
  'sc-jira.c',
  'sc-jira.h',
  'sc-recorder.c',
  'sc-recorder.h',
  'sc-video-editor-blur.c',
  'sc-video-editor-blur.h',
]

libscreenshooter_core = static_library(
  'screenshooter-core',
  libscreenshooter_core_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: [
    glib,
    dependency('gio-2.0'),
    libcurl,
    json_glib,
    libm,
    libscreenshooter_platform_dep,
  ],
  install: false,
)

libscreenshooter_core_dep = declare_dependency(
  link_with: libscreenshooter_core,
  include_directories: include_directories('.'),
)
```

- [ ] **Step 2: Commit**

```bash
git add lib/core/meson.build
git commit -m "build: add meson build for libscreenshooter-core"
```

---

### Task 5: Create platform abstraction layer

**Files:**
- Create: `lib/platform/sc-platform.h`
- Create: `lib/platform/sc-platform-linux.c`
- Create: `lib/platform/meson.build`

- [ ] **Step 1: Create directory and write platform interface header**

Run: `mkdir -p lib/platform`

Create `lib/platform/sc-platform.h`:

```c
#ifndef __SC_PLATFORM_H__
#define __SC_PLATFORM_H__

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef enum {
    SC_CAPTURE_FULLSCREEN,
    SC_CAPTURE_WINDOW,
    SC_CAPTURE_REGION
} ScCaptureMode;

typedef struct {
    gint x, y, width, height;
} ScRegion;

/* Capture a screenshot */
GdkPixbuf *sc_platform_capture         (ScCaptureMode mode, ScRegion *region);

/* Interactive region selection */
gboolean   sc_platform_select_region   (ScRegion *out_region);

/* FFmpeg recorder input arguments (e.g. "-f x11grab" or "-f gdigrab") */
gchar    **sc_platform_recorder_args   (ScCaptureMode mode, ScRegion *region);
void       sc_platform_recorder_args_free (gchar **args);

/* Config directory path
 * Linux: ~/.config/xfce4-screenshooter/
 * Windows: %APPDATA%\xfce4-screenshooter\
 * Caller must g_free() the result.
 */
gchar     *sc_platform_config_dir      (void);

/* Clipboard */
gboolean   sc_platform_clipboard_copy_image (GdkPixbuf *pixbuf);

/* Notifications */
void       sc_platform_notify          (const gchar *title, const gchar *body);

/* File permissions (platform-specific)
 * Linux: chmod(path, 0600)
 * Windows: SetFileSecurity() with owner-only ACL
 */
void       sc_platform_restrict_file   (const gchar *path);

#endif
```

- [ ] **Step 2: Write Linux backend**

Create `lib/platform/sc-platform-linux.c`:

```c
#include "sc-platform.h"

#include <glib.h>
#include <sys/stat.h>


gchar *
sc_platform_config_dir (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "xfce4-screenshooter", NULL);
}


void
sc_platform_restrict_file (const gchar *path)
{
  chmod (path, 0600);
}


/*
 * Capture, region selection, clipboard, and notifications are
 * implemented by the GTK UI layer for now. These stubs satisfy
 * the interface. They will be wired through here as the
 * refactoring progresses.
 */

GdkPixbuf *
sc_platform_capture (ScCaptureMode mode, ScRegion *region)
{
  g_warning ("sc_platform_capture: use GTK UI layer directly");
  return NULL;
}

gboolean
sc_platform_select_region (ScRegion *out_region)
{
  g_warning ("sc_platform_select_region: use GTK UI layer directly");
  return FALSE;
}

gchar **
sc_platform_recorder_args (ScCaptureMode mode, ScRegion *region)
{
  g_warning ("sc_platform_recorder_args: not yet wired");
  return NULL;
}

void
sc_platform_recorder_args_free (gchar **args)
{
  g_strfreev (args);
}

gboolean
sc_platform_clipboard_copy_image (GdkPixbuf *pixbuf)
{
  g_warning ("sc_platform_clipboard_copy_image: use GTK UI layer directly");
  return FALSE;
}

void
sc_platform_notify (const gchar *title, const gchar *body)
{
  g_warning ("sc_platform_notify: not yet wired");
}
```

- [ ] **Step 3: Write platform meson build**

Create `lib/platform/meson.build`:

```meson
libscreenshooter_platform_sources = [
  'sc-platform.h',
  'sc-platform-linux.c',
]

libscreenshooter_platform = static_library(
  'screenshooter-platform',
  libscreenshooter_platform_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: [
    glib,
    dependency('gdk-pixbuf-2.0'),
  ],
  install: false,
)

libscreenshooter_platform_dep = declare_dependency(
  link_with: libscreenshooter_platform,
  include_directories: include_directories('.'),
)
```

- [ ] **Step 4: Commit**

```bash
git add lib/platform/
git commit -m "feat: add platform abstraction layer with Linux backend"
```

---

### Task 6: Restructure `lib/` → `lib/ui-gtk/` and update builds

This is the critical task. Order matters: update includes BEFORE deleting old files.

**Files:**
- Move: all GTK/XFCE source files from `lib/` to `lib/ui-gtk/`
- Create: `lib/ui-gtk/meson.build` (replaces `lib/meson.build`)
- Modify: `meson.build` (top-level)
- Modify: `lib/ui-gtk/libscreenshooter.h`
- Delete: old copies of moved-to-core files from `lib/`

- [ ] **Step 1: Create `lib/ui-gtk/` and move GTK/XFCE files**

```bash
mkdir -p lib/ui-gtk

# Move GTK/XFCE-dependent source files
for f in screenshooter-dialogs screenshooter-actions screenshooter-capture \
         screenshooter-select screenshooter-capture-x11 screenshooter-select-x11 \
         screenshooter-capture-wayland screenshooter-select-wayland \
         screenshooter-utils screenshooter-utils-x11 \
         screenshooter-custom-actions screenshooter-jira-dialog \
         screenshooter-recorder-dialog screenshooter-video-editor \
         screenshooter-video-editor-canvas screenshooter-video-editor-timeline \
         screenshooter-format; do
  [ -f "lib/${f}.c" ] && git mv "lib/${f}.c" "lib/ui-gtk/${f}.c"
  [ -f "lib/${f}.h" ] && git mv "lib/${f}.h" "lib/ui-gtk/${f}.h"
done

# Move other GTK-dependent files
git mv lib/screenshooter-global.h lib/ui-gtk/screenshooter-global.h
git mv lib/libscreenshooter.h lib/ui-gtk/libscreenshooter.h
git mv lib/screenshooter-marshal.list lib/ui-gtk/screenshooter-marshal.list
```

- [ ] **Step 2: Update `#include` paths in all ui-gtk sources**

In every `.c` file under `lib/ui-gtk/`, change includes for moved-to-core headers:
- `#include "screenshooter-cloud-config.h"` → `#include <sc-cloud-config.h>`
- `#include "screenshooter-r2.h"` → `#include <sc-r2.h>`
- `#include "screenshooter-jira.h"` → `#include <sc-jira.h>`
- `#include "screenshooter-recorder.h"` → `#include <sc-recorder.h>`
- `#include "screenshooter-video-editor-blur.h"` → `#include <sc-video-editor-blur.h>`

Also update function call sites to use new `sc_*` names. Key files:
- `screenshooter-actions.c` — calls R2 upload, Jira post, recorder, cloud config functions
- `screenshooter-video-editor.c` — calls video_editor_blur functions (these kept same names, no change needed)
- `screenshooter-jira-dialog.c` — calls jira search, jira issue functions

Run: `grep -rn 'screenshooter_cloud_config\|screenshooter_r2_\|screenshooter_jira_\|screenshooter_recorder_' lib/ui-gtk/` to find all call sites that need renaming.

- [ ] **Step 3: Update `lib/ui-gtk/libscreenshooter.h`**

```c
#ifndef HAVE_SCREENSHOOTER_H
#define HAVE_SCREENSHOOTER_H

/* GTK UI headers */
#include "screenshooter-dialogs.h"
#include "screenshooter-utils.h"
#include "screenshooter-actions.h"
#include "screenshooter-capture.h"
#include "screenshooter-format.h"
#include "screenshooter-global.h"
#include "screenshooter-jira-dialog.h"
#include "screenshooter-recorder-dialog.h"
#include "screenshooter-video-editor.h"

/* Core library */
#include <sc-cloud-config.h>
#include <sc-r2.h>
#include <sc-jira.h>
#include <sc-recorder.h>
#include <sc-video-editor-blur.h>

/* Platform */
#include <sc-platform.h>

#endif
```

- [ ] **Step 4: Delete old copies from `lib/` (already copied to `lib/core/`)**

```bash
git rm lib/screenshooter-cloud-config.c lib/screenshooter-cloud-config.h
git rm lib/screenshooter-r2.c lib/screenshooter-r2.h
git rm lib/screenshooter-jira.c lib/screenshooter-jira.h
git rm lib/screenshooter-recorder.c lib/screenshooter-recorder.h
git rm lib/screenshooter-video-editor-blur.c lib/screenshooter-video-editor-blur.h
```

- [ ] **Step 5: Create `lib/ui-gtk/meson.build`**

This replaces the old `lib/meson.build`. Key differences from original:
- Core sources removed (now in `lib/core/`)
- Added dependency on `libscreenshooter_core_dep` and `libscreenshooter_platform_dep`
- Preserved `gnome.genmarshal()` call
- Preserved conditional X11/Wayland source additions
- Updated include paths

```meson
libscreenshooter_gtk_sources = [
  'libscreenshooter.h',
  'screenshooter-actions.c',
  'screenshooter-actions.h',
  'screenshooter-capture.c',
  'screenshooter-capture.h',
  'screenshooter-custom-actions.c',
  'screenshooter-custom-actions.h',
  'screenshooter-format.c',
  'screenshooter-format.h',
  'screenshooter-dialogs.c',
  'screenshooter-dialogs.h',
  'screenshooter-global.h',
  'screenshooter-select.c',
  'screenshooter-select.h',
  'screenshooter-utils.c',
  'screenshooter-utils.h',
  'screenshooter-jira-dialog.c',
  'screenshooter-jira-dialog.h',
  'screenshooter-recorder-dialog.c',
  'screenshooter-recorder-dialog.h',
  'screenshooter-video-editor.c',
  'screenshooter-video-editor.h',
  'screenshooter-video-editor-canvas.c',
  'screenshooter-video-editor-canvas.h',
  'screenshooter-video-editor-timeline.c',
  'screenshooter-video-editor-timeline.h',
]

libscreenshooter_gtk_sources += gnome.genmarshal(
  'screenshooter-marshal',
  sources: 'screenshooter-marshal.list',
  prefix: '_screenshooter_marshal',
  internal: true,
  install_header: false,
)

if enable_x11
  libscreenshooter_gtk_sources += [
    'screenshooter-capture-x11.c',
    'screenshooter-capture-x11.h',
    'screenshooter-select-x11.c',
    'screenshooter-select-x11.h',
    'screenshooter-utils-x11.c',
    'screenshooter-utils-x11.h',
  ]
endif

if enable_wayland
  libscreenshooter_gtk_sources += wayland_protocols_generated_sources
  libscreenshooter_gtk_sources += [
    'screenshooter-capture-wayland.c',
    'screenshooter-capture-wayland.h',
    'screenshooter-select-wayland.c',
    'screenshooter-select-wayland.h',
  ]
endif

libscreenshooter = static_library(
  'libscreenshooter',
  libscreenshooter_gtk_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: [
    glib,
    gtk,
    gtk_layer_shell,
    exo,
    libxfce4ui,
    libxfce4util,
    xfconf,
    x11_deps,
    wayland_deps,
    xfixes,
    libcurl,
    json_glib,
    libm,
    libscreenshooter_core_dep,
    libscreenshooter_platform_dep,
  ],
  install: false,
)
```

- [ ] **Step 6: Remove old `lib/meson.build`, update top-level**

```bash
git rm lib/meson.build
```

Edit top-level `meson.build`: change `subdir('lib')` to:
```meson
subdir('lib/platform')  # platform first (core depends on it for sc_platform_restrict_file)
subdir('lib/core')
subdir('lib/ui-gtk')
```

Also update include paths in both `src/meson.build` AND `panel-plugin/meson.build`:
- Change `include_directories('..', '..' / 'lib')` to `include_directories('..', '..' / 'lib' / 'ui-gtk')`
- The `link_with: [libscreenshooter]` in both files must also include core and platform:
  `link_with: [libscreenshooter, libscreenshooter_core, libscreenshooter_platform]`
  (Meson does not propagate transitive link dependencies for static libraries via `link_with`)

- [ ] **Step 7: Build and verify**

Run: `meson setup builddir --wipe && ninja -C builddir`
Expected: Successful build with no errors. If there are missing include errors, fix them before proceeding.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor: restructure lib/ into core/, platform/, ui-gtk/"
```

---

## Chunk 2: Connection Testers, Wizard & CLI Integration

### Task 7: Implement test connection functions

**Files:**
- Modify: `lib/core/sc-r2.c`
- Modify: `lib/core/sc-jira.c`
- Create: `test/test-connection-validators.c`

- [ ] **Step 1: Write test for connection validators**

Create `test/test-connection-validators.c`:

```c
#include <glib.h>
#include "../lib/core/sc-cloud-config.h"
#include "../lib/core/sc-r2.h"
#include "../lib/core/sc-jira.h"

static void
test_r2_test_connection_missing_fields (void)
{
  CloudConfig *config = sc_cloud_config_create_default ();
  GError *error = NULL;

  gboolean result = sc_r2_test_connection (config, &error);
  g_assert_false (result);
  g_assert_nonnull (error);
  g_assert_true (g_str_has_prefix (error->message, "R2 configuration"));
  g_error_free (error);
  sc_cloud_config_free (config);
}

static void
test_jira_test_connection_missing_fields (void)
{
  CloudConfig *config = sc_cloud_config_create_default ();
  GError *error = NULL;

  gboolean result = sc_jira_test_connection (config, &error);
  g_assert_false (result);
  g_assert_nonnull (error);
  g_assert_true (g_str_has_prefix (error->message, "Jira configuration"));
  g_error_free (error);
  sc_cloud_config_free (config);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/r2/test-connection-missing-fields",
                    test_r2_test_connection_missing_fields);
  g_test_add_func ("/jira/test-connection-missing-fields",
                    test_jira_test_connection_missing_fields);
  return g_test_run ();
}
```

- [ ] **Step 2: Implement `sc_r2_test_connection()` in `lib/core/sc-r2.c`**

This function must use the same AWS Signature V4 signing that `sc_r2_upload()` uses. Reuse the existing internal signing functions (`hmac_sha256_hex`, signing key derivation, etc.) to sign a HEAD request to the bucket endpoint.

```c
gboolean
sc_r2_test_connection (const CloudConfig *config, GError **error)
{
  g_return_val_if_fail (config != NULL, FALSE);

  if (!sc_cloud_config_valid_r2 (config))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "R2 configuration is incomplete");
      return FALSE;
    }

  /* Build signed HEAD request to bucket root using existing signing helpers.
   * The signing functions (hmac_sha256_hex, derive_signing_key, etc.) are
   * file-local statics in sc-r2.c, so they can be called directly here.
   *
   * URL: https://{account_id}.r2.cloudflarestorage.com/{bucket}
   * Method: HEAD
   * Signed with AWS Sig V4 using config credentials.
   */

  CURL *curl = curl_easy_init ();
  if (!curl)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to initialize HTTP client");
      return FALSE;
    }

  gchar *url = g_strdup_printf ("https://%s.r2.cloudflarestorage.com/%s",
                                 config->r2.account_id,
                                 config->r2.bucket);

  /* Build date strings */
  GDateTime *now = g_date_time_new_now_utc ();
  gchar *date_iso = g_date_time_format (now, "%Y%m%dT%H%M%SZ");
  gchar *date_short = g_date_time_format (now, "%Y%m%d");
  g_date_time_unref (now);

  /* Build canonical request and sign with AWS Sig V4
   * (reuse existing static helpers in this file) */
  /* ... signing code using hmac_sha256_hex, etc. ... */

  struct curl_slist *headers = NULL;
  headers = curl_slist_append (headers, g_strdup_printf ("x-amz-date: %s", date_iso));
  /* Add Authorization header with Sig V4 signature */

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform (curl);
  long http_code = 0;
  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

  gboolean success = FALSE;
  if (res != CURLE_OK)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "R2 connection failed: %s", curl_easy_strerror (res));
    }
  else if (http_code == 200 || http_code == 404)
    {
      /* 200 = bucket exists, 404 = bucket not found but creds valid */
      success = TRUE;
    }
  else if (http_code == 403)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "R2 authentication failed (invalid credentials)");
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "R2 unexpected response (HTTP %ld)", http_code);
    }

  g_free (date_iso);
  g_free (date_short);
  g_free (url);
  curl_slist_free_all (headers);
  curl_easy_cleanup (curl);
  return success;
}
```

**Important:** The implementer must wire in the actual AWS Sig V4 signing using the existing `hmac_sha256_hex()`, `derive_signing_key()` and signing logic already in `sc-r2.c`. The pseudocode above shows the structure — the actual signing header construction should mirror what `sc_r2_upload()` does.

- [ ] **Step 3: Implement `sc_jira_test_connection()` in `lib/core/sc-jira.c`**

```c
gboolean
sc_jira_test_connection (const CloudConfig *config, GError **error)
{
  g_return_val_if_fail (config != NULL, FALSE);

  if (!sc_cloud_config_valid_jira (config))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Jira configuration is incomplete");
      return FALSE;
    }

  CURL *curl = curl_easy_init ();
  if (!curl)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to initialize HTTP client");
      return FALSE;
    }

  gchar *url = g_strdup_printf ("%s/rest/api/3/myself",
                                 config->jira.base_url);
  gchar *userpwd = g_strdup_printf ("%s:%s",
                                     config->jira.email,
                                     config->jira.api_token);

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_USERPWD, userpwd);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform (curl);
  long http_code = 0;
  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

  gboolean success = (res == CURLE_OK && http_code == 200);

  if (res != CURLE_OK)
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Jira connection failed: %s", curl_easy_strerror (res));
  else if (http_code != 200)
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Jira authentication failed (HTTP %ld)", http_code);

  g_free (url);
  g_free (userpwd);
  curl_easy_cleanup (curl);
  return success;
}
```

- [ ] **Step 4: Run tests**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0 libcurl json-glib-1.0) -lm -Ilib/core lib/core/sc-cloud-config.c lib/core/sc-r2.c lib/core/sc-jira.c test/test-connection-validators.c -o /tmp/test-conn && /tmp/test-conn`
Expected: Both tests PASS (missing fields returns FALSE with error)

- [ ] **Step 5: Commit**

```bash
git add lib/core/sc-r2.c lib/core/sc-jira.c test/test-connection-validators.c
git commit -m "feat: add sc_r2_test_connection() and sc_jira_test_connection()"
```

---

### Task 8: Implement GtkAssistant first-run wizard

**Files:**
- Create: `lib/ui-gtk/screenshooter-wizard.h`
- Create: `lib/ui-gtk/screenshooter-wizard.c`
- Modify: `lib/ui-gtk/meson.build`

- [ ] **Step 1: Write wizard header**

Create `lib/ui-gtk/screenshooter-wizard.h`:

```c
#ifndef __SCREENSHOOTER_WIZARD_H__
#define __SCREENSHOOTER_WIZARD_H__

#include <gtk/gtk.h>

/* Returns TRUE if wizard completed, FALSE if skipped/cancelled */
gboolean screenshooter_wizard_run (GtkWindow *parent, const gchar *config_dir);

#endif
```

- [ ] **Step 2: Implement wizard with GtkAssistant**

Create `lib/ui-gtk/screenshooter-wizard.c`. Implementation:

- 4 pages via `GtkAssistant`:
  1. **Welcome** — GtkLabel with description. Skip button uses `GTK_RESPONSE_CANCEL`.
  2. **R2 Setup** — GtkGrid with 5 GtkEntry fields + "Test Connection" GtkButton + result GtkLabel.
  3. **Jira Setup** — GtkGrid with 4 GtkEntry fields + "Test Connection" GtkButton + result GtkLabel.
  4. **Done** — GtkLabel summary. `GTK_ASSISTANT_PAGE_CONFIRM`.

- On "Test Connection" click: read entries into temp CloudConfig, call `sc_r2_test_connection()` or `sc_jira_test_connection()`, update result label (green/red via CSS class).
- On `apply` signal (finish): build CloudConfig from entries, call `sc_cloud_config_save(config, config_dir, &error)`, show error dialog if save fails.
- On `cancel` signal (skip): save empty default config so wizard doesn't re-trigger, destroy assistant.
- All pages set complete by default (`gtk_assistant_set_page_complete(... TRUE)`) since all fields are optional.

Includes: `<gtk/gtk.h>`, `<sc-cloud-config.h>`, `<sc-r2.h>`, `<sc-jira.h>`, `<sc-platform.h>`

- [ ] **Step 3: Add wizard to ui-gtk meson.build**

Add `'screenshooter-wizard.c'` and `'screenshooter-wizard.h'` to the `libscreenshooter_gtk_sources` list.

- [ ] **Step 4: Build and verify**

Run: `ninja -C builddir`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add lib/ui-gtk/screenshooter-wizard.c lib/ui-gtk/screenshooter-wizard.h lib/ui-gtk/meson.build
git commit -m "feat: add GtkAssistant first-run wizard for cloud config"
```

---

### Task 9: Wire wizard into main.c and add menu item

**Files:**
- Modify: `src/main.c`
- Modify: `lib/ui-gtk/libscreenshooter.h`
- Modify: `lib/ui-gtk/screenshooter-dialogs.c`

- [ ] **Step 1: Add `--setup-wizard` CLI flag**

In `src/main.c`, add to globals and `entries[]`:

```c
gboolean setup_wizard = FALSE;

/* In entries[] before the NULL terminator: */
{
  "setup-wizard", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE,
  &setup_wizard,
  N_("Launch the cloud services setup wizard"),
  NULL
},
```

- [ ] **Step 2: Add wizard trigger logic**

After `sd` is initialized and `rc_file` is read (after line 310), add:

```c
/* First-run wizard or manual re-run */
{
  gchar *config_dir = sc_platform_config_dir ();
  gboolean need_wizard = setup_wizard;

  if (!need_wizard && !sc_cloud_config_exists (config_dir))
    need_wizard = TRUE;

  /* If config exists but is corrupt, delete and re-trigger */
  if (!need_wizard && sc_cloud_config_exists (config_dir))
    {
      GError *load_error = NULL;
      CloudConfig *test = sc_cloud_config_load (config_dir, &load_error);
      if (test == NULL && load_error != NULL &&
          load_error->code != G_IO_ERROR_NOT_FOUND)
        {
          g_warning ("Corrupt cloud config, resetting: %s", load_error->message);
          gchar *path = sc_cloud_config_get_path (config_dir);
          g_unlink (path);
          g_free (path);
          need_wizard = TRUE;
        }
      g_clear_error (&load_error);
      sc_cloud_config_free (test);
    }

  if (need_wizard && !fullscreen && !window && !region && !any_record && !edit_video)
    {
      screenshooter_wizard_run (NULL, config_dir);
    }
  g_free (config_dir);
}
```

- [ ] **Step 3: Add wizard include to `libscreenshooter.h`**

Add `#include "screenshooter-wizard.h"` to the GTK UI headers section.

- [ ] **Step 4: Add "Reconfigure Cloud Services" to main dialog**

In `lib/ui-gtk/screenshooter-dialogs.c`, find the preferences/settings area in `screenshooter_region_dialog_new()` and add a button:

```c
/* Near existing preference controls */
GtkWidget *wizard_btn = gtk_button_new_with_label (_("Reconfigure Cloud Services..."));
g_signal_connect_swapped (wizard_btn, "clicked",
                          G_CALLBACK (screenshooter_wizard_run_from_dialog),
                          dialog);
/* Pack into appropriate container */
```

Add a helper that gets config_dir and calls `screenshooter_wizard_run()`.

- [ ] **Step 5: Build and verify**

Run: `ninja -C builddir`
Expected: Build succeeds.

- [ ] **Step 6: Manual test**

Run: `builddir/src/xfce4-screenshooter --setup-wizard`
Expected: Wizard dialog appears.

- [ ] **Step 7: Commit**

```bash
git add src/main.c lib/ui-gtk/libscreenshooter.h lib/ui-gtk/screenshooter-dialogs.c
git commit -m "feat: add --setup-wizard flag, auto-trigger, and menu item"
```

---

### Task 10: Update existing test files for new paths and function names

**Files:**
- Modify: `test/test-cloud.c`
- Modify: `test/test-config-import.c`
- Modify: `test/test-render-blur.c`
- Modify: `test/test-video-editor-blur.c`
- Modify: `test/test-video-editor-integration.c`

- [ ] **Step 1: Update `test/test-cloud.c`**

This file has extensive changes needed:

Include changes:
- `#include "../lib/screenshooter-cloud-config.h"` → `#include "../lib/core/sc-cloud-config.h"`
- `#include "../lib/screenshooter-r2.h"` → `#include "../lib/core/sc-r2.h"`
- `#include "../lib/screenshooter-jira.h"` → `#include "../lib/core/sc-jira.h"`
- Add `#include "../lib/platform/sc-platform.h"`

Function renames:
- `screenshooter_cloud_config_load(&error)` → `sc_cloud_config_load(config_dir, &error)` where `config_dir = sc_platform_config_dir()` (add a variable at the top of `main()`)
- `screenshooter_cloud_config_valid_r2()` → `sc_cloud_config_valid_r2()`
- `screenshooter_cloud_config_valid_jira()` → `sc_cloud_config_valid_jira()`
- `screenshooter_jira_search()` → `sc_jira_search()`
- `screenshooter_jira_issue_list_free()` → `sc_jira_issue_list_free()`
- `screenshooter_r2_upload()` → `sc_r2_upload()`
- `screenshooter_cloud_config_free()` → `sc_cloud_config_free()`

Compile command for this test:
```bash
cc $(pkg-config --cflags --libs glib-2.0 gio-2.0 libcurl json-glib-1.0 gdk-pixbuf-2.0) -lm \
  -Ilib/core -Ilib/platform \
  lib/core/sc-cloud-config.c lib/core/sc-r2.c lib/core/sc-jira.c \
  lib/platform/sc-platform-linux.c \
  test/test-cloud.c -o /tmp/test-cloud
```

- [ ] **Step 2: Update `test/test-config-import.c`**

This file uses bare include (no `../lib/` prefix):
- `#include "screenshooter-video-editor-blur.h"` → `#include "../lib/core/sc-video-editor-blur.h"`
- Function names `video_editor_*` and `blur_region_*` are unchanged (no rename needed).

- [ ] **Step 3: Update `test/test-render-blur.c`**

Same as test-config-import:
- `#include "screenshooter-video-editor-blur.h"` → `#include "../lib/core/sc-video-editor-blur.h"`
- No function renames needed.

- [ ] **Step 4: Update `test/test-video-editor-blur.c`**

Check include path and update if needed:
- If it uses `#include "../lib/screenshooter-video-editor-blur.h"` → `#include "../lib/core/sc-video-editor-blur.h"`

- [ ] **Step 5: Update `test/test-video-editor-integration.c`**

Same pattern as above.

- [ ] **Step 6: Verify tests compile**

Run each test manually to verify compilation works with the new include paths. These are standalone test files (no `test/meson.build`), so verify with direct `cc` commands:

```bash
# Example for test-config-import:
cc $(pkg-config --cflags glib-2.0 json-glib-1.0) -Ilib/core \
  lib/core/sc-video-editor-blur.c test/test-config-import.c \
  $(pkg-config --libs glib-2.0 json-glib-1.0) -lm -o /tmp/test-ci && /tmp/test-ci --help
```

- [ ] **Step 7: Commit**

```bash
git add test/
git commit -m "test: update test includes and function names for core refactor"
```

---

### Task 11: Final build verification

- [ ] **Step 1: Clean build from scratch**

Run: `rm -rf builddir && meson setup builddir && ninja -C builddir`
Expected: Full successful build with no errors or warnings about missing symbols.

- [ ] **Step 2: Run the app normally**

Run: `builddir/src/xfce4-screenshooter`
Expected: App launches normally.

- [ ] **Step 3: Test wizard flow**

Run: `builddir/src/xfce4-screenshooter --setup-wizard`
Expected: Wizard appears, can navigate all pages, can skip, config file created.

- [ ] **Step 4: Test existing features**

Test screenshot capture (fullscreen `-f`, window `-w`, region `-r`), video editor (`--edit-video`), to ensure restructuring didn't break anything.

- [ ] **Step 5: Final fixup commit if needed**

```bash
git add -A
git commit -m "fix: final adjustments for Phase 1-2 restructure"
```
