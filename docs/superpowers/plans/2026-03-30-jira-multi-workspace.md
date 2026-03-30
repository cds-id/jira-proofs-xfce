# Jira Multi-Workspace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Support multiple Jira Cloud workspaces under shared credentials, with unified search, grouped UI results, CLI auto-detect, and video link comments.

**Architecture:** Replace the single `JiraCloudConfig` with shared credentials + a `JiraWorkspace` array. All Jira API functions take credentials + workspace instead of the full config. The config parser reads named `[jira.workspace.<label>]` GKeyFile groups and auto-migrates old single-workspace format.

**Tech Stack:** C, GLib/GKeyFile, GTK3, libcurl, json-glib

---

### Task 1: Update JiraCloudConfig Struct and Header

**Files:**
- Modify: `lib/core/sc-cloud-config.h:6-11`

- [ ] **Step 1: Write the failing test**

Create `test/test-multi-workspace-config.c`:

```c
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include "sc-cloud-config.h"


static gchar *
create_temp_dir (void)
{
  gchar *tmpl = g_build_filename (g_get_tmp_dir (),
                                   "test-multi-ws-XXXXXX", NULL);
  gchar *dir = g_mkdtemp (tmpl);
  g_assert_nonnull (dir);
  return dir;
}


static void
remove_temp_dir (const gchar *dir)
{
  gchar *path = sc_cloud_config_get_path (dir);
  if (g_file_test (path, G_FILE_TEST_EXISTS))
    g_unlink (path);
  g_free (path);
  g_rmdir (dir);
}


static void
test_new_struct_has_workspaces (void)
{
  CloudConfig *config = sc_cloud_config_create_default ();

  /* New struct should have shared credentials and zero workspaces */
  g_assert_cmpstr (config->jira.email, ==, "");
  g_assert_cmpstr (config->jira.api_token, ==, "");
  g_assert_cmpuint (config->jira.n_workspaces, ==, 0);
  g_assert_null (config->jira.workspaces);

  sc_cloud_config_free (config);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/multi-workspace/new-struct-has-workspaces",
                    test_new_struct_has_workspaces);

  return g_test_run ();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-multi-workspace-config.c -o /tmp/test-multi-ws && /tmp/test-multi-ws`

Expected: FAIL — `n_workspaces` and `workspaces` are not members of `JiraCloudConfig`.

- [ ] **Step 3: Update the header**

Replace the contents of `lib/core/sc-cloud-config.h`:

```c
#ifndef __SC_CLOUD_CONFIG_H__
#define __SC_CLOUD_CONFIG_H__

#include <glib.h>

typedef struct {
  gchar *label;
  gchar *base_url;
  gchar *default_project;
} JiraWorkspace;

typedef struct {
  gchar *email;
  gchar *api_token;
  JiraWorkspace *workspaces;
  gsize n_workspaces;
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

CloudConfig *sc_cloud_config_load           (const gchar *config_dir, GError **error);
gboolean     sc_cloud_config_save           (const CloudConfig *config, const gchar *config_dir, GError **error);
void         sc_cloud_config_free           (CloudConfig *config);
CloudConfig *sc_cloud_config_create_default (void);
gboolean     sc_cloud_config_valid_r2       (const CloudConfig *config);
gboolean     sc_cloud_config_valid_jira     (const CloudConfig *config);
gchar       *sc_cloud_config_get_path       (const gchar *config_dir);
gboolean     sc_cloud_config_exists         (const gchar *config_dir);

#endif
```

- [ ] **Step 4: Update `sc_cloud_config_create_default()` in `lib/core/sc-cloud-config.c`**

Replace the jira initialization in `sc_cloud_config_create_default()` (lines 216-219):

```c
  config->jira.email = g_strdup ("");
  config->jira.api_token = g_strdup ("");
  config->jira.workspaces = NULL;
  config->jira.n_workspaces = 0;
```

- [ ] **Step 5: Update `sc_cloud_config_free()` in `lib/core/sc-cloud-config.c`**

Replace the jira free block (lines 193-196):

```c
  g_free (config->jira.email);
  g_free (config->jira.api_token);
  for (gsize i = 0; i < config->jira.n_workspaces; i++)
    {
      g_free (config->jira.workspaces[i].label);
      g_free (config->jira.workspaces[i].base_url);
      g_free (config->jira.workspaces[i].default_project);
    }
  g_free (config->jira.workspaces);
```

- [ ] **Step 6: Update `sc_cloud_config_valid_jira()` in `lib/core/sc-cloud-config.c`**

Replace the validation function (lines 250-258):

```c
gboolean
sc_cloud_config_valid_jira (const CloudConfig *config)
{
  if (config == NULL || !config->loaded)
    return FALSE;
  if (config->jira.email[0] == '\0' ||
      config->jira.api_token[0] == '\0')
    return FALSE;
  if (config->jira.n_workspaces == 0)
    return FALSE;
  for (gsize i = 0; i < config->jira.n_workspaces; i++)
    {
      if (config->jira.workspaces[i].base_url == NULL ||
          config->jira.workspaces[i].base_url[0] == '\0')
        return FALSE;
    }
  return TRUE;
}
```

- [ ] **Step 7: Run test to verify it passes**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-multi-workspace-config.c -o /tmp/test-multi-ws && /tmp/test-multi-ws`

Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add lib/core/sc-cloud-config.h lib/core/sc-cloud-config.c test/test-multi-workspace-config.c
git commit -m "refactor: update JiraCloudConfig to workspace array struct"
```

---

### Task 2: Config Parser — Load and Save Multi-Workspace Format

**Files:**
- Modify: `lib/core/sc-cloud-config.c:75-126` (load), `lib/core/sc-cloud-config.c:129-184` (save)
- Modify: `test/test-multi-workspace-config.c`

- [ ] **Step 1: Add test for loading new format**

Append to `test/test-multi-workspace-config.c` before `main()`:

```c
static void
test_load_multi_workspace_format (void)
{
  gchar *dir = create_temp_dir ();
  gchar *path = sc_cloud_config_get_path (dir);
  GError *error = NULL;

  /* Write a multi-workspace config file */
  const gchar *content =
    "[jira]\n"
    "email=user@example.com\n"
    "api_token=tok123\n"
    "\n"
    "[jira.workspace.team-a]\n"
    "base_url=https://team-a.atlassian.net\n"
    "default_project=BNS\n"
    "\n"
    "[jira.workspace.team-b]\n"
    "base_url=https://team-b.atlassian.net\n"
    "default_project=OPS\n"
    "\n"
    "[r2]\n"
    "account_id=\n"
    "access_key_id=\n"
    "secret_access_key=\n"
    "bucket=\n"
    "public_url=\n";

  g_mkdir_with_parents (dir, 0700);
  g_file_set_contents (path, content, -1, &error);
  g_assert_no_error (error);

  CloudConfig *config = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (config);

  g_assert_cmpstr (config->jira.email, ==, "user@example.com");
  g_assert_cmpstr (config->jira.api_token, ==, "tok123");
  g_assert_cmpuint (config->jira.n_workspaces, ==, 2);

  /* Workspaces may be in any order, find team-a and team-b */
  gboolean found_a = FALSE, found_b = FALSE;
  for (gsize i = 0; i < config->jira.n_workspaces; i++)
    {
      if (g_strcmp0 (config->jira.workspaces[i].label, "team-a") == 0)
        {
          g_assert_cmpstr (config->jira.workspaces[i].base_url, ==,
                           "https://team-a.atlassian.net");
          g_assert_cmpstr (config->jira.workspaces[i].default_project, ==, "BNS");
          found_a = TRUE;
        }
      else if (g_strcmp0 (config->jira.workspaces[i].label, "team-b") == 0)
        {
          g_assert_cmpstr (config->jira.workspaces[i].base_url, ==,
                           "https://team-b.atlassian.net");
          g_assert_cmpstr (config->jira.workspaces[i].default_project, ==, "OPS");
          found_b = TRUE;
        }
    }
  g_assert_true (found_a);
  g_assert_true (found_b);

  sc_cloud_config_free (config);
  remove_temp_dir (dir);
  g_free (path);
  g_free (dir);
}
```

