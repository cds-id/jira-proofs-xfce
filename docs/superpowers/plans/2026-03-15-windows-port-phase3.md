# Windows Port Phase 3: Windows Platform Backend & CI

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the Windows platform backend (`sc-platform-windows.c`) and set up GitHub Actions CI with MSYS2 to compile and test it.

**Architecture:** Add a Windows-specific implementation of the `sc-platform.h` interface alongside the existing Linux backend. Use Meson's `host_machine.system()` to select the right backend at build time. Set up a GitHub Actions workflow with MSYS2/MinGW64 to validate the Windows build. Since we're developing on Linux, the Windows code is written and committed locally, then validated by CI.

**Tech Stack:** C (gnu11), Meson, Win32 API (GDI, User32, Shell32, Advapi32), GLib, GdkPixbuf, MSYS2/MinGW64, GitHub Actions

**Spec:** `docs/superpowers/specs/2026-03-15-windows-port-wizard-design.md` (Sections 2, 4, 7)

**Spec deviation note:** The spec defines `sc_platform_config_load/save/path/exists` in the platform interface. The actual implementation (from Phase 1-2) uses `sc_platform_config_dir()` and `sc_platform_restrict_file()` instead, with cloud config handled by the core layer. This deviation is documented in the Phase 1-2 plan header.

---

## File Map

### Files to create

| File | Responsibility |
|------|---------------|
| `lib/platform/sc-platform-windows.c` | Windows platform backend — config dir, file ACLs, screenshot capture, clipboard, notifications, recorder args |
| `.github/workflows/ci-windows.yml` | GitHub Actions workflow for Windows build with MSYS2 |
| `src/main-win32.c` | Minimal Windows entry point (console app, no GTK) for build validation |

### Files to modify

| File | Change |
|------|--------|
| `lib/platform/meson.build` | Conditionally compile Linux or Windows backend |
| `meson.build` | Make Linux deps conditional. Wrap `gnome`/`i18n` imports. Guard X11/Wayland error check. |
| `src/meson.build` | Wrap Linux build in conditional, add Windows build. Guard `i18n` and `xfce_revision_h` usage. |

---

## Chunk 1: Windows Platform Backend + Build System

### Task 1: Create `sc-platform-windows.c`

**Files:**
- Create: `lib/platform/sc-platform-windows.c`

- [ ] **Step 1: Read the existing Linux backend for reference**

Read `lib/platform/sc-platform-linux.c` and `lib/platform/sc-platform.h`.

- [ ] **Step 2: Write the Windows backend**

Create `lib/platform/sc-platform-windows.c`. All functions are wrapped in `#ifdef G_OS_WIN32` with `g_assert_not_reached()` fallbacks.

Key implementation details per function:

**`sc_platform_config_dir()`** — Uses `g_get_user_config_dir()` which maps to `%APPDATA%` on Windows.

**`sc_platform_restrict_file()`** — Sets owner-only ACL via SDDL string `D:P(A;;FA;;;OW)` using `ConvertStringSecurityDescriptorToSecurityDescriptorW` + `SetNamedSecurityInfoW`.

**`sc_platform_capture()`** — Uses `BitBlt` for fullscreen/region. Must:
- Save the old bitmap from `SelectObject()` and restore it before cleanup (prevents GDI object leak)
- Extract pixels via `GetDIBits` with 32-bit BGRA format
- Convert BGRA → RGBA for GdkPixbuf
- Use `GDK_COLORSPACE_RGB` with `has_alpha=TRUE`

**`sc_platform_select_region()`** — Stub returning FALSE with warning. Full implementation deferred to Phase 4 (Win32 UI).

**`sc_platform_recorder_args()`** — Returns `-f gdigrab -i desktop` args for FFmpeg, with `-offset_x`, `-offset_y`, `-video_size` for region capture.

**`sc_platform_recorder_args_free()`** — Same as Linux: `g_strfreev(args)` with NULL check. Must be included — the linker needs it.

**`sc_platform_clipboard_copy_image()`** — Uses `CF_DIB` format with `GlobalAlloc`:
1. Allocate `BITMAPINFOHEADER + pixel data` in a single `GlobalAlloc(GMEM_MOVEABLE)` block
2. Convert RGBA → BGRA in the DIB pixel data
3. Call `OpenClipboard(NULL)`, `EmptyClipboard()`, `SetClipboardData(CF_DIB, hGlobal)`, `CloseClipboard()`
4. Do NOT free `hGlobal` — clipboard owns it after `SetClipboardData`

**Important:** Do NOT use `CF_BITMAP` with a DIBSection — it produces corrupt images. `CF_DIB` is the correct format for device-independent clipboard images.

