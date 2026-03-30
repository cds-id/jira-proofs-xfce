# Jira Multi-Workspace Support

**Date:** 2026-03-30
**Status:** Approved

## Overview

Support multiple Jira Cloud workspaces (e.g., `team-a.atlassian.net`, `team-b.atlassian.net`) under a single set of credentials (one email + one API token). Includes unified cross-workspace search, grouped results in the UI, auto-detect in CLI, and video links (not embeds) in Jira comments.

## 1. Config Format & Data Model

### New `cloud.toml` format

```toml
[jira]
email = "you@example.com"
api_token = "your-token"

[jira.workspace.team-a]
base_url = "https://team-a.atlassian.net"
default_project = "BNS"

[jira.workspace.team-b]
base_url = "https://team-b.atlassian.net"
default_project = "OPS"
```

Credentials (`email`, `api_token`) live under `[jira]`. Each workspace is a named GKeyFile group `[jira.workspace.<label>]` with `base_url` and `default_project`.

### Auto-migration from old format

On load, if `[jira]` contains a `base_url` key (old single-workspace format):

1. Extract the hostname prefix from the URL (e.g., `team-a` from `https://team-a.atlassian.net`) to use as the workspace label.
2. Create `[jira.workspace.<label>]` with `base_url` and `default_project` values from `[jira]`.
3. Remove `base_url` and `default_project` from `[jira]`.
4. Save the config file.

### C structs

```c
typedef struct {
  gchar *label;           /* "team-a" */
  gchar *base_url;        /* "https://team-a.atlassian.net" */
  gchar *default_project; /* "BNS" */
} JiraWorkspace;

typedef struct {
  gchar *email;
  gchar *api_token;
  JiraWorkspace *workspaces;  /* heap-allocated array */
  gsize n_workspaces;
} JiraCloudConfig;
```

### Validation

`sc_cloud_config_valid_jira()` returns TRUE when:
- `email` and `api_token` are non-NULL and non-empty
- `n_workspaces >= 1`
- Every workspace has non-NULL, non-empty `base_url`

## 2. Jira API Changes

### New grouped search result type

```c
typedef struct {
  JiraWorkspace *workspace;  /* pointer, not owned */
  GList *issues;             /* GList of JiraIssue* */
} JiraSearchGroup;
```

### `sc_jira_search()` — refactored signature

```c
GList *sc_jira_search (const gchar *email, const gchar *api_token,
                       const JiraWorkspace *workspace,
                       const gchar *query, GError **error);
```

Takes shared credentials + a single workspace. JQL behavior unchanged: empty query uses `default_project`, issue-key regex matches by key, otherwise summary search.

### `sc_jira_search_all()` — new function

```c
GList *sc_jira_search_all (const JiraCloudConfig *jira,
                           const gchar *query, GError **error);
```

Iterates all workspaces sequentially, calling `sc_jira_search()` on each. Returns a `GList *` of `JiraSearchGroup *`. Workspaces with zero results are omitted. Caller frees with `sc_jira_search_group_list_free()`.

### `sc_jira_post_comment()` — refactored signature

```c
gboolean sc_jira_post_comment (const gchar *email, const gchar *api_token,
                               const JiraWorkspace *workspace,
                               const gchar *issue_key,
                               const gchar *preset_title,
                               const gchar *description,
                               const gchar *media_url,
                               gboolean is_video,
                               GError **error);
```

When `is_video == FALSE`: existing behavior — `mediaSingle` with external image embed.

When `is_video == TRUE`: replaces the media block with a paragraph containing an `inlineCard`:

```json
{
  "type": "paragraph",
  "content": [{
    "type": "inlineCard",
    "attrs": { "url": "https://r2.example.com/recording.mp4" }
  }]
}
```

### `sc_jira_test_connection()` — refactored

Tests credentials against the first workspace's `/rest/api/3/myself` endpoint. One successful response validates credentials for all workspaces (same Atlassian account).

```c
gboolean sc_jira_test_connection (const gchar *email, const gchar *api_token,
                                  const JiraWorkspace *workspace,
                                  GError **error);
```