Register it in `main()`:

```c
  g_test_add_func ("/multi-workspace/load-multi-workspace-format",
                    test_load_multi_workspace_format);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-multi-workspace-config.c -o /tmp/test-multi-ws && /tmp/test-multi-ws`

Expected: FAIL — `n_workspaces` is 0, parser doesn't read workspace groups yet.

- [ ] **Step 3: Implement multi-workspace loading in `sc_cloud_config_load()`**

Replace the jira loading block in `sc_cloud_config_load()` (lines 101-106) with:

```c
  /* Load shared jira credentials */
  ht = parse_toml_section (kf, "jira");
  config->jira.email = ht_take_string (ht, "email");
  config->jira.api_token = ht_take_string (ht, "api_token");
  g_hash_table_destroy (ht);

  /* Load workspaces from [jira.workspace.*] groups */
  {
    gchar **groups = g_key_file_get_groups (kf, NULL);
    GPtrArray *ws_arr = g_ptr_array_new ();
    const gchar *prefix = "jira.workspace.";
    gsize prefix_len = strlen (prefix);

    for (gint i = 0; groups[i] != NULL; i++)
      {
        if (g_str_has_prefix (groups[i], prefix))
          {
            const gchar *label = groups[i] + prefix_len;
            JiraWorkspace ws;
            GHashTable *ws_ht = parse_toml_section (kf, groups[i]);

            ws.label = g_strdup (label);
            ws.base_url = ht_take_string (ws_ht, "base_url");
            ws.default_project = ht_take_string (ws_ht, "default_project");
            g_hash_table_destroy (ws_ht);

            g_ptr_array_add (ws_arr, g_memdup2 (&ws, sizeof (JiraWorkspace)));
          }
      }

    config->jira.n_workspaces = ws_arr->len;
    if (ws_arr->len > 0)
      {
        config->jira.workspaces = g_new (JiraWorkspace, ws_arr->len);
        for (guint i = 0; i < ws_arr->len; i++)
          {
            config->jira.workspaces[i] = *(JiraWorkspace *) ws_arr->pdata[i];
            g_free (ws_arr->pdata[i]);
          }
      }
    else
      {
        config->jira.workspaces = NULL;
      }

    g_ptr_array_free (ws_arr, TRUE);
    g_strfreev (groups);
  }
```

Note: `g_memdup2` requires GLib 2.68+. If the build targets older GLib, use `g_memdup` instead (same signature but deprecated). Check the project's minimum GLib version in `meson.build`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-multi-workspace-config.c -o /tmp/test-multi-ws && /tmp/test-multi-ws`

Expected: PASS

- [ ] **Step 5: Add test for save round-trip**

Append to `test/test-multi-workspace-config.c` before `main()`:

```c
static void
test_save_and_load_multi_workspace (void)
{
  gchar *dir = create_temp_dir ();
  GError *error = NULL;

  CloudConfig *config = sc_cloud_config_create_default ();

  g_free (config->jira.email);
  config->jira.email = g_strdup ("user@example.com");
  g_free (config->jira.api_token);
  config->jira.api_token = g_strdup ("tok123");

  config->jira.n_workspaces = 2;
  config->jira.workspaces = g_new0 (JiraWorkspace, 2);
  config->jira.workspaces[0].label = g_strdup ("team-a");
  config->jira.workspaces[0].base_url = g_strdup ("https://team-a.atlassian.net");
  config->jira.workspaces[0].default_project = g_strdup ("BNS");
  config->jira.workspaces[1].label = g_strdup ("team-b");
  config->jira.workspaces[1].base_url = g_strdup ("https://team-b.atlassian.net");
  config->jira.workspaces[1].default_project = g_strdup ("OPS");

  config->loaded = TRUE;

  gboolean ok = sc_cloud_config_save (config, dir, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  sc_cloud_config_free (config);

  CloudConfig *loaded = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (loaded);

  g_assert_cmpstr (loaded->jira.email, ==, "user@example.com");
  g_assert_cmpstr (loaded->jira.api_token, ==, "tok123");
  g_assert_cmpuint (loaded->jira.n_workspaces, ==, 2);

  sc_cloud_config_free (loaded);
  remove_temp_dir (dir);
  g_free (dir);
}
```

Register in `main()`:

```c
  g_test_add_func ("/multi-workspace/save-and-load-round-trip",
                    test_save_and_load_multi_workspace);
```

- [ ] **Step 6: Implement multi-workspace saving in `sc_cloud_config_save()`**

Replace the `[jira]` save block in `sc_cloud_config_save()` (lines 155-159) with:

```c
  /* Remove old-style jira keys if present */
  g_key_file_remove_key (kf, "jira", "base_url", NULL);
  g_key_file_remove_key (kf, "jira", "default_project", NULL);

  /* Remove all existing [jira.workspace.*] groups */
  {
    gchar **groups = g_key_file_get_groups (kf, NULL);
    for (gint i = 0; groups[i] != NULL; i++)
      {
        if (g_str_has_prefix (groups[i], "jira.workspace."))
          g_key_file_remove_group (kf, groups[i], NULL);
      }
    g_strfreev (groups);
  }

  /* Write shared credentials */
  g_key_file_set_string (kf, "jira", "email",
                         config->jira.email ? config->jira.email : "");
  g_key_file_set_string (kf, "jira", "api_token",
                         config->jira.api_token ? config->jira.api_token : "");

  /* Write each workspace */
  for (gsize i = 0; i < config->jira.n_workspaces; i++)
    {
      gchar *group = g_strdup_printf ("jira.workspace.%s",
                                       config->jira.workspaces[i].label);
      g_key_file_set_string (kf, group, "base_url",
                             config->jira.workspaces[i].base_url
                               ? config->jira.workspaces[i].base_url : "");
      g_key_file_set_string (kf, group, "default_project",
                             config->jira.workspaces[i].default_project
                               ? config->jira.workspaces[i].default_project : "");
      g_free (group);
    }
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-multi-workspace-config.c -o /tmp/test-multi-ws && /tmp/test-multi-ws`

Expected: All 3 tests PASS

- [ ] **Step 8: Commit**

```bash
git add lib/core/sc-cloud-config.c test/test-multi-workspace-config.c
git commit -m "feat: load and save multi-workspace jira config"
```

---

### Task 3: Auto-Migration from Old Config Format

**Files:**
- Modify: `lib/core/sc-cloud-config.c` (inside `sc_cloud_config_load`)
- Modify: `test/test-multi-workspace-config.c`

- [ ] **Step 1: Add migration test**

Append to `test/test-multi-workspace-config.c` before `main()`:

```c
static void
test_auto_migrate_old_format (void)
{
  gchar *dir = create_temp_dir ();
  gchar *path = sc_cloud_config_get_path (dir);
  GError *error = NULL;

  /* Write old single-workspace format */
  const gchar *content =
    "[jira]\n"
    "base_url=https://myteam.atlassian.net\n"
    "email=user@example.com\n"
    "api_token=tok123\n"
    "default_project=PROJ\n"
    "\n"
    "[r2]\n"
    "account_id=acct\n"
    "access_key_id=key\n"
    "secret_access_key=secret\n"
    "bucket=mybucket\n"
    "public_url=https://assets.example.com\n";

  g_mkdir_with_parents (dir, 0700);
  g_file_set_contents (path, content, -1, &error);
  g_assert_no_error (error);

  /* Load should auto-migrate */
  CloudConfig *config = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (config);

  g_assert_cmpstr (config->jira.email, ==, "user@example.com");
  g_assert_cmpstr (config->jira.api_token, ==, "tok123");
  g_assert_cmpuint (config->jira.n_workspaces, ==, 1);
  g_assert_cmpstr (config->jira.workspaces[0].label, ==, "myteam");
  g_assert_cmpstr (config->jira.workspaces[0].base_url, ==,
                   "https://myteam.atlassian.net");
  g_assert_cmpstr (config->jira.workspaces[0].default_project, ==, "PROJ");

  sc_cloud_config_free (config);

  /* Verify the file was rewritten — reload should still work */
  config = sc_cloud_config_load (dir, &error);
  g_assert_no_error (error);
  g_assert_nonnull (config);
  g_assert_cmpuint (config->jira.n_workspaces, ==, 1);
  g_assert_cmpstr (config->jira.workspaces[0].label, ==, "myteam");

  sc_cloud_config_free (config);
  remove_temp_dir (dir);
  g_free (path);
  g_free (dir);
}
```