**`sc_platform_notify()`** — Creates a hidden message-only window (`CreateWindowEx` with `HWND_MESSAGE` parent) for `Shell_NotifyIconW`. Must:
- Register a window class and create the hidden window
- Set `nid.hWnd` to the hidden window (NOT NULL — Shell_NotifyIcon requires a valid HWND)
- Null-terminate strings after `wcsncpy` (add `nid.szInfoTitle[63] = L'\0'` etc.)
- Clean up: `NIM_DELETE`, `DestroyWindow`

- [ ] **Step 3: Commit**

```bash
git add lib/platform/sc-platform-windows.c
git commit -m "feat: add Windows platform backend (sc-platform-windows.c)"
```

---

### Task 2: Update platform meson.build for cross-platform

**Files:**
- Modify: `lib/platform/meson.build`

- [ ] **Step 1: Read current meson.build**

Read `lib/platform/meson.build`.

- [ ] **Step 2: Update to conditionally select backend**

```meson
libscreenshooter_platform_sources = [
  'sc-platform.h',
]

platform_deps = [
  glib,
  dependency('gdk-pixbuf-2.0'),
]

if host_machine.system() == 'windows'
  libscreenshooter_platform_sources += 'sc-platform-windows.c'
  # Require Windows 10 for DPI awareness V2 and modern Shell APIs
  add_project_arguments('-D_WIN32_WINNT=0x0A00', language: 'c')
  platform_deps += [
    cc.find_library('gdi32'),
    cc.find_library('user32'),
    cc.find_library('shell32'),
    cc.find_library('advapi32'),
    cc.find_library('ole32'),
  ]
else
  libscreenshooter_platform_sources += 'sc-platform-linux.c'
endif

libscreenshooter_platform = static_library(
  'screenshooter-platform',
  libscreenshooter_platform_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: platform_deps,
  install: false,
)

libscreenshooter_platform_dep = declare_dependency(
  link_with: libscreenshooter_platform,
  include_directories: include_directories('.'),
  dependencies: platform_deps,
)
```

- [ ] **Step 3: Verify Linux build still works**

Run: `ninja -C builddir`

- [ ] **Step 4: Commit**

```bash
git add lib/platform/meson.build
git commit -m "build: select platform backend based on host_machine.system()"
```

---

### Task 3: Make top-level meson.build cross-platform

**Files:**
- Modify: `meson.build`

This is the most delicate task. Must preserve exact Linux behavior while enabling Windows builds.

- [ ] **Step 1: Read current top-level meson.build**

Read `meson.build` carefully. Note all variables defined and where they're used.

- [ ] **Step 2: Apply changes**

Key changes (in order of appearance in the file):

1. **Wrap `gnome` and `i18n` imports** — these require tools that may not exist on MSYS2:
```meson
if host_machine.system() != 'windows'
  gnome = import('gnome')
  i18n = import('i18n')
endif
```

2. **Move cross-platform deps before the conditional block:**
```meson
glib = dependency('glib-2.0', version: dependency_versions['glib'])
libcurl = dependency('libcurl', version: '>= 7.68.0')
json_glib = dependency('json-glib-1.0', version: '>= 1.4.0')
libm = cc.find_library('m', required: false)
```

3. **Wrap ALL Linux-only deps** (GTK, XFCE, X11, Wayland, exo) in:
```meson
if host_machine.system() != 'windows'
  gtk = dependency('gtk+-3.0', ...)
  libxfce4util = dependency(...)
  # ... everything up to and including Wayland ...
endif
```

4. **Guard the X11/Wayland error check:**
```meson
if host_machine.system() != 'windows'
  if not enable_x11 and not enable_wayland
    error('At least one of the X11 and Wayland backends must be enabled')
  endif
endif
```
Without this guard, the Windows build will hit this error since neither X11 nor Wayland deps are found.

5. **Guard `feature_cflags` for X11/Wayland** — the `enable_x11` and `enable_wayland` variables won't exist on Windows. Initialize them to `false` before the conditional block, or put all their usage inside the Linux conditional.

6. **Wrap `vcs_tag()` and XFCE-specific `extra_cflags`** — the `xfce_revision_h` and XFCE-related `-D` defines should be Linux-only. On Windows, provide minimal defines:
```meson
if host_machine.system() == 'windows'
  extra_cflags += [
    '-DPACKAGE="xfce4-screenshooter"',
    '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
    '-DVERSION="@0@"'.format(meson.project_version()),
  ]
endif
```

7. **Subdirectory ordering:**
```meson
subdir('lib/platform')
subdir('lib/core')

if host_machine.system() != 'windows'
  subdir('icons')
  subdir('po')
  subdir('protocols')
  subdir('lib/ui-gtk')
  subdir('panel-plugin')
endif

subdir('src')
```

- [ ] **Step 3: Verify Linux build still works**

Run: `meson setup builddir --wipe -Dwayland=disabled && ninja -C builddir`
Expected: Full successful build, identical to before.

- [ ] **Step 4: Commit**

```bash
git add meson.build
git commit -m "build: make top-level meson.build cross-platform"
```

---

### Task 4: Create minimal Windows entry point and update src/meson.build