### New free functions

```c
void sc_jira_search_group_free      (JiraSearchGroup *group);
void sc_jira_search_group_list_free (GList *groups);
void sc_jira_workspace_free         (JiraWorkspace *workspace);
```

## 3. UI — Search Dialog

### Grouped results in GtkListBox

The `GtkListBox` displays results grouped by workspace:

- **Header rows** — non-selectable, bold workspace label text (e.g., "team-a"). Added before each group's issues.
- **Issue rows** — selectable, showing `key — summary` as before.
- Headers for workspaces with zero results are omitted.
- If only one workspace has results, its header still shows for consistency.

### Search flow

1. User types in `GtkSearchEntry` (existing 300ms debounce).
2. Call `sc_jira_search_all()` with the query.
3. Rebuild the ListBox: for each `JiraSearchGroup`, insert a header row then issue rows.
4. Selecting an issue row stores both the `JiraIssue *` and the `JiraWorkspace *` so the post knows which workspace to target.

### No other dialog changes

Preset dropdown and description text view are unchanged.

## 4. CLI — Auto-detect with Fallback

### Parsing `--jira` / `-j` argument

1. If the argument contains `:` (e.g., `team-a:BNS-2727`), split into workspace label + issue key. Look up the workspace by label and target it directly. Error if label not found.
2. If no `:`, **auto-detect**: iterate all workspaces, query `key = ISSUE_KEY` on each.
   - **Exactly one match** → use that workspace.
   - **Zero matches** → error: `"Issue ISSUE_KEY not found on any configured workspace."`
   - **Multiple matches** → error: `"Ambiguous issue key ISSUE_KEY found on: team-a, team-b. Specify workspace: -j team-a:ISSUE_KEY"`

### Performance

Sequential search, worst case `n_workspaces * 15s` timeout. Acceptable for typical 2-3 workspace setups.

## 5. Setup Wizard — Workspace List Page

### Layout: two sections on the Jira page

**Credentials section (top):**
- Email field
- API Token field
- "Test Connection" button (tests against first workspace)

**Workspaces section (bottom):**
- `GtkListBox` displaying current workspaces — each row shows `label: base_url (default_project)`
- "Add" button — opens inline row or small dialog with 3 fields: label, base_url, default_project
- "Remove" button — removes selected workspace; disabled when only one workspace remains (minimum 1 required)

**Validation:** "Next"/"Save" is sensitive only when email + api_token are filled and at least one workspace has all 3 fields (label, base_url, default_project).

**"Reconfigure Cloud Services" button** opens the same page with current values pre-filled.

## 6. Video Link in Comments

### Behavior change

`build_adf_comment_json()` accepts an `is_video` parameter. When true, the `mediaSingle` embed block is replaced with a paragraph containing an `inlineCard` node that renders as a clickable link card in Jira.

### Detection

The action layer determines `is_video` by checking if the R2 URL ends with a video extension: `.mp4`, `.webm`, `.mkv`.

## Files Modified

| File | Change |
|------|--------|
| `lib/core/sc-cloud-config.h` | New `JiraWorkspace` struct, updated `JiraCloudConfig` |
| `lib/core/sc-cloud-config.c` | Parse named workspace sections, auto-migration logic |
| `lib/core/sc-jira.h` | New `JiraSearchGroup`, updated function signatures, `sc_jira_search_all()` |
| `lib/core/sc-jira.c` | Refactored search/post/test functions, video link ADF, grouped search |
| `lib/ui-gtk/screenshooter-jira-dialog.c` | Grouped ListBox with headers, store workspace per selection |
| `lib/ui-gtk/screenshooter-actions.c` | Pass `is_video` flag, workspace resolution for CLI |
| `lib/ui-gtk/screenshooter-dialogs.c` | Wizard workspace list management UI |
| `src/main.c` | Parse `label:KEY` syntax in `-j` argument |
| `test/test-cloud-config-save.c` | Update tests for new config format |