Register in `main()`:

```c
  g_test_add_func ("/multi-workspace/auto-migrate-old-format",
                    test_auto_migrate_old_format);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-multi-workspace-config.c -o /tmp/test-multi-ws && /tmp/test-multi-ws`

Expected: FAIL — old format has `base_url` under `[jira]`, no `[jira.workspace.*]` groups, so `n_workspaces == 0`.

- [ ] **Step 3: Implement migration in `sc_cloud_config_load()`**

After loading the shared credentials and before the workspace group scanning loop, add migration logic. Insert this right after `g_hash_table_destroy (ht);` for the jira credentials section:

```c
  /* Auto-migrate old single-workspace format */
  {
    gchar *old_base_url = g_key_file_get_string (kf, "jira", "base_url", NULL);
    if (old_base_url && old_base_url[0] != '\0')
      {
        gchar *old_project = g_key_file_get_string (kf, "jira", "default_project", NULL);

        /* Extract label from URL: https://myteam.atlassian.net -> myteam */
        gchar *label = NULL;
        {
          gchar *stripped = strip_toml_quotes (g_strdup (old_base_url));
          const gchar *host_start = strstr (stripped, "://");
          if (host_start)
            host_start += 3;
          else
            host_start = stripped;
          const gchar *dot = strchr (host_start, '.');
          if (dot)
            label = g_strndup (host_start, dot - host_start);
          else
            label = g_strdup (host_start);
          g_free (stripped);
        }

        /* Write migrated workspace group */
        gchar *group = g_strdup_printf ("jira.workspace.%s", label);
        g_key_file_set_string (kf, group, "base_url",
                               strip_toml_quotes (g_strdup (old_base_url)));
        g_key_file_set_string (kf, group, "default_project",
                               old_project ? strip_toml_quotes (g_strdup (old_project)) : "");
        g_free (group);

        /* Remove old keys */
        g_key_file_remove_key (kf, "jira", "base_url", NULL);
        g_key_file_remove_key (kf, "jira", "default_project", NULL);

        /* Save migrated file */
        {
          gchar *data = g_key_file_to_data (kf, NULL, NULL);
          g_file_set_contents (path, data, -1, NULL);
          g_free (data);
        }

        g_free (label);
        g_free (old_base_url);
        g_free (old_project);
      }
    else
      {
        g_free (old_base_url);
      }
  }
```

Note: `strip_toml_quotes` takes ownership and returns new pointer. The calls above pass `g_strdup` copies so the originals from `g_key_file_get_string` remain valid.

- [ ] **Step 4: Run tests to verify they all pass**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-multi-workspace-config.c -o /tmp/test-multi-ws && /tmp/test-multi-ws`

Expected: All 4 tests PASS

- [ ] **Step 5: Commit**

```bash
git add lib/core/sc-cloud-config.c test/test-multi-workspace-config.c
git commit -m "feat: auto-migrate old single-workspace jira config"
```

---

### Task 4: Update Existing Config Test

**Files:**
- Modify: `test/test-cloud-config-save.c`

The old test directly accesses `config->jira.base_url` and `config->jira.default_project` which no longer exist. Update it to use the new workspace array.

- [ ] **Step 1: Update `test_save_and_load_round_trip`**

Replace the jira field assignments (lines 42-49) with:

```c
  g_free (config->jira.email);
  config->jira.email = g_strdup ("user@example.com");
  g_free (config->jira.api_token);
  config->jira.api_token = g_strdup ("token123");

  config->jira.n_workspaces = 1;
  config->jira.workspaces = g_new0 (JiraWorkspace, 1);
  config->jira.workspaces[0].label = g_strdup ("test");
  config->jira.workspaces[0].base_url = g_strdup ("https://test.atlassian.net");
  config->jira.workspaces[0].default_project = g_strdup ("TEST");
```

Replace the jira assertions (lines 76-79) with:

```c
  g_assert_cmpstr (loaded->jira.email, ==, "user@example.com");
  g_assert_cmpstr (loaded->jira.api_token, ==, "token123");
  g_assert_cmpuint (loaded->jira.n_workspaces, ==, 1);
  g_assert_cmpstr (loaded->jira.workspaces[0].base_url, ==, "https://test.atlassian.net");
  g_assert_cmpstr (loaded->jira.workspaces[0].default_project, ==, "TEST");
```

- [ ] **Step 2: Update `test_save_preserves_presets`**

Replace the jira field assignment (lines 108-109) with:

```c
  g_free (config->jira.email);
  config->jira.email = g_strdup ("user@example.com");
  config->jira.n_workspaces = 1;
  config->jira.workspaces = g_new0 (JiraWorkspace, 1);
  config->jira.workspaces[0].label = g_strdup ("test");
  config->jira.workspaces[0].base_url = g_strdup ("https://test.atlassian.net");
  config->jira.workspaces[0].default_project = g_strdup ("TEST");
```

Replace the second save's jira field (lines 122-123) with:

```c
  g_free (config->jira.email);
  config->jira.email = g_strdup ("user@example.com");
  config->jira.n_workspaces = 1;
  config->jira.workspaces = g_new0 (JiraWorkspace, 1);
  config->jira.workspaces[0].label = g_strdup ("test");
  config->jira.workspaces[0].base_url = g_strdup ("https://updated.atlassian.net");
  config->jira.workspaces[0].default_project = g_strdup ("TEST");
```

Replace the jira assertion in reload (line 136) with:

```c
  g_assert_cmpstr (loaded->jira.workspaces[0].base_url, ==, "https://updated.atlassian.net");
```

- [ ] **Step 3: Update `test_config_exists`**

Replace (lines 157-158) with:

```c
  g_free (config->jira.email);
  config->jira.email = g_strdup ("user@example.com");
  config->jira.n_workspaces = 1;
  config->jira.workspaces = g_new0 (JiraWorkspace, 1);
  config->jira.workspaces[0].label = g_strdup ("test");
  config->jira.workspaces[0].base_url = g_strdup ("https://test.atlassian.net");
  config->jira.workspaces[0].default_project = g_strdup ("TEST");
```

- [ ] **Step 4: Run old test to verify it passes**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-cloud-config-save.c -o /tmp/test-cc-save && /tmp/test-cc-save`

Expected: All 3 tests PASS

- [ ] **Step 5: Commit**

```bash
git add test/test-cloud-config-save.c
git commit -m "test: update cloud config tests for workspace array"
```

---

### Task 5: Refactor Jira API — Search Per Workspace

**Files:**
- Modify: `lib/core/sc-jira.h`
- Modify: `lib/core/sc-jira.c:42-161`

- [ ] **Step 1: Update the header `lib/core/sc-jira.h`**

Replace the entire file:

```c
#ifndef __SC_JIRA_H__
#define __SC_JIRA_H__

#include <glib.h>
#include "sc-cloud-config.h"

typedef struct {
  gchar *key;
  gchar *summary;
} JiraIssue;

typedef struct {
  JiraWorkspace *workspace;
  GList *issues;
} JiraSearchGroup;

GList    *sc_jira_search               (const gchar *email, const gchar *api_token,
                                        const JiraWorkspace *workspace,
                                        const gchar *query, GError **error);
GList    *sc_jira_search_all           (const JiraCloudConfig *jira,
                                        const gchar *query, GError **error);
gboolean  sc_jira_post_comment         (const gchar *email, const gchar *api_token,
                                        const JiraWorkspace *workspace,
                                        const gchar *issue_key,
                                        const gchar *preset_title,
                                        const gchar *description,
                                        const gchar *media_url,
                                        gboolean is_video,
                                        GError **error);
gboolean  sc_jira_test_connection      (const gchar *email, const gchar *api_token,
                                        const JiraWorkspace *workspace,
                                        GError **error);
void      sc_jira_issue_free           (JiraIssue *issue);
void      sc_jira_issue_list_free      (GList *issues);
void      sc_jira_search_group_free    (JiraSearchGroup *group);
void      sc_jira_search_group_list_free (GList *groups);

#endif
```