**Files:**
- Create: `src/main-win32.c`
- Modify: `src/meson.build`

- [ ] **Step 1: Create minimal Windows main**

Create `src/main-win32.c`:

```c
#include <glib.h>
#include <sc-cloud-config.h>
#include <sc-platform.h>
#include <sc-r2.h>
#include <sc-jira.h>
#include <sc-recorder.h>
#include <sc-video-editor-blur.h>
#include <stdio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

int
main (int argc, char **argv)
{
#ifdef G_OS_WIN32
  /* Enable per-monitor DPI awareness for correct capture on high-DPI displays */
  SetProcessDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

  /* Validate platform layer */
  gchar *config_dir = sc_platform_config_dir ();
  g_print ("Config dir: %s\n", config_dir);

  /* Validate cloud config */
  CloudConfig *config = sc_cloud_config_create_default ();
  g_print ("Cloud config created (loaded=%d)\n", config->loaded);

  gboolean exists = sc_cloud_config_exists (config_dir);
  g_print ("Config exists: %s\n", exists ? "yes" : "no");

  /* Validate video editor blur types */
  VideoEditorState *state = video_editor_state_new ("test.mp4");
  g_print ("Video editor state created\n");
  video_editor_state_free (state);

  sc_cloud_config_free (config);
  g_free (config_dir);

  g_print ("Windows build validation OK\n");
  return 0;
}
```

- [ ] **Step 2: Update src/meson.build**

Read `src/meson.build` first. Then wrap the existing Linux build in a conditional and add the Windows build. The existing code uses `xfce_revision_h` and `i18n.merge_file` — both must be inside the Linux conditional:

```meson
if host_machine.system() == 'windows'
  executable(
    'xfce4-screenshooter',
    'main-win32.c',
    include_directories: [include_directories('..')],
    dependencies: [
      glib,
      libscreenshooter_core_dep,
      libscreenshooter_platform_dep,
    ],
    install: true,
  )
else
  # Existing Linux build code (xfce_revision_h, i18n.merge_file, etc.)
  # ... keep all existing code exactly as-is ...
endif
```

- [ ] **Step 3: Verify Linux build still works**

Run: `ninja -C builddir`

- [ ] **Step 4: Commit**

```bash
git add src/main-win32.c src/meson.build
git commit -m "feat: add minimal Windows entry point for build validation"
```

---

## Chunk 2: GitHub Actions Windows CI

### Task 5: Create Windows CI workflow

**Files:**
- Create: `.github/workflows/ci-windows.yml`

- [ ] **Step 1: Write the GitHub Actions workflow**

Create `.github/workflows/ci-windows.yml`:

```yaml
name: CI Windows

on:
  push:
    branches: [master]
  pull_request:
    branches: [master]

jobs:
  build-windows:
    runs-on: windows-latest
    defaults:
      run:
        shell: msys2 {0}

    steps:
      - uses: actions/checkout@v4

      - uses: msys2/setup-msys2@v2
        with:
          msystem: MINGW64
          update: true
          install: >-
            mingw-w64-x86_64-gcc
            mingw-w64-x86_64-meson
            mingw-w64-x86_64-ninja
            mingw-w64-x86_64-pkg-config
            mingw-w64-x86_64-glib2
            mingw-w64-x86_64-gdk-pixbuf2
            mingw-w64-x86_64-curl
            mingw-w64-x86_64-json-glib
            mingw-w64-x86_64-gettext

      - name: Configure
        run: meson setup build

      - name: Build
        run: meson compile -C build

      - name: Run validation
        run: ./build/src/xfce4-screenshooter.exe
```

**Note:** Tests are not yet integrated into Meson's `test()` system. The validation binary exercises the core + platform libraries. Full test integration can be added when `test/meson.build` is created in a future phase.

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/ci-windows.yml
git commit -m "ci: add Windows build workflow with MSYS2/MinGW64"
```

---

### Task 6: Push and verify CI passes

- [ ] **Step 1: Push to trigger CI**

Push to trigger both Linux and Windows CI workflows.

- [ ] **Step 2: Monitor CI**

Check GitHub Actions:
- Linux CI (`ci.yml`): should still pass unchanged
- Windows CI (`ci-windows.yml`): should build and run the validation binary

- [ ] **Step 3: Fix any CI failures**

Common issues to watch for:
- Missing `#define _WIN32_WINNT 0x0A00` for DPI awareness API (needs Windows 10 SDK)
- Win32 API function declarations needing specific `_WIN32_WINNT` version
- GLib/GdkPixbuf pkg-config differences on MSYS2
- Missing DLLs at runtime (check `PATH` includes MinGW64 bin dir)
- `gnome` or `i18n` module imports failing if not properly guarded

Iterate until both CI workflows pass.

- [ ] **Step 4: Final commit with any CI fixes**

```bash
git add -A
git commit -m "fix: resolve Windows CI build issues"
```
