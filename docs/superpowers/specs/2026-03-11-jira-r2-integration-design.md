# Jira + Cloudflare R2 Integration for xfce4-screenshooter

**Date:** 2026-03-11
**Status:** Approved

## Overview

Add native Cloudflare R2 upload and Jira issue commenting to xfce4-screenshooter, ported from the jira-proofs project (Tauri/Rust) into C/GTK. Video recording is out of scope (future work).

## Configuration

File: `~/.config/xfce4-screenshooter/cloud.toml`

```toml
[jira]
base_url = "https://yourteam.atlassian.net"
email = "you@example.com"
api_token = "your-jira-api-token"
default_project = "PROJ"

[r2]
account_id = "your-cf-account-id"
access_key_id = "your-r2-access-key"
secret_access_key = "your-r2-secret"
bucket = "screenshots"
public_url = "https://assets.yourdomain.com"

[presets]
bug_evidence = "Bug Evidence"
work_evidence = "Work Evidence"
```

Parsed by `lib/screenshooter-cloud-config.c` into a `CloudConfig` struct. Minimal hand-written TOML parser (simple key-value sections only).

## R2 Upload Module

File: `lib/screenshooter-r2.c`

- AWS Signature V4 signing using GLib's `g_compute_hmac` (HMAC-SHA256)
- Upload via libcurl PUT request to `https://{account_id}.r2.cloudflarestorage.com`
- Object key: `captures/{YYYY-MM-DD}/{filename}`
- Returns public URL: `{public_url}/captures/{YYYY-MM-DD}/{filename}`
- Content-type detection from file extension
- Progress callback for existing upload dialog infrastructure

```c
gchar *screenshooter_r2_upload(const CloudConfig *config,
                                const gchar *file_path,
                                GError **error);
```

## Jira Integration Module

File: `lib/screenshooter-jira.c`

- HTTP Basic Auth: `base64(email:api_token)`
- Issue search: `GET /rest/api/3/search?jql=...` (limit 20)
  - Default JQL: `project = {default_project} AND status != Done ORDER BY updated DESC`
  - With query: adds `AND summary ~ "query"`
- Post comment: `POST /rest/api/3/issue/{key}/comment`
  - ADF format: heading (preset title) + description paragraph + embedded image URL
- HTTP via libcurl, JSON via json-glib

```c
GList *screenshooter_jira_search(const CloudConfig *config,
                                  const gchar *query,
                                  GError **error);

gboolean screenshooter_jira_post_comment(const CloudConfig *config,
                                          const gchar *issue_key,
                                          const gchar *preset_title,
                                          const gchar *description,
                                          const gchar *image_url,
                                          GError **error);
```

## Jira Issue Search Dialog

File: `lib/screenshooter-jira-dialog.c`

Modal GTK3 dialog:
- `GtkSearchEntry` — type to search issues (300ms debounce via `g_timeout_add`)
- `GtkListBox` — search results (issue key + summary per row)
- `GtkComboBoxText` — preset selector (from cloud.toml presets)
- `GtkTextView` — optional description text
- Cancel / Post buttons

```c
gboolean screenshooter_jira_dialog_run(GtkWindow *parent,
                                        const CloudConfig *config,
                                        const gchar *image_url);
```

## Action System Integration

Extend action bitmask in `screenshooter-global.h`:
```c
UPLOAD_R2   = 16,
POST_JIRA   = 32,
```

Flags compose with existing actions (e.g. `SAVE | UPLOAD_R2 | POST_JIRA`).

Flow in `screenshooter-actions.c` `action_idle()`:
1. Existing actions run first (save/clipboard/open/custom)
2. If `UPLOAD_R2` — upload, store returned URL
3. If `POST_JIRA` — open Jira dialog (uses R2 URL if available, warns if not)

## Screenshot Dialog Changes

In `screenshooter-dialogs.c`, add a "Cloud" frame below existing actions:
- Checkbox: "Upload to Cloudflare R2"
- Checkbox: "Post to Jira issue"
- Greyed out with tooltip if `cloud.toml` missing/incomplete
- "Post to Jira" auto-checks "Upload to R2" (warning if unchecked)

## CLI Flags

In `src/main.c`:
- `--upload-r2` / `-u` — upload to R2 after capture
- `--jira` / `-j ISSUE_KEY` — post to Jira issue (skips search dialog)

## New Dependencies

- `libcurl` — HTTP for R2 and Jira
- `json-glib-1.0` — JSON parsing for Jira API

No external TOML library (minimal built-in parser).

## New Files

| File | Purpose |
|------|---------|
| `lib/screenshooter-cloud-config.c/.h` | TOML config parser, CloudConfig struct |
| `lib/screenshooter-r2.c/.h` | R2 upload with AWS SigV4 |
| `lib/screenshooter-jira.c/.h` | Jira search + comment posting |
| `lib/screenshooter-jira-dialog.c/.h` | GTK issue search dialog |

## Modified Files

| File | Change |
|------|--------|
| `lib/screenshooter-global.h` | Add UPLOAD_R2, POST_JIRA action flags |
| `lib/screenshooter-actions.c` | Handle new action flags |
| `lib/screenshooter-dialogs.c` | Add Cloud section with checkboxes |
| `lib/meson.build` | Add new sources + deps |
| `src/main.c` | Add --upload-r2, --jira CLI flags |
| `meson.build` | Add libcurl, json-glib deps |