- [ ] **Step 2: Refactor `sc_jira_search()` in `lib/core/sc-jira.c`**

Replace the function signature and the lines that use `config->jira.*` (lines 42-161):

```c
GList *
sc_jira_search (const gchar *email,
                const gchar *api_token,
                const JiraWorkspace *workspace,
                const gchar *query,
                GError **error)
{
  CURL *curl;
  CURLcode res;
  CurlBuffer response = { NULL, 0 };
  struct curl_slist *headers = NULL;
  gchar *auth, *url, *jql, *post_body;
  GList *issues = NULL;
  long http_code;

  g_return_val_if_fail (email != NULL && email[0] != '\0', NULL);
  g_return_val_if_fail (api_token != NULL && api_token[0] != '\0', NULL);
  g_return_val_if_fail (workspace != NULL, NULL);
  g_return_val_if_fail (workspace->base_url != NULL && workspace->base_url[0] != '\0', NULL);

  if (query == NULL || query[0] == '\0')
    jql = g_strdup_printf (
      "project = %s AND status != Done ORDER BY updated DESC",
      workspace->default_project);
  else
    {
      gchar *safe = g_strescape (query, NULL);
      if (g_regex_match_simple ("^[A-Z]+-[0-9]+$", safe, 0, 0))
        jql = g_strdup_printf ("key = %s", safe);
      else
        jql = g_strdup_printf (
          "project = %s AND summary ~ \\\"%s\\\" AND status != Done "
          "ORDER BY updated DESC",
          workspace->default_project, safe);
      g_free (safe);
    }

  JsonBuilder *builder = json_builder_new ();
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "jql");
  json_builder_add_string_value (builder, jql);
  json_builder_set_member_name (builder, "maxResults");
  json_builder_add_int_value (builder, 20);
  json_builder_set_member_name (builder, "fields");
  json_builder_begin_array (builder);
  json_builder_add_string_value (builder, "summary");
  json_builder_end_array (builder);
  json_builder_end_object (builder);

  JsonGenerator *gen = json_generator_new ();
  JsonNode *root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  post_body = json_generator_to_data (gen, NULL);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);

  auth = build_auth_header (email, api_token);
  url = g_strdup_printf ("%s/rest/api/3/search/jql", workspace->base_url);

  curl = curl_easy_init ();
  headers = curl_slist_append (headers, auth);
  headers = curl_slist_append (headers, "Content-Type: application/json");

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDS, post_body);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, 15L);

  res = curl_easy_perform (curl);
  if (res != CURLE_OK)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Jira search failed: %s", curl_easy_strerror (res));
      goto cleanup;
    }

  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code < 200 || http_code >= 300)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Jira API error %ld: %s", http_code,
                   response.data ? response.data : "");
      goto cleanup;
    }

  {
    JsonParser *parser = json_parser_new ();
    if (json_parser_load_from_data (parser, response.data, response.len, error))
      {
        JsonNode *rnode = json_parser_get_root (parser);
        JsonObject *obj = json_node_get_object (rnode);
        JsonArray *arr = json_object_get_array_member (obj, "issues");

        for (guint i = 0; i < json_array_get_length (arr); i++)
          {
            JsonObject *issue_obj = json_array_get_object_element (arr, i);
            JsonObject *fields = json_object_get_object_member (issue_obj, "fields");

            JiraIssue *issue = g_new0 (JiraIssue, 1);
            issue->key = g_strdup (json_object_get_string_member (issue_obj, "key"));
            issue->summary = g_strdup (json_object_get_string_member (fields, "summary"));
            issues = g_list_append (issues, issue);
          }
      }
    g_object_unref (parser);
  }

cleanup:
  curl_easy_cleanup (curl);
  curl_slist_free_all (headers);
  g_free (auth);
  g_free (url);
  g_free (jql);
  g_free (post_body);
  g_free (response.data);
  return issues;
}
```

- [ ] **Step 3: Add `sc_jira_search_all()`**

Add after `sc_jira_search()`:

```c
GList *
sc_jira_search_all (const JiraCloudConfig *jira,
                    const gchar *query,
                    GError **error)
{
  GList *groups = NULL;

  for (gsize i = 0; i < jira->n_workspaces; i++)
    {
      GError *ws_error = NULL;
      GList *issues = sc_jira_search (jira->email, jira->api_token,
                                       &jira->workspaces[i],
                                       query, &ws_error);
      if (ws_error)
        {
          g_warning ("Jira search on %s failed: %s",
                     jira->workspaces[i].label, ws_error->message);
          g_error_free (ws_error);
          continue;
        }

      if (issues != NULL)
        {
          JiraSearchGroup *group = g_new0 (JiraSearchGroup, 1);
          group->workspace = &jira->workspaces[i];
          group->issues = issues;
          groups = g_list_append (groups, group);
        }
    }

  return groups;
}
```

- [ ] **Step 4: Add free functions**

Add after `sc_jira_issue_list_free()`:

```c
void
sc_jira_search_group_free (JiraSearchGroup *group)
{
  if (group == NULL)
    return;
  sc_jira_issue_list_free (group->issues);
  g_free (group);
}


void
sc_jira_search_group_list_free (GList *groups)
{
  g_list_free_full (groups, (GDestroyNotify) sc_jira_search_group_free);
}
```

- [ ] **Step 5: Commit**

```bash
git add lib/core/sc-jira.h lib/core/sc-jira.c
git commit -m "refactor: update jira search API for multi-workspace"
```

---

### Task 6: Refactor Jira API — Post Comment with Video Link

**Files:**
- Modify: `lib/core/sc-jira.c:164-323`

- [ ] **Step 1: Update `build_adf_comment_json()` signature and add video link support**

Replace the function (lines 164-256):

```c
static gchar *
build_adf_comment_json (const gchar *preset_title,
                         const gchar *description,
                         const gchar *media_url,
                         gboolean is_video)
{
  JsonBuilder *b = json_builder_new ();

  json_builder_begin_object (b);
  json_builder_set_member_name (b, "body");
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "version");
  json_builder_add_int_value (b, 1);
  json_builder_set_member_name (b, "type");
  json_builder_add_string_value (b, "doc");
  json_builder_set_member_name (b, "content");
  json_builder_begin_array (b);

  /* Heading */
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "type");
  json_builder_add_string_value (b, "heading");
  json_builder_set_member_name (b, "attrs");
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "level");
  json_builder_add_int_value (b, 3);
  json_builder_end_object (b);
  json_builder_set_member_name (b, "content");
  json_builder_begin_array (b);
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "type");
  json_builder_add_string_value (b, "text");
  json_builder_set_member_name (b, "text");
  json_builder_add_string_value (b, preset_title);
  json_builder_end_object (b);
  json_builder_end_array (b);
  json_builder_end_object (b);

  /* Description paragraph */
  if (description && description[0] != '\0')
    {
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "type");
      json_builder_add_string_value (b, "paragraph");
      json_builder_set_member_name (b, "content");
      json_builder_begin_array (b);
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "type");
      json_builder_add_string_value (b, "text");
      json_builder_set_member_name (b, "text");
      json_builder_add_string_value (b, description);
      json_builder_end_object (b);
      json_builder_end_array (b);
      json_builder_end_object (b);
    }

  if (is_video)
    {
      /* Video: inline card link */
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "type");
      json_builder_add_string_value (b, "paragraph");
      json_builder_set_member_name (b, "content");
      json_builder_begin_array (b);
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "type");
      json_builder_add_string_value (b, "inlineCard");
      json_builder_set_member_name (b, "attrs");
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "url");
      json_builder_add_string_value (b, media_url);
      json_builder_end_object (b);
      json_builder_end_object (b);
      json_builder_end_array (b);
      json_builder_end_object (b);
    }
  else
    {
      /* Image: embedded media */
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "type");
      json_builder_add_string_value (b, "mediaSingle");
      json_builder_set_member_name (b, "attrs");
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "layout");
      json_builder_add_string_value (b, "center");
      json_builder_end_object (b);
      json_builder_set_member_name (b, "content");
      json_builder_begin_array (b);
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "type");
      json_builder_add_string_value (b, "media");
      json_builder_set_member_name (b, "attrs");
      json_builder_begin_object (b);
      json_builder_set_member_name (b, "type");
      json_builder_add_string_value (b, "external");
      json_builder_set_member_name (b, "url");
      json_builder_add_string_value (b, media_url);
      json_builder_end_object (b);
      json_builder_end_object (b);
      json_builder_end_array (b);
      json_builder_end_object (b);
    }

  json_builder_end_array (b);
  json_builder_end_object (b);
  json_builder_end_object (b);

  JsonGenerator *gen = json_generator_new ();
  JsonNode *root = json_builder_get_root (b);
  json_generator_set_root (gen, root);
  gchar *json = json_generator_to_data (gen, NULL);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (b);
  return json;
}
```

- [ ] **Step 2: Refactor `sc_jira_post_comment()`**

Replace the function (lines 259-323):

```c
gboolean
sc_jira_post_comment (const gchar *email,
                      const gchar *api_token,
                      const JiraWorkspace *workspace,
                      const gchar *issue_key,
                      const gchar *preset_title,
                      const gchar *description,
                      const gchar *media_url,
                      gboolean is_video,
                      GError **error)
{
  CURL *curl;
  CURLcode res;
  CurlBuffer response = { NULL, 0 };
  struct curl_slist *headers = NULL;

  g_return_val_if_fail (email != NULL && email[0] != '\0', FALSE);
  g_return_val_if_fail (api_token != NULL && api_token[0] != '\0', FALSE);
  g_return_val_if_fail (workspace != NULL, FALSE);

  gchar *auth, *url, *body;
  long http_code;
  gboolean success = FALSE;

  auth = build_auth_header (email, api_token);
  url = g_strdup_printf ("%s/rest/api/3/issue/%s/comment",
                          workspace->base_url, issue_key);
  body = build_adf_comment_json (preset_title, description, media_url, is_video);

  curl = curl_easy_init ();
  headers = curl_slist_append (headers, auth);
  headers = curl_slist_append (headers, "Content-Type: application/json");

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, 15L);

  res = curl_easy_perform (curl);
  if (res != CURLE_OK)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to post comment: %s", curl_easy_strerror (res));
    }
  else
    {
      curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
      if (http_code >= 200 && http_code < 300)
        success = TRUE;
      else
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Jira comment failed %ld: %s", http_code,
                     response.data ? response.data : "");
    }

  curl_easy_cleanup (curl);
  curl_slist_free_all (headers);
  g_free (auth);
  g_free (url);
  g_free (body);
  g_free (response.data);
  return success;
}
```

- [ ] **Step 3: Refactor `sc_jira_test_connection()`**

Replace the function (lines 326-399):

```c
gboolean
sc_jira_test_connection (const gchar *email,
                         const gchar *api_token,
                         const JiraWorkspace *workspace,
                         GError **error)
{
  CURL *curl;
  CURLcode res;
  CurlBuffer response = { NULL, 0 };
  struct curl_slist *headers = NULL;
  gchar *auth, *url;
  long http_code;
  gboolean success = FALSE;

  g_return_val_if_fail (email != NULL && email[0] != '\0', FALSE);
  g_return_val_if_fail (api_token != NULL && api_token[0] != '\0', FALSE);
  g_return_val_if_fail (workspace != NULL, FALSE);

  auth = build_auth_header (email, api_token);
  url = g_strdup_printf ("%s/rest/api/3/myself", workspace->base_url);

  curl = curl_easy_init ();
  if (curl == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to initialize curl");
      g_free (auth);
      g_free (url);
      return FALSE;
    }

  headers = curl_slist_append (headers, auth);
  headers = curl_slist_append (headers, "Content-Type: application/json");

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 10L);

  res = curl_easy_perform (curl);
  if (res != CURLE_OK)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Jira connection failed: %s", curl_easy_strerror (res));
    }
  else
    {
      curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
      if (http_code == 200)
        {
          success = TRUE;
        }
      else if (http_code == 401 || http_code == 403)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                       "Jira authentication failed (HTTP %ld)", http_code);
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Jira test connection failed with HTTP %ld", http_code);
        }
    }

  curl_easy_cleanup (curl);
  curl_slist_free_all (headers);
  g_free (auth);
  g_free (url);
  g_free (response.data);
  return success;
}
```

- [ ] **Step 4: Commit**

```bash
git add lib/core/sc-jira.h lib/core/sc-jira.c
git commit -m "feat: add video link ADF, refactor post/test for workspace params"
```

---

### Task 7: Update Jira Search Dialog — Grouped Results

**Files:**
- Modify: `lib/ui-gtk/screenshooter-jira-dialog.h`
- Modify: `lib/ui-gtk/screenshooter-jira-dialog.c`

- [ ] **Step 1: Update the header**

Replace `lib/ui-gtk/screenshooter-jira-dialog.h`:

```c
#ifndef __SCREENSHOOTER_JIRA_DIALOG_H__
#define __SCREENSHOOTER_JIRA_DIALOG_H__

#include <gtk/gtk.h>
#include <sc-cloud-config.h>

gboolean screenshooter_jira_dialog_run (GtkWindow *parent,
                                         const CloudConfig *config,
                                         const gchar *media_url,
                                         gboolean is_video);

#endif
```

- [ ] **Step 2: Update `JiraDialogData` struct and add workspace tracking**

Replace the struct and add a helper in `screenshooter-jira-dialog.c` (lines 8-18):

```c
typedef struct {
  const CloudConfig *config;
  const gchar *media_url;
  gboolean is_video;
  GtkWidget *search_entry;
  GtkWidget *list_box;
  GtkWidget *preset_combo;
  GtkWidget *desc_view;
  GtkWidget *post_button;
  gchar *selected_key;
  JiraWorkspace *selected_workspace;
  guint search_timeout_id;
} JiraDialogData;
```

- [ ] **Step 3: Update `cb_row_selected` to track workspace**

Replace `cb_row_selected` (lines 32-46):

```c
static void
cb_row_selected (GtkListBox *box, GtkListBoxRow *row, JiraDialogData *data)
{
  if (row == NULL)
    {
      g_free (data->selected_key);
      data->selected_key = NULL;
      data->selected_workspace = NULL;
      gtk_widget_set_sensitive (data->post_button, FALSE);
      return;
    }

  /* Skip header rows */
  if (g_object_get_data (G_OBJECT (row), "is-header"))
    {
      gtk_list_box_unselect_row (box, row);
      return;
    }

  const gchar *key = g_object_get_data (G_OBJECT (row), "issue-key");
  JiraWorkspace *ws = g_object_get_data (G_OBJECT (row), "workspace");
  g_free (data->selected_key);
  data->selected_key = g_strdup (key);
  data->selected_workspace = ws;
  gtk_widget_set_sensitive (data->post_button, TRUE);
}
```

- [ ] **Step 4: Replace `populate_results` with grouped version**

Replace `populate_results` (lines 50-76):

```c
static void
populate_results_grouped (JiraDialogData *data, GList *groups)
{
  clear_list_box (GTK_LIST_BOX (data->list_box));

  for (GList *g = groups; g != NULL; g = g->next)
    {
      JiraSearchGroup *group = g->data;

      /* Header row */
      GtkWidget *header_label = gtk_label_new (NULL);
      gchar *markup = g_markup_printf_escaped (
          "<b>%s</b>", group->workspace->label);
      gtk_label_set_markup (GTK_LABEL (header_label), markup);
      g_free (markup);
      gtk_label_set_xalign (GTK_LABEL (header_label), 0.0);
      gtk_widget_set_margin_start (header_label, 6);
      gtk_widget_set_margin_top (header_label, 8);
      gtk_widget_set_margin_bottom (header_label, 2);

      GtkWidget *header_row = gtk_list_box_row_new ();
      g_object_set_data (G_OBJECT (header_row), "is-header",
                         GINT_TO_POINTER (1));
      gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (header_row), FALSE);
      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (header_row), FALSE);
      gtk_container_add (GTK_CONTAINER (header_row), header_label);
      gtk_list_box_insert (GTK_LIST_BOX (data->list_box), header_row, -1);

      /* Issue rows */
      for (GList *l = group->issues; l != NULL; l = l->next)
        {
          JiraIssue *issue = l->data;
          gchar *label_text = g_strdup_printf ("%s  —  %s",
                                                issue->key, issue->summary);
          GtkWidget *label = gtk_label_new (label_text);
          gtk_label_set_xalign (GTK_LABEL (label), 0.0);
          gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
          gtk_widget_set_margin_start (label, 16);
          gtk_widget_set_margin_end (label, 6);
          gtk_widget_set_margin_top (label, 4);
          gtk_widget_set_margin_bottom (label, 4);

          GtkWidget *row = gtk_list_box_row_new ();
          g_object_set_data_full (G_OBJECT (row), "issue-key",
                                  g_strdup (issue->key), g_free);
          g_object_set_data (G_OBJECT (row), "workspace", group->workspace);
          gtk_container_add (GTK_CONTAINER (row), label);
          gtk_list_box_insert (GTK_LIST_BOX (data->list_box), row, -1);
          g_free (label_text);
        }
    }

  gtk_widget_show_all (data->list_box);
}
```

- [ ] **Step 5: Update `do_search` to use `sc_jira_search_all`**

Replace `do_search` (lines 79-99):

```c
static gboolean
do_search (gpointer user_data)
{
  JiraDialogData *data = user_data;
  const gchar *query = gtk_entry_get_text (GTK_ENTRY (data->search_entry));
  GError *error = NULL;

  data->search_timeout_id = 0;

  GList *groups = sc_jira_search_all (&data->config->jira, query, &error);
  if (error)
    {
      g_warning ("Jira search error: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  populate_results_grouped (data, groups);
  sc_jira_search_group_list_free (groups);
  return FALSE;
}
```

- [ ] **Step 6: Update `screenshooter_jira_dialog_run` signature and post call**

Replace the function signature (line 112) and update the dialog data init and the post call:

```c
gboolean
screenshooter_jira_dialog_run (GtkWindow *parent,
                                const CloudConfig *config,
                                const gchar *media_url,
                                gboolean is_video)
```

In the data init, replace:
```c
  data.config = config;
  data.image_url = image_url;
```
with:
```c
  data.config = config;
  data.media_url = media_url;
  data.is_video = is_video;
```

Replace the `sc_jira_post_comment` call (around line 230) with:

```c
      result = sc_jira_post_comment (config->jira.email,
        config->jira.api_token,
        data.selected_workspace,
        data.selected_key,
        preset_title ? preset_title : "Screenshot",
        desc, media_url, is_video, &error);
```

Replace the success dialog's `image_url` references with `media_url`:

```c
          gtk_clipboard_set_text (clip, media_url, -1);

          GtkWidget *ok_dlg = gtk_message_dialog_new (
            GTK_WINDOW (dlg), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Posted to %s!\n\n%s\n\n(URL copied to clipboard)",
            data.selected_key, media_url);
```

- [ ] **Step 7: Commit**

```bash
git add lib/ui-gtk/screenshooter-jira-dialog.h lib/ui-gtk/screenshooter-jira-dialog.c
git commit -m "feat: grouped multi-workspace search results in jira dialog"
```

---

### Task 8: Update Action Handlers — is_video and Workspace Resolution

**Files:**
- Modify: `lib/ui-gtk/screenshooter-actions.c`

- [ ] **Step 1: Add `url_is_video` helper at top of file (after includes)**

```c
static gboolean
url_is_video (const gchar *url)
{
  if (url == NULL)
    return FALSE;
  return (g_str_has_suffix (url, ".mp4") ||
          g_str_has_suffix (url, ".webm") ||
          g_str_has_suffix (url, ".mkv"));
}
```

- [ ] **Step 2: Update screenshot Jira post in `action_idle()` — CLI mode**

Replace the CLI Jira post block (lines 227-257) with:

```c
              if (sd->jira_issue_key && sd->jira_issue_key[0] != '\0')
                {
                  /* CLI mode: resolve workspace for issue key */
                  gboolean is_video = url_is_video (public_url);
                  JiraWorkspace *target_ws = NULL;
                  gchar *issue_key = sd->jira_issue_key;

                  /* Check for workspace:key syntax */
                  gchar *colon = strchr (sd->jira_issue_key, ':');
                  if (colon)
                    {
                      gchar *ws_label = g_strndup (sd->jira_issue_key,
                                                    colon - sd->jira_issue_key);
                      issue_key = colon + 1;
                      for (gsize i = 0; i < cloud_config->jira.n_workspaces; i++)
                        {
                          if (g_strcmp0 (cloud_config->jira.workspaces[i].label,
                                        ws_label) == 0)
                            {
                              target_ws = &cloud_config->jira.workspaces[i];
                              break;
                            }
                        }
                      if (target_ws == NULL)
                        {
                          GtkWidget *warn = gtk_message_dialog_new (NULL,
                            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                            "Unknown workspace: %s", ws_label);
                          gtk_dialog_run (GTK_DIALOG (warn));
                          gtk_widget_destroy (warn);
                        }
                      g_free (ws_label);
                    }
                  else
                    {
                      /* Auto-detect: search each workspace for the key */
                      GPtrArray *matches = g_ptr_array_new ();
                      for (gsize i = 0; i < cloud_config->jira.n_workspaces; i++)
                        {
                          GError *search_err = NULL;
                          GList *results = sc_jira_search (
                            cloud_config->jira.email,
                            cloud_config->jira.api_token,
                            &cloud_config->jira.workspaces[i],
                            issue_key, &search_err);
                          if (results != NULL)
                            {
                              g_ptr_array_add (matches,
                                &cloud_config->jira.workspaces[i]);
                              sc_jira_issue_list_free (results);
                            }
                          g_clear_error (&search_err);
                        }

                      if (matches->len == 1)
                        target_ws = g_ptr_array_index (matches, 0);
                      else if (matches->len == 0)
                        {
                          GtkWidget *warn = gtk_message_dialog_new (NULL,
                            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                            "Issue %s not found on any workspace.", issue_key);
                          gtk_dialog_run (GTK_DIALOG (warn));
                          gtk_widget_destroy (warn);
                        }
                      else
                        {
                          GString *labels = g_string_new ("");
                          for (guint i = 0; i < matches->len; i++)
                            {
                              JiraWorkspace *ws = g_ptr_array_index (matches, i);
                              if (i > 0)
                                g_string_append (labels, ", ");
                              g_string_append (labels, ws->label);
                            }
                          GtkWidget *warn = gtk_message_dialog_new (NULL,
                            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                            "Ambiguous issue %s found on: %s.\n"
                            "Specify workspace: -j workspace:%s",
                            issue_key, labels->str, issue_key);
                          gtk_dialog_run (GTK_DIALOG (warn));
                          gtk_widget_destroy (warn);
                          g_string_free (labels, TRUE);
                        }
                      g_ptr_array_free (matches, TRUE);
                    }

                  if (target_ws)
                    {
                      GError *jira_err = NULL;
                      sc_jira_post_comment (cloud_config->jira.email,
                        cloud_config->jira.api_token, target_ws,
                        issue_key,
                        cloud_config->presets.bug_evidence
                          ? cloud_config->presets.bug_evidence : "Screenshot",
                        "", public_url, is_video, &jira_err);
                      if (jira_err)
                        {
                          GtkWidget *warn = gtk_message_dialog_new (NULL,
                            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                            "Jira post failed: %s", jira_err->message);
                          gtk_dialog_run (GTK_DIALOG (warn));
                          gtk_widget_destroy (warn);
                          g_error_free (jira_err);
                        }
                      else
                        {
                          GtkClipboard *clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
                          gtk_clipboard_set_text (clip, public_url, -1);

                          GtkWidget *info = gtk_message_dialog_new (NULL,
                            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                            "Posted to %s!\n\n%s\n\n(URL copied to clipboard)",
                            issue_key, public_url);
                          gtk_dialog_run (GTK_DIALOG (info));
                          gtk_widget_destroy (info);
                        }
                    }
                }
```

- [ ] **Step 3: Update screenshot Jira post — GUI mode**

Replace the GUI Jira dialog call (lines 258-263) with:

```c
              else
                {
                  gboolean is_video = url_is_video (public_url);
                  screenshooter_jira_dialog_run (NULL, cloud_config,
                                                  public_url, is_video);
                }
```

- [ ] **Step 4: Update recording Jira post in `action_idle_recording()` — CLI mode**

Apply the same CLI workspace resolution pattern to the recording block (lines 427-458). Replace with identical logic as Step 2, but change the default preset label from `"Screenshot"` to `"Recording"`.

- [ ] **Step 5: Update recording Jira post — GUI mode**

Replace the recording GUI dialog call (lines 459-462) with:

```c
              else
                {
                  gboolean is_video = url_is_video (public_url);
                  screenshooter_jira_dialog_run (NULL, cloud_config,
                                                  public_url, is_video);
                }
```

- [ ] **Step 6: Verify compilation**

Run: `cd /home/nst/CProjects/xfce4-screenshooter-master && meson setup builddir --wipe 2>&1 | tail -5 && ninja -C builddir 2>&1 | tail -20`

Expected: Build succeeds with no errors.

- [ ] **Step 7: Commit**

```bash
git add lib/ui-gtk/screenshooter-actions.c
git commit -m "feat: workspace resolution and video link in action handlers"
```

---

### Task 9: Update Setup Wizard — Workspace List UI

**Files:**
- Modify: `lib/ui-gtk/screenshooter-wizard.c`

- [ ] **Step 1: Update WizardData struct**

Replace the Jira entries section (lines 43-47):

```c
  /* Jira credentials */
  GtkWidget *jira_email;
  GtkWidget *jira_api_token;
  GtkWidget *jira_result_label;

  /* Jira workspaces */
  GtkWidget *jira_ws_list_box;
  GPtrArray *jira_ws_entries; /* array of { GtkWidget *label_entry, *url_entry, *project_entry } */
```

Add a workspace entry struct after `WizardData`:

```c
typedef struct {
  GtkWidget *label_entry;
  GtkWidget *url_entry;
  GtkWidget *project_entry;
  GtkWidget *row;
} WizardWorkspaceEntry;
```

- [ ] **Step 2: Update `has_jira_config()` for workspace awareness**

Replace `has_jira_config` (lines 70-80):

```c
static gboolean
has_jira_config (WizardData *wd)
{
  const gchar *email     = gtk_entry_get_text (GTK_ENTRY (wd->jira_email));
  const gchar *api_token = gtk_entry_get_text (GTK_ENTRY (wd->jira_api_token));

  if (!email || !*email || !api_token || !*api_token)
    return FALSE;

  if (wd->jira_ws_entries->len == 0)
    return FALSE;

  for (guint i = 0; i < wd->jira_ws_entries->len; i++)
    {
      WizardWorkspaceEntry *e = g_ptr_array_index (wd->jira_ws_entries, i);
      const gchar *label = gtk_entry_get_text (GTK_ENTRY (e->label_entry));
      const gchar *url   = gtk_entry_get_text (GTK_ENTRY (e->url_entry));
      if (!label || !*label || !url || !*url)
        return FALSE;
    }

  return TRUE;
}
```

- [ ] **Step 3: Update `build_config_from_entries()` for workspace array**

Replace the jira section in `build_config_from_entries` (lines 101-109):

```c
  g_free (config->jira.email);
  g_free (config->jira.api_token);

  config->jira.email     = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->jira_email)));
  config->jira.api_token = g_strdup (gtk_entry_get_text (GTK_ENTRY (wd->jira_api_token)));

  config->jira.n_workspaces = wd->jira_ws_entries->len;
  config->jira.workspaces = g_new0 (JiraWorkspace, wd->jira_ws_entries->len);
  for (guint i = 0; i < wd->jira_ws_entries->len; i++)
    {
      WizardWorkspaceEntry *e = g_ptr_array_index (wd->jira_ws_entries, i);
      config->jira.workspaces[i].label =
        g_strdup (gtk_entry_get_text (GTK_ENTRY (e->label_entry)));
      config->jira.workspaces[i].base_url =
        g_strdup (gtk_entry_get_text (GTK_ENTRY (e->url_entry)));
      config->jira.workspaces[i].default_project =
        g_strdup (gtk_entry_get_text (GTK_ENTRY (e->project_entry)));
    }
```

- [ ] **Step 4: Update `cb_jira_test_connection()`**

Replace the function (lines 165-191):

```c
static void
cb_jira_test_connection (GtkButton *button, WizardData *wd)
{
  const gchar *email = gtk_entry_get_text (GTK_ENTRY (wd->jira_email));
  const gchar *api_token = gtk_entry_get_text (GTK_ENTRY (wd->jira_api_token));
  GError *error = NULL;
  gboolean success = FALSE;

  if (wd->jira_ws_entries->len > 0)
    {
      WizardWorkspaceEntry *e = g_ptr_array_index (wd->jira_ws_entries, 0);
      JiraWorkspace ws = {
        .label = (gchar *) gtk_entry_get_text (GTK_ENTRY (e->label_entry)),
        .base_url = (gchar *) gtk_entry_get_text (GTK_ENTRY (e->url_entry)),
        .default_project = (gchar *) gtk_entry_get_text (GTK_ENTRY (e->project_entry)),
      };
      success = sc_jira_test_connection (email, api_token, &ws, &error);
    }
  else
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Add at least one workspace first");
    }

  if (success)
    {
      gtk_label_set_markup (GTK_LABEL (wd->jira_result_label),
                            "<span foreground=\"green\">Connected!</span>");
    }
  else
    {
      gchar *markup = g_markup_printf_escaped (
          "<span foreground=\"red\">%s</span>",
          error ? error->message : _("Unknown error"));
      gtk_label_set_markup (GTK_LABEL (wd->jira_result_label), markup);
      g_free (markup);
      g_clear_error (&error);
    }
}
```

- [ ] **Step 5: Add workspace add/remove helpers**

Add before `create_jira_page`:

```c
static void
add_workspace_row (WizardData *wd, const gchar *label,
                   const gchar *url, const gchar *project)
{
  WizardWorkspaceEntry *e = g_new0 (WizardWorkspaceEntry, 1);

  GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  e->label_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (e->label_entry), "label");
  gtk_entry_set_width_chars (GTK_ENTRY (e->label_entry), 10);
  if (label)
    gtk_entry_set_text (GTK_ENTRY (e->label_entry), label);

  e->url_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (e->url_entry), "https://team.atlassian.net");
  gtk_widget_set_hexpand (e->url_entry, TRUE);
  if (url)
    gtk_entry_set_text (GTK_ENTRY (e->url_entry), url);

  e->project_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (e->project_entry), "PROJ");
  gtk_entry_set_width_chars (GTK_ENTRY (e->project_entry), 8);
  if (project)
    gtk_entry_set_text (GTK_ENTRY (e->project_entry), project);

  gtk_box_pack_start (GTK_BOX (hbox), e->label_entry, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), e->url_entry, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), e->project_entry, FALSE, FALSE, 0);

  e->row = gtk_list_box_row_new ();
  gtk_container_add (GTK_CONTAINER (e->row), hbox);
  gtk_list_box_insert (GTK_LIST_BOX (wd->jira_ws_list_box), e->row, -1);
  gtk_widget_show_all (e->row);

  g_ptr_array_add (wd->jira_ws_entries, e);
}


static void
cb_add_workspace (GtkButton *button, WizardData *wd)
{
  add_workspace_row (wd, NULL, NULL, NULL);
}


static void
cb_remove_workspace (GtkButton *button, WizardData *wd)
{
  if (wd->jira_ws_entries->len <= 1)
    return;

  GtkListBoxRow *selected = gtk_list_box_get_selected_row (
    GTK_LIST_BOX (wd->jira_ws_list_box));
  if (selected == NULL)
    return;

  for (guint i = 0; i < wd->jira_ws_entries->len; i++)
    {
      WizardWorkspaceEntry *e = g_ptr_array_index (wd->jira_ws_entries, i);
      if (e->row == GTK_WIDGET (selected))
        {
          gtk_widget_destroy (e->row);
          g_free (e);
          g_ptr_array_remove_index (wd->jira_ws_entries, i);
          break;
        }
    }
}
```

- [ ] **Step 6: Replace `create_jira_page`**

Replace the function (lines 348-378):

```c
static GtkWidget *
create_jira_page (WizardData *wd)
{
  GtkWidget *box, *grid, *label, *scrolled, *btn_box, *add_btn, *rm_btn;
  GtkWidget *test_button;
  gint row = 0;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);

  /* Credentials section */
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), "<b>Credentials</b>");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

  add_grid_entry (GTK_GRID (grid), row++, _("Email:"),     &wd->jira_email,     TRUE);
  add_grid_entry (GTK_GRID (grid), row++, _("API Token:"), &wd->jira_api_token, FALSE);

  test_button = gtk_button_new_with_label (_("Test Connection"));
  gtk_grid_attach (GTK_GRID (grid), test_button, 0, row, 1, 1);

  wd->jira_result_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (wd->jira_result_label), 0.0);
  gtk_grid_attach (GTK_GRID (grid), wd->jira_result_label, 1, row, 1, 1);

  g_signal_connect (test_button, "clicked",
                    G_CALLBACK (cb_jira_test_connection), wd);

  /* Workspaces section */
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), "<b>Workspaces</b>");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_margin_top (label, 8);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height (
    GTK_SCROLLED_WINDOW (scrolled), 120);
  gtk_widget_set_vexpand (scrolled, TRUE);
  gtk_box_pack_start (GTK_BOX (box), scrolled, TRUE, TRUE, 0);

  wd->jira_ws_list_box = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (wd->jira_ws_list_box),
    GTK_SELECTION_SINGLE);
  gtk_container_add (GTK_CONTAINER (scrolled), wd->jira_ws_list_box);

  btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (box), btn_box, FALSE, FALSE, 0);

  add_btn = gtk_button_new_with_label (_("Add"));
  rm_btn = gtk_button_new_with_label (_("Remove"));
  gtk_box_pack_start (GTK_BOX (btn_box), add_btn, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (btn_box), rm_btn, FALSE, FALSE, 0);

  g_signal_connect (add_btn, "clicked", G_CALLBACK (cb_add_workspace), wd);
  g_signal_connect (rm_btn, "clicked", G_CALLBACK (cb_remove_workspace), wd);

  return box;
}
```

- [ ] **Step 7: Initialize `jira_ws_entries` and populate from existing config**

In `screenshooter_wizard_run()`, after `wd.loop = g_main_loop_new (...)`, add:

```c
  wd.jira_ws_entries = g_ptr_array_new ();
```

After `gtk_widget_show_all (GTK_WIDGET (wd.assistant));`, add pre-population from existing config:

```c
  /* Pre-populate from existing config if available */
  {
    GError *load_err = NULL;
    CloudConfig *existing = sc_cloud_config_load (config_dir, &load_err);
    if (existing)
      {
        if (existing->jira.email[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.jira_email), existing->jira.email);
        if (existing->jira.api_token[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.jira_api_token), existing->jira.api_token);

        for (gsize i = 0; i < existing->jira.n_workspaces; i++)
          add_workspace_row (&wd, existing->jira.workspaces[i].label,
                             existing->jira.workspaces[i].base_url,
                             existing->jira.workspaces[i].default_project);

        if (existing->r2.account_id[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_account_id), existing->r2.account_id);
        if (existing->r2.access_key_id[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_access_key_id), existing->r2.access_key_id);
        if (existing->r2.secret_access_key[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_secret_access_key), existing->r2.secret_access_key);
        if (existing->r2.bucket[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_bucket), existing->r2.bucket);
        if (existing->r2.public_url[0] != '\0')
          gtk_entry_set_text (GTK_ENTRY (wd.r2_public_url), existing->r2.public_url);

        sc_cloud_config_free (existing);
      }
    g_clear_error (&load_err);
  }

  /* Ensure at least one workspace row */
  if (wd.jira_ws_entries->len == 0)
    add_workspace_row (&wd, NULL, NULL, NULL);
```

After `g_main_loop_unref (wd.loop);`, add cleanup:

```c
  for (guint i = 0; i < wd.jira_ws_entries->len; i++)
    g_free (g_ptr_array_index (wd.jira_ws_entries, i));
  g_ptr_array_free (wd.jira_ws_entries, TRUE);
```

- [ ] **Step 8: Verify compilation**

Run: `ninja -C builddir 2>&1 | tail -20`

Expected: Build succeeds.

- [ ] **Step 9: Commit**

```bash
git add lib/ui-gtk/screenshooter-wizard.c
git commit -m "feat: multi-workspace management in setup wizard"
```

---

### Task 10: Update Reconfigure Dialog Cloud Button

**Files:**
- Modify: `lib/ui-gtk/screenshooter-dialogs.c`

The "Reconfigure Cloud Services" button and any direct references to `config->jira.base_url` or `config->jira.default_project` for tooltip/sensitivity checks need updating.

- [ ] **Step 1: Find and update Jira config validation references**

Search `screenshooter-dialogs.c` for references to `config->jira.base_url`, `config->jira.default_project`, or `sc_cloud_config_valid_jira`. These control whether the "Post to Jira" radio button is sensitive. The validation function was already updated in Task 1 to check `n_workspaces >= 1` instead of `base_url`, so the existing `sc_cloud_config_valid_jira()` calls should work. Verify there are no direct field accesses to `config->jira.base_url` in this file.

If there are direct accesses, replace them with `sc_cloud_config_valid_jira(config)` calls.

- [ ] **Step 2: Verify compilation**

Run: `ninja -C builddir 2>&1 | tail -20`

Expected: Build succeeds.

- [ ] **Step 3: Commit (if changes were needed)**

```bash
git add lib/ui-gtk/screenshooter-dialogs.c
git commit -m "fix: update dialog jira config references for multi-workspace"
```

---

### Task 11: Final Build Verification and Cleanup

**Files:**
- All modified files

- [ ] **Step 1: Full clean build**

Run: `cd /home/nst/CProjects/xfce4-screenshooter-master && meson setup builddir --wipe 2>&1 | tail -10 && ninja -C builddir 2>&1 | tail -20`

Expected: Build succeeds with zero errors.

- [ ] **Step 2: Run all config tests**

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-multi-workspace-config.c -o /tmp/test-multi-ws && /tmp/test-multi-ws`

Expected: All tests PASS.

Run: `cc $(pkg-config --cflags --libs glib-2.0 gio-2.0) -Ilib/core -Ilib/platform lib/core/sc-cloud-config.c lib/platform/sc-platform-linux.c test/test-cloud-config-save.c -o /tmp/test-cc-save && /tmp/test-cc-save`

Expected: All tests PASS.

- [ ] **Step 3: Commit any remaining fixes**

If any compilation issues were found and fixed, commit them:

```bash
git add -u
git commit -m "fix: resolve multi-workspace build issues"
```
