# Jira + R2 Integration Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add native Cloudflare R2 upload and Jira issue commenting to xfce4-screenshooter, ported from jira-proofs (Rust/Tauri) into C/GTK3.

**Architecture:** Extend the existing action bitmask system with two new flags (UPLOAD_R2=16, POST_JIRA=32). Add four new source files to lib/ for cloud config parsing, R2 upload via libcurl+AWS SigV4, Jira REST API integration, and a GTK issue search dialog. The new actions compose with existing ones (save, clipboard, open, custom).

**Tech Stack:** C (gnu11), GTK3, libcurl (HTTP + S3 signing), json-glib (Jira API), GLib (HMAC-SHA256, base64), Meson build system.

**Spec:** `docs/superpowers/specs/2026-03-11-jira-r2-integration-design.md`

**Reference implementation:** `/home/nst/GolandProjects/jira-proofs/src-tauri/src/` (config.rs, r2.rs, jira.rs)

---

## Chunk 1: Build System + Config

### Task 1: Add libcurl and json-glib dependencies to Meson

**Files:**
- Modify: `meson.build:32-42` (add dependency declarations)
- Modify: `lib/meson.build:55-66` (add to library dependencies)

- [ ] **Step 1: Add dependency declarations to root meson.build**

In `meson.build`, after the xfconf dependency line (line 41), add:

```c
libcurl = dependency('libcurl', version: '>= 7.68.0')
json_glib = dependency('json-glib-1.0', version: '>= 1.4.0')
```

- [ ] **Step 2: Add dependencies to lib/meson.build**

In `lib/meson.build`, add `libcurl` and `json_glib` to the `dependencies` list in the `static_library` call (after line 65, before the closing bracket):

```meson
    libcurl,
    json_glib,
```

- [ ] **Step 3: Verify the build still configures**

Run: `meson setup build --wipe`
Expected: Configuration succeeds, libcurl and json-glib found.

- [ ] **Step 4: Commit**

```bash
git add meson.build lib/meson.build
git commit -m "build: add libcurl and json-glib dependencies for cloud integration"
```

---

### Task 2: Cloud config TOML parser

**Files:**
- Create: `lib/screenshooter-cloud-config.h`
- Create: `lib/screenshooter-cloud-config.c`
- Modify: `lib/meson.build:1-18` (add new sources)

- [ ] **Step 1: Create the header file**

Create `lib/screenshooter-cloud-config.h`:

```c
#ifndef __SCREENSHOOTER_CLOUD_CONFIG_H__
#define __SCREENSHOOTER_CLOUD_CONFIG_H__

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

CloudConfig *screenshooter_cloud_config_load   (GError **error);
void         screenshooter_cloud_config_free   (CloudConfig *config);
gboolean     screenshooter_cloud_config_valid_r2   (const CloudConfig *config);
gboolean     screenshooter_cloud_config_valid_jira (const CloudConfig *config);
gchar       *screenshooter_cloud_config_get_path   (void);
void         screenshooter_cloud_config_create_default (const gchar *path,
                                                         GError **error);

#endif
```

- [ ] **Step 2: Create the implementation file**

Create `lib/screenshooter-cloud-config.c`:

```c
#include "screenshooter-cloud-config.h"

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <sys/stat.h>


static gchar *
cloud_config_dir (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "xfce4-screenshooter", NULL);
}


gchar *
screenshooter_cloud_config_get_path (void)
{
  gchar *dir = cloud_config_dir ();
  gchar *path = g_build_filename (dir, "cloud.toml", NULL);
  g_free (dir);
  return path;
}


/* Minimal TOML parser: handles [section] headers and key = "value" pairs.
 * Only supports string values in double quotes. Sufficient for our config. */
static GHashTable *
parse_toml_section (GKeyFile *kf, const gchar *section)
{
  GHashTable *ht = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_free);
  gchar **keys = g_key_file_get_keys (kf, section, NULL, NULL);
  if (keys == NULL)
    return ht;

  for (gint i = 0; keys[i] != NULL; i++)
    {
      gchar *val = g_key_file_get_string (kf, section, keys[i], NULL);
      if (val)
        g_hash_table_insert (ht, g_strdup (keys[i]), val);
    }
  g_strfreev (keys);
  return ht;
}


static gchar *
strip_toml_quotes (gchar *str)
{
  if (str == NULL)
    return str;
  gsize len = strlen (str);
  if (len >= 2 && str[0] == '"' && str[len - 1] == '"')
    {
      gchar *stripped = g_strndup (str + 1, len - 2);
      g_free (str);
      return stripped;
    }
  return str;
}


static gchar *
ht_take_string (GHashTable *ht, const gchar *key)
{
  gchar *val = g_hash_table_lookup (ht, key);
  if (val)
    {
      val = g_strdup (val);
      return strip_toml_quotes (val);
    }
  return g_strdup ("");
}


CloudConfig *
screenshooter_cloud_config_load (GError **error)
{
  gchar *path = screenshooter_cloud_config_get_path ();
  CloudConfig *config;
  GKeyFile *kf;
  GHashTable *ht;

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Cloud config not found: %s", path);
      g_free (path);
      return NULL;
    }

  kf = g_key_file_new ();
  if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, error))
    {
      g_key_file_free (kf);
      g_free (path);
      return NULL;
    }

  config = g_new0 (CloudConfig, 1);

  /* Parse [jira] section */
  ht = parse_toml_section (kf, "jira");
  config->jira.base_url = ht_take_string (ht, "base_url");
  config->jira.email = ht_take_string (ht, "email");
  config->jira.api_token = ht_take_string (ht, "api_token");
  config->jira.default_project = ht_take_string (ht, "default_project");
  g_hash_table_destroy (ht);

  /* Parse [r2] section */
  ht = parse_toml_section (kf, "r2");
  config->r2.account_id = ht_take_string (ht, "account_id");
  config->r2.access_key_id = ht_take_string (ht, "access_key_id");
  config->r2.secret_access_key = ht_take_string (ht, "secret_access_key");
  config->r2.bucket = ht_take_string (ht, "bucket");
  config->r2.public_url = ht_take_string (ht, "public_url");
  g_hash_table_destroy (ht);

  /* Parse [presets] section */
  ht = parse_toml_section (kf, "presets");
  config->presets.bug_evidence = ht_take_string (ht, "bug_evidence");
  config->presets.work_evidence = ht_take_string (ht, "work_evidence");
  g_hash_table_destroy (ht);

  config->loaded = TRUE;

  g_key_file_free (kf);
  g_free (path);
  return config;
}


void
screenshooter_cloud_config_free (CloudConfig *config)
{
  if (config == NULL)
    return;

  g_free (config->jira.base_url);
  g_free (config->jira.email);
  g_free (config->jira.api_token);
  g_free (config->jira.default_project);

  g_free (config->r2.account_id);
  g_free (config->r2.access_key_id);
  g_free (config->r2.secret_access_key);
  g_free (config->r2.bucket);
  g_free (config->r2.public_url);

  g_free (config->presets.bug_evidence);
  g_free (config->presets.work_evidence);

  g_free (config);
}


gboolean
screenshooter_cloud_config_valid_r2 (const CloudConfig *config)
{
  if (config == NULL || !config->loaded)
    return FALSE;
  return (config->r2.account_id[0] != '\0' &&
          config->r2.access_key_id[0] != '\0' &&
          config->r2.secret_access_key[0] != '\0' &&
          config->r2.bucket[0] != '\0' &&
          config->r2.public_url[0] != '\0');
}


gboolean
screenshooter_cloud_config_valid_jira (const CloudConfig *config)
{
  if (config == NULL || !config->loaded)
    return FALSE;
  return (config->jira.base_url[0] != '\0' &&
          config->jira.email[0] != '\0' &&
          config->jira.api_token[0] != '\0' &&
          config->jira.default_project[0] != '\0');
}


void
screenshooter_cloud_config_create_default (const gchar *path,
                                            GError **error)
{
  gchar *dir;
  const gchar *content =
    "[jira]\n"
    "base_url = \"https://yourteam.atlassian.net\"\n"
    "email = \"you@example.com\"\n"
    "api_token = \"your-jira-api-token\"\n"
    "default_project = \"PROJ\"\n"
    "\n"
    "[r2]\n"
    "account_id = \"your-cf-account-id\"\n"
    "access_key_id = \"your-r2-access-key\"\n"
    "secret_access_key = \"your-r2-secret\"\n"
    "bucket = \"screenshots\"\n"
    "public_url = \"https://assets.yourdomain.com\"\n"
    "\n"
    "[presets]\n"
    "bug_evidence = \"Bug Evidence\"\n"
    "work_evidence = \"Work Evidence\"\n";

  dir = g_path_get_dirname (path);
  g_mkdir_with_parents (dir, 0700);
  g_free (dir);

  if (!g_file_set_contents (path, content, -1, error))
    return;

  chmod (path, 0600);
}
```

- [ ] **Step 3: Add new sources to lib/meson.build**

In `lib/meson.build`, add to `libscreenshooter_sources` list (after line 17 `'screenshooter-utils.h',`):

```meson
  'screenshooter-cloud-config.c',
  'screenshooter-cloud-config.h',
```

- [ ] **Step 4: Build and verify**

Run: `meson compile -C build`
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add lib/screenshooter-cloud-config.c lib/screenshooter-cloud-config.h lib/meson.build
git commit -m "feat: add cloud config TOML parser for R2 and Jira settings"
```

---

## Chunk 2: R2 Upload Module

### Task 3: R2 upload with AWS Signature V4

**Files:**
- Create: `lib/screenshooter-r2.h`
- Create: `lib/screenshooter-r2.c`
- Modify: `lib/meson.build` (add new sources)

- [ ] **Step 1: Create the header file**

Create `lib/screenshooter-r2.h`:

```c
#ifndef __SCREENSHOOTER_R2_H__
#define __SCREENSHOOTER_R2_H__

#include <glib.h>
#include "screenshooter-cloud-config.h"

typedef void (*R2ProgressCallback) (gdouble fraction, gpointer user_data);

gchar *screenshooter_r2_upload (const CloudConfig *config,
                                 const gchar *file_path,
                                 R2ProgressCallback progress_cb,
                                 gpointer progress_data,
                                 GError **error);

gchar *screenshooter_r2_build_object_key (const gchar *filename);
const gchar *screenshooter_r2_content_type (const gchar *extension);

#endif
```

- [ ] **Step 2: Create the implementation file**

Create `lib/screenshooter-r2.c`. This is the largest file — implements AWS Signature V4 signing and S3 PUT via libcurl.

```c
#include "screenshooter-r2.h"

#include <curl/curl.h>
#include <glib.h>
#include <string.h>
#include <time.h>


const gchar *
screenshooter_r2_content_type (const gchar *extension)
{
  if (g_strcmp0 (extension, "png") == 0) return "image/png";
  if (g_strcmp0 (extension, "jpg") == 0 || g_strcmp0 (extension, "jpeg") == 0) return "image/jpeg";
  if (g_strcmp0 (extension, "bmp") == 0) return "image/bmp";
  if (g_strcmp0 (extension, "webp") == 0) return "image/webp";
  if (g_strcmp0 (extension, "jxl") == 0) return "image/jxl";
  if (g_strcmp0 (extension, "avif") == 0) return "image/avif";
  if (g_strcmp0 (extension, "gif") == 0) return "image/gif";
  if (g_strcmp0 (extension, "mp4") == 0) return "video/mp4";
  if (g_strcmp0 (extension, "webm") == 0) return "video/webm";
  return "application/octet-stream";
}


gchar *
screenshooter_r2_build_object_key (const gchar *filename)
{
  GDateTime *now = g_date_time_new_now_local ();
  gchar *date = g_date_time_format (now, "%Y-%m-%d");
  gchar *key = g_strdup_printf ("captures/%s/%s", date, filename);
  g_free (date);
  g_date_time_unref (now);
  return key;
}


/* AWS Signature V4 helper functions */

static gchar *
hmac_sha256_hex (const guchar *key, gsize key_len,
                 const gchar *data, gsize data_len)
{
  GHmac *hmac = g_hmac_new (G_CHECKSUM_SHA256, key, key_len);
  g_hmac_update (hmac, (const guchar *) data, data_len);
  gchar *hex = g_strdup (g_hmac_get_string (hmac));
  g_hmac_unref (hmac);
  return hex;
}


static guchar *
hmac_sha256_raw (const guchar *key, gsize key_len,
                 const gchar *data, gsize data_len,
                 gsize *out_len)
{
  GHmac *hmac = g_hmac_new (G_CHECKSUM_SHA256, key, key_len);
  g_hmac_update (hmac, (const guchar *) data, data_len);
  guchar *digest = g_malloc (32);
  *out_len = 32;
  g_hmac_get_digest (hmac, digest, out_len);
  g_hmac_unref (hmac);
  return digest;
}


static gchar *
sha256_hex (const guchar *data, gsize len)
{
  GChecksum *cs = g_checksum_new (G_CHECKSUM_SHA256);
  g_checksum_update (cs, data, len);
  gchar *hex = g_strdup (g_checksum_get_string (cs));
  g_checksum_free (cs);
  return hex;
}


static guchar *
derive_signing_key (const gchar *secret_key, const gchar *date_stamp,
                    const gchar *region, const gchar *service,
                    gsize *out_len)
{
  gchar *k_secret = g_strdup_printf ("AWS4%s", secret_key);
  gsize klen;
  guchar *k_date = hmac_sha256_raw ((const guchar *) k_secret,
                                     strlen (k_secret),
                                     date_stamp, strlen (date_stamp), &klen);
  g_free (k_secret);
  guchar *k_region = hmac_sha256_raw (k_date, klen, region,
                                       strlen (region), &klen);
  g_free (k_date);
  guchar *k_service = hmac_sha256_raw (k_region, klen, service,
                                        strlen (service), &klen);
  g_free (k_region);
  guchar *k_signing = hmac_sha256_raw (k_service, klen, "aws4_request",
                                        12, out_len);
  g_free (k_service);
  return k_signing;
}


typedef struct {
  R2ProgressCallback cb;
  gpointer data;
} ProgressCtx;


static int
curl_progress_func (void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t ultotal, curl_off_t ulnow)
{
  ProgressCtx *ctx = clientp;
  if (ctx->cb && ultotal > 0)
    ctx->cb ((gdouble) ulnow / (gdouble) ultotal, ctx->data);
  return 0;
}


gchar *
screenshooter_r2_upload (const CloudConfig *config,
                          const gchar *file_path,
                          R2ProgressCallback progress_cb,
                          gpointer progress_data,
                          GError **error)
{
  CURL *curl;
  CURLcode res;
  gchar *file_contents = NULL;
  gsize file_len;
  gchar *filename, *extension, *object_key, *content_type_str;
  gchar *host, *url;
  gchar *payload_hash;
  GDateTime *now;
  gchar *amz_date, *date_stamp;
  gchar *canonical_request, *canonical_headers, *signed_headers;
  gchar *credential_scope, *string_to_sign, *canonical_request_hash;
  guchar *signing_key;
  gsize signing_key_len;
  gchar *signature;
  gchar *auth_header;
  gchar *h_sha = NULL, *h_date = NULL, *h_ct = NULL;
  struct curl_slist *headers = NULL;
  long http_code;
  gchar *public_url;
  ProgressCtx progress_ctx;

  if (!screenshooter_cloud_config_valid_r2 (config))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "R2 configuration is incomplete");
      return NULL;
    }

  if (!g_file_get_contents (file_path, &file_contents, &file_len, error))
    return NULL;

  filename = g_path_get_basename (file_path);
  extension = g_strrstr (filename, ".");
  extension = extension ? extension + 1 : "bin";
  content_type_str = g_strdup (screenshooter_r2_content_type (extension));
  object_key = screenshooter_r2_build_object_key (filename);

  host = g_strdup_printf ("%s.r2.cloudflarestorage.com",
                           config->r2.account_id);
  url = g_strdup_printf ("https://%s/%s/%s", host,
                          config->r2.bucket, object_key);

  /* Compute payload hash */
  payload_hash = sha256_hex ((const guchar *) file_contents, file_len);

  /* Timestamps */
  now = g_date_time_new_now_utc ();
  amz_date = g_date_time_format (now, "%Y%m%dT%H%M%SZ");
  date_stamp = g_date_time_format (now, "%Y%m%d");
  g_date_time_unref (now);

  /* Canonical request */
  gchar *encoded_key = g_uri_escape_string (object_key, "/", FALSE);
  signed_headers = g_strdup ("content-type;host;x-amz-content-sha256;x-amz-date");
  canonical_headers = g_strdup_printf (
    "content-type:%s\nhost:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
    content_type_str, host, payload_hash, amz_date);
  canonical_request = g_strdup_printf (
    "PUT\n/%s/%s\n\n%s\n%s\n%s",
    config->r2.bucket, encoded_key,
    canonical_headers, signed_headers, payload_hash);

  /* String to sign */
  credential_scope = g_strdup_printf ("%s/auto/s3/aws4_request", date_stamp);
  canonical_request_hash = sha256_hex (
    (const guchar *) canonical_request, strlen (canonical_request));
  string_to_sign = g_strdup_printf (
    "AWS4-HMAC-SHA256\n%s\n%s\n%s",
    amz_date, credential_scope, canonical_request_hash);

  /* Signing key and signature */
  signing_key = derive_signing_key (config->r2.secret_access_key,
                                     date_stamp, "auto", "s3",
                                     &signing_key_len);
  signature = hmac_sha256_hex (signing_key, signing_key_len,
                                string_to_sign, strlen (string_to_sign));

  /* Authorization header */
  auth_header = g_strdup_printf (
    "Authorization: AWS4-HMAC-SHA256 Credential=%s/%s, "
    "SignedHeaders=%s, Signature=%s",
    config->r2.access_key_id, credential_scope,
    signed_headers, signature);

  /* CURL request */
  curl = curl_easy_init ();
  if (curl == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to initialize curl");
      /* cleanup below will handle frees */
      public_url = NULL;
      goto cleanup;
    }

  gchar *h_sha = g_strdup_printf ("x-amz-content-sha256: %s", payload_hash);
  gchar *h_date = g_strdup_printf ("x-amz-date: %s", amz_date);
  gchar *h_ct = g_strdup_printf ("Content-Type: %s", content_type_str);
  headers = curl_slist_append (headers, auth_header);
  headers = curl_slist_append (headers, h_sha);
  headers = curl_slist_append (headers, h_date);
  headers = curl_slist_append (headers, h_ct);

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt (curl, CURLOPT_POSTFIELDS, file_contents);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, (long) file_len);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 10L);

  if (progress_cb)
    {
      progress_ctx.cb = progress_cb;
      progress_ctx.data = progress_data;
      curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION, curl_progress_func);
      curl_easy_setopt (curl, CURLOPT_XFERINFODATA, &progress_ctx);
      curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);
    }

  res = curl_easy_perform (curl);
  if (res != CURLE_OK)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "R2 upload failed: %s", curl_easy_strerror (res));
      public_url = NULL;
    }
  else
    {
      curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
      if (http_code >= 200 && http_code < 300)
        {
          gchar *trimmed_base = g_strchomp (g_strdup (config->r2.public_url));
          /* Remove trailing slash */
          gsize len = strlen (trimmed_base);
          if (len > 0 && trimmed_base[len - 1] == '/')
            trimmed_base[len - 1] = '\0';
          public_url = g_strdup_printf ("%s/%s", trimmed_base, object_key);
          g_free (trimmed_base);
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "R2 upload failed with HTTP %ld", http_code);
          public_url = NULL;
        }
    }

  curl_easy_cleanup (curl);

cleanup:
  curl_slist_free_all (headers);
  g_free (h_sha);
  g_free (h_date);
  g_free (h_ct);
  g_free (file_contents);
  g_free (filename);
  g_free (content_type_str);
  g_free (object_key);
  g_free (host);
  g_free (url);
  g_free (payload_hash);
  g_free (amz_date);
  g_free (date_stamp);
  g_free (encoded_key);
  g_free (signed_headers);
  g_free (canonical_headers);
  g_free (canonical_request);
  g_free (credential_scope);
  g_free (canonical_request_hash);
  g_free (string_to_sign);
  g_free (signing_key);
  g_free (signature);
  g_free (auth_header);

  return public_url;
}
```

- [ ] **Step 3: Add new sources to lib/meson.build**

Add after the cloud-config entries:

```meson
  'screenshooter-r2.c',
  'screenshooter-r2.h',
```

- [ ] **Step 4: Build and verify**

Run: `meson compile -C build`
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add lib/screenshooter-r2.c lib/screenshooter-r2.h lib/meson.build
git commit -m "feat: add R2 upload module with AWS Signature V4 signing"
```

---

## Chunk 3: Jira Integration Module

### Task 4: Jira REST API client

**Files:**
- Create: `lib/screenshooter-jira.h`
- Create: `lib/screenshooter-jira.c`
- Modify: `lib/meson.build` (add new sources)

- [ ] **Step 1: Create the header file**

Create `lib/screenshooter-jira.h`:

```c
#ifndef __SCREENSHOOTER_JIRA_H__
#define __SCREENSHOOTER_JIRA_H__

#include <glib.h>
#include "screenshooter-cloud-config.h"

typedef struct {
  gchar *key;
  gchar *summary;
} JiraIssue;

GList    *screenshooter_jira_search       (const CloudConfig *config,
                                            const gchar *query,
                                            GError **error);
gboolean  screenshooter_jira_post_comment (const CloudConfig *config,
                                            const gchar *issue_key,
                                            const gchar *preset_title,
                                            const gchar *description,
                                            const gchar *image_url,
                                            GError **error);
void      screenshooter_jira_issue_free   (JiraIssue *issue);
void      screenshooter_jira_issue_list_free (GList *issues);

#endif
```

- [ ] **Step 2: Create the implementation file**

Create `lib/screenshooter-jira.c`:

```c
#include "screenshooter-jira.h"

#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <string.h>


typedef struct {
  gchar *data;
  gsize len;
} CurlBuffer;


static size_t
write_callback (void *contents, size_t size, size_t nmemb, void *userp)
{
  CurlBuffer *buf = userp;
  gsize realsize = size * nmemb;
  buf->data = g_realloc (buf->data, buf->len + realsize + 1);
  memcpy (buf->data + buf->len, contents, realsize);
  buf->len += realsize;
  buf->data[buf->len] = '\0';
  return realsize;
}


static gchar *
build_auth_header (const gchar *email, const gchar *api_token)
{
  gchar *credentials = g_strdup_printf ("%s:%s", email, api_token);
  gchar *encoded = g_base64_encode ((const guchar *) credentials,
                                     strlen (credentials));
  gchar *header = g_strdup_printf ("Authorization: Basic %s", encoded);
  g_free (credentials);
  g_free (encoded);
  return header;
}


GList *
screenshooter_jira_search (const CloudConfig *config,
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

  if (!screenshooter_cloud_config_valid_jira (config))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Jira configuration is incomplete");
      return NULL;
    }

  if (query == NULL || query[0] == '\0')
    jql = g_strdup_printf (
      "project = %s AND status != Done ORDER BY updated DESC",
      config->jira.default_project);
  else
    {
      gchar *safe = g_strescape (query, NULL);
      jql = g_strdup_printf (
        "project = %s AND summary ~ \\\"%s\\\" AND status != Done "
        "ORDER BY updated DESC",
        config->jira.default_project, safe);
      g_free (safe);
    }

  /* Build JSON body */
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

  auth = build_auth_header (config->jira.email, config->jira.api_token);
  url = g_strdup_printf ("%s/rest/api/3/search", config->jira.base_url);

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

  /* Parse response */
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


static gchar *
build_adf_comment_json (const gchar *preset_title,
                         const gchar *description,
                         const gchar *image_url)
{
  JsonBuilder *b = json_builder_new ();

  /* { "body": { "version": 1, "type": "doc", "content": [ ... ] } } */
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

  /* Embedded image (mediaSingle with external media) */
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
  json_builder_add_string_value (b, image_url);
  json_builder_end_object (b);
  json_builder_end_object (b);
  json_builder_end_array (b);
  json_builder_end_object (b);

  json_builder_end_array (b);  /* content */
  json_builder_end_object (b); /* body */
  json_builder_end_object (b); /* root */

  JsonGenerator *gen = json_generator_new ();
  JsonNode *root = json_builder_get_root (b);
  json_generator_set_root (gen, root);
  gchar *json = json_generator_to_data (gen, NULL);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (b);
  return json;
}


gboolean
screenshooter_jira_post_comment (const CloudConfig *config,
                                  const gchar *issue_key,
                                  const gchar *preset_title,
                                  const gchar *description,
                                  const gchar *image_url,
                                  GError **error)
{
  CURL *curl;
  CURLcode res;
  CurlBuffer response = { NULL, 0 };
  struct curl_slist *headers = NULL;
  gchar *auth, *url, *body;
  long http_code;
  gboolean success = FALSE;

  auth = build_auth_header (config->jira.email, config->jira.api_token);
  url = g_strdup_printf ("%s/rest/api/3/issue/%s/comment",
                          config->jira.base_url, issue_key);
  body = build_adf_comment_json (preset_title, description, image_url);

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


void
screenshooter_jira_issue_free (JiraIssue *issue)
{
  if (issue == NULL)
    return;
  g_free (issue->key);
  g_free (issue->summary);
  g_free (issue);
}


void
screenshooter_jira_issue_list_free (GList *issues)
{
  g_list_free_full (issues, (GDestroyNotify) screenshooter_jira_issue_free);
}
```

- [ ] **Step 3: Add new sources to lib/meson.build**

Add after the R2 entries:

```meson
  'screenshooter-jira.c',
  'screenshooter-jira.h',
```

- [ ] **Step 4: Build and verify**

Run: `meson compile -C build`
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add lib/screenshooter-jira.c lib/screenshooter-jira.h lib/meson.build
git commit -m "feat: add Jira REST API client with issue search and ADF comment posting"
```

---

## Chunk 4: Jira Dialog + Action Integration + CLI

### Task 5: GTK Jira issue search dialog

**Files:**
- Create: `lib/screenshooter-jira-dialog.h`
- Create: `lib/screenshooter-jira-dialog.c`
- Modify: `lib/meson.build` (add new sources)

- [ ] **Step 1: Create the header file**

Create `lib/screenshooter-jira-dialog.h`:

```c
#ifndef __SCREENSHOOTER_JIRA_DIALOG_H__
#define __SCREENSHOOTER_JIRA_DIALOG_H__

#include <gtk/gtk.h>
#include "screenshooter-cloud-config.h"

gboolean screenshooter_jira_dialog_run (GtkWindow *parent,
                                         const CloudConfig *config,
                                         const gchar *image_url);

#endif
```

- [ ] **Step 2: Create the implementation file**

Create `lib/screenshooter-jira-dialog.c`:

```c
#include "screenshooter-jira-dialog.h"
#include "screenshooter-jira.h"

#include <libxfce4ui/libxfce4ui.h>
#include <string.h>


typedef struct {
  const CloudConfig *config;
  const gchar *image_url;
  GtkWidget *search_entry;
  GtkWidget *list_box;
  GtkWidget *preset_combo;
  GtkWidget *desc_view;
  GtkWidget *post_button;
  gchar *selected_key;
  guint search_timeout_id;
} JiraDialogData;


static void
clear_list_box (GtkListBox *list_box)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (list_box));
  for (GList *l = children; l != NULL; l = l->next)
    gtk_widget_destroy (GTK_WIDGET (l->data));
  g_list_free (children);
}


static void
cb_row_selected (GtkListBox *box, GtkListBoxRow *row, JiraDialogData *data)
{
  if (row == NULL)
    {
      g_free (data->selected_key);
      data->selected_key = NULL;
      gtk_widget_set_sensitive (data->post_button, FALSE);
      return;
    }

  const gchar *key = g_object_get_data (G_OBJECT (row), "issue-key");
  g_free (data->selected_key);
  data->selected_key = g_strdup (key);
  gtk_widget_set_sensitive (data->post_button, TRUE);
}


static void
populate_results (JiraDialogData *data, GList *issues)
{
  clear_list_box (GTK_LIST_BOX (data->list_box));

  for (GList *l = issues; l != NULL; l = l->next)
    {
      JiraIssue *issue = l->data;
      gchar *label_text = g_strdup_printf ("%s  —  %s",
                                            issue->key, issue->summary);
      GtkWidget *label = gtk_label_new (label_text);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
      gtk_widget_set_margin_start (label, 6);
      gtk_widget_set_margin_end (label, 6);
      gtk_widget_set_margin_top (label, 4);
      gtk_widget_set_margin_bottom (label, 4);

      GtkWidget *row = gtk_list_box_row_new ();
      g_object_set_data_full (G_OBJECT (row), "issue-key",
                              g_strdup (issue->key), g_free);
      gtk_container_add (GTK_CONTAINER (row), label);
      gtk_list_box_insert (GTK_LIST_BOX (data->list_box), row, -1);
      g_free (label_text);
    }

  gtk_widget_show_all (data->list_box);
}


static gboolean
do_search (gpointer user_data)
{
  JiraDialogData *data = user_data;
  const gchar *query = gtk_entry_get_text (GTK_ENTRY (data->search_entry));
  GError *error = NULL;

  data->search_timeout_id = 0;

  GList *issues = screenshooter_jira_search (data->config, query, &error);
  if (error)
    {
      g_warning ("Jira search error: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  populate_results (data, issues);
  screenshooter_jira_issue_list_free (issues);
  return FALSE;
}


static void
cb_search_changed (GtkSearchEntry *entry, JiraDialogData *data)
{
  if (data->search_timeout_id > 0)
    g_source_remove (data->search_timeout_id);
  data->search_timeout_id = g_timeout_add (300, do_search, data);
}


gboolean
screenshooter_jira_dialog_run (GtkWindow *parent,
                                const CloudConfig *config,
                                const gchar *image_url)
{
  GtkWidget *dlg, *content, *box, *label, *scrolled;
  GtkWidget *search_entry, *list_box, *preset_combo, *desc_scrolled, *desc_view;
  JiraDialogData data = { 0 };
  gint response;
  gboolean result = FALSE;

  data.config = config;
  data.image_url = image_url;

  dlg = xfce_titled_dialog_new_with_mixed_buttons (
    "Post to Jira",
    parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "", "_Cancel", GTK_RESPONSE_CANCEL,
    "", "_Post", GTK_RESPONSE_OK,
    NULL);

  gtk_window_set_default_size (GTK_WINDOW (dlg), 500, 450);
  gtk_window_set_icon_name (GTK_WINDOW (dlg), "org.xfce.screenshooter");

  data.post_button = gtk_dialog_get_widget_for_response (
    GTK_DIALOG (dlg), GTK_RESPONSE_OK);
  gtk_widget_set_sensitive (data.post_button, FALSE);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dlg));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_box_pack_start (GTK_BOX (content), box, TRUE, TRUE, 0);

  /* Search entry */
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
    "<span weight=\"bold\">Search Issues</span>");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  search_entry = gtk_search_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (search_entry),
    "Type to search issues...");
  gtk_box_pack_start (GTK_BOX (box), search_entry, FALSE, FALSE, 0);
  data.search_entry = search_entry;

  /* Results list */
  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height (
    GTK_SCROLLED_WINDOW (scrolled), 180);
  gtk_widget_set_vexpand (scrolled, TRUE);
  gtk_box_pack_start (GTK_BOX (box), scrolled, TRUE, TRUE, 0);

  list_box = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_box),
    GTK_SELECTION_SINGLE);
  gtk_container_add (GTK_CONTAINER (scrolled), list_box);
  data.list_box = list_box;

  /* Preset combo */
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
    "<span weight=\"bold\">Preset</span>");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  preset_combo = gtk_combo_box_text_new ();
  if (config->presets.bug_evidence && config->presets.bug_evidence[0] != '\0')
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (preset_combo),
      "bug", config->presets.bug_evidence);
  if (config->presets.work_evidence && config->presets.work_evidence[0] != '\0')
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (preset_combo),
      "work", config->presets.work_evidence);
  gtk_combo_box_set_active (GTK_COMBO_BOX (preset_combo), 0);
  gtk_box_pack_start (GTK_BOX (box), preset_combo, FALSE, FALSE, 0);
  data.preset_combo = preset_combo;

  /* Description text view */
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
    "<span weight=\"bold\">Description</span>");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  desc_scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (desc_scrolled),
    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height (
    GTK_SCROLLED_WINDOW (desc_scrolled), 60);
  gtk_box_pack_start (GTK_BOX (box), desc_scrolled, FALSE, FALSE, 0);

  desc_view = gtk_text_view_new ();
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (desc_view), GTK_WRAP_WORD_CHAR);
  gtk_container_add (GTK_CONTAINER (desc_scrolled), desc_view);
  data.desc_view = desc_view;

  /* Connect signals */
  g_signal_connect (search_entry, "search-changed",
    G_CALLBACK (cb_search_changed), &data);
  g_signal_connect (list_box, "row-selected",
    G_CALLBACK (cb_row_selected), &data);

  gtk_widget_show_all (dlg);

  /* Trigger initial search */
  do_search (&data);

  response = gtk_dialog_run (GTK_DIALOG (dlg));

  if (response == GTK_RESPONSE_OK && data.selected_key)
    {
      gchar *preset_title = gtk_combo_box_text_get_active_text (
        GTK_COMBO_BOX_TEXT (preset_combo));
      GtkTextBuffer *buf = gtk_text_view_get_buffer (
        GTK_TEXT_VIEW (desc_view));
      GtkTextIter start, end;
      gtk_text_buffer_get_bounds (buf, &start, &end);
      gchar *desc = gtk_text_buffer_get_text (buf, &start, &end, FALSE);
      GError *error = NULL;

      result = screenshooter_jira_post_comment (config,
        data.selected_key,
        preset_title ? preset_title : "Screenshot",
        desc, image_url, &error);

      if (error)
        {
          GtkWidget *err_dlg = gtk_message_dialog_new (
            GTK_WINDOW (dlg), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Failed to post to Jira:\n%s", error->message);
          gtk_dialog_run (GTK_DIALOG (err_dlg));
          gtk_widget_destroy (err_dlg);
          g_error_free (error);
        }

      g_free (desc);
      g_free (preset_title);
    }

  if (data.search_timeout_id > 0)
    g_source_remove (data.search_timeout_id);
  g_free (data.selected_key);
  gtk_widget_destroy (dlg);
  return result;
}
```

- [ ] **Step 3: Add new sources to lib/meson.build**

Add after the jira entries:

```meson
  'screenshooter-jira-dialog.c',
  'screenshooter-jira-dialog.h',
```

- [ ] **Step 4: Build and verify**

Run: `meson compile -C build`
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add lib/screenshooter-jira-dialog.c lib/screenshooter-jira-dialog.h lib/meson.build
git commit -m "feat: add GTK Jira issue search dialog with debounced search"
```

---

### Task 6: Extend action system with UPLOAD_R2 and POST_JIRA flags

**Files:**
- Modify: `lib/screenshooter-global.h:28-35` (add new action flags)
- Modify: `lib/screenshooter-actions.c:20-26` (add includes)
- Modify: `lib/screenshooter-actions.c:80-158` (add cloud action handling in `action_idle`)

- [ ] **Step 1: Add action flags to screenshooter-global.h**

In `lib/screenshooter-global.h`, change the action enum (lines 29-35):

Replace:
```c
enum {
  NONE = 0,
  SAVE = 1,
  CLIPBOARD = 2,
  OPEN = 4,
  CUSTOM_ACTION = 8,
};
```

With:
```c
enum {
  NONE = 0,
  SAVE = 1,
  CLIPBOARD = 2,
  OPEN = 4,
  CUSTOM_ACTION = 8,
  UPLOAD_R2 = 16,
  POST_JIRA = 32,
};
```

Also add a `jira_issue_key` field to the `ScreenshotData` struct (after `last_extension`):

```c
  gchar *jira_issue_key;
```

- [ ] **Step 2: Add cloud includes to screenshooter-actions.c**

In `lib/screenshooter-actions.c`, after line 26 (`#include "screenshooter-format.h"`), add:

```c
#include "screenshooter-cloud-config.h"
#include "screenshooter-r2.h"
#include "screenshooter-jira.h"
#include "screenshooter-jira-dialog.h"
```

- [ ] **Step 3: Add cloud action handling in action_idle**

In `lib/screenshooter-actions.c`, before the "Persist last used file extension" comment (line 160), insert:

```c
  /* Cloud actions */
  if (sd->action & (UPLOAD_R2 | POST_JIRA))
    {
      GError *cloud_error = NULL;
      CloudConfig *cloud_config = screenshooter_cloud_config_load (&cloud_error);

      if (cloud_config == NULL)
        {
          g_warning ("Cloud config error: %s",
                     cloud_error ? cloud_error->message : "unknown");
          g_clear_error (&cloud_error);
        }
      else
        {
          gchar *public_url = NULL;

          if (sd->action & UPLOAD_R2)
            {
              /* Use save_location if available, otherwise save to temp */
              const gchar *upload_path = save_location;
              gchar *temp_path = NULL;

              if (upload_path == NULL)
                {
                  GFile *temp_dir = g_file_new_for_path (g_get_tmp_dir ());
                  gchar *temp_dir_uri = g_file_get_uri (temp_dir);
                  gchar *filename = screenshooter_get_filename_for_uri (
                    temp_dir_uri, sd->title, sd->last_extension, sd->timestamp);
                  gchar *temp_uri = screenshooter_save_screenshot (
                    sd->screenshot, temp_dir_uri, filename,
                    sd->last_extension, FALSE, FALSE);
                  if (temp_uri)
                    {
                      GFile *f = g_file_new_for_uri (temp_uri);
                      temp_path = g_file_get_path (f);
                      g_object_unref (f);
                      g_free (temp_uri);
                    }
                  g_object_unref (temp_dir);
                  g_free (temp_dir_uri);
                  g_free (filename);
                  upload_path = temp_path;
                }
              else
                {
                  /* save_location is a URI, convert to path */
                  GFile *f = g_file_new_for_uri (upload_path);
                  temp_path = g_file_get_path (f);
                  g_object_unref (f);
                  upload_path = temp_path;
                }

              if (upload_path)
                {
                  public_url = screenshooter_r2_upload (cloud_config,
                    upload_path, NULL, NULL, &cloud_error);
                  if (cloud_error)
                    {
                      g_warning ("R2 upload error: %s", cloud_error->message);
                      g_clear_error (&cloud_error);
                    }
                }

              g_free (temp_path);
            }

          if (sd->action & POST_JIRA)
            {
              if (public_url)
                {
                  if (sd->jira_issue_key && sd->jira_issue_key[0] != '\0')
                    {
                      /* CLI mode: post directly without dialog */
                      GError *jira_err = NULL;
                      screenshooter_jira_post_comment (cloud_config,
                        sd->jira_issue_key,
                        cloud_config->presets.bug_evidence
                          ? cloud_config->presets.bug_evidence : "Screenshot",
                        "", public_url, &jira_err);
                      if (jira_err)
                        {
                          g_warning ("Jira post error: %s", jira_err->message);
                          g_error_free (jira_err);
                        }
                    }
                  else
                    {
                      /* Interactive mode: show dialog */
                      screenshooter_jira_dialog_run (NULL, cloud_config,
                                                      public_url);
                    }
                }
              else
                {
                  GtkWidget *warn = gtk_message_dialog_new (NULL,
                    GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                    "No image URL available. Enable 'Upload to R2' to "
                    "post screenshots to Jira.");
                  gtk_dialog_run (GTK_DIALOG (warn));
                  gtk_widget_destroy (warn);
                }
            }

          g_free (public_url);
          screenshooter_cloud_config_free (cloud_config);
        }
    }
```

- [ ] **Step 4: Build and verify**

Run: `meson compile -C build`
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add lib/screenshooter-global.h lib/screenshooter-actions.c
git commit -m "feat: integrate R2 upload and Jira posting into action system"
```

---

### Task 7: Add Cloud section to screenshot actions dialog

**Files:**
- Modify: `lib/screenshooter-dialogs.c:1116-1350` (add cloud checkboxes in `screenshooter_actions_dialog_new`)

- [ ] **Step 1: Add include for cloud config**

At the top of `lib/screenshooter-dialogs.c`, after the existing includes (line 24), add:

```c
#include "screenshooter-cloud-config.h"
```

- [ ] **Step 2: Add callback functions**

Before the `screenshooter_actions_dialog_new` function (before line 1116), add:

```c
static void
cb_upload_r2_toggled (GtkToggleButton *tb, ScreenshotData *sd)
{
  if (gtk_toggle_button_get_active (tb))
    sd->action |= UPLOAD_R2;
  else
    sd->action &= ~UPLOAD_R2;
}

static void
cb_post_jira_toggled (GtkToggleButton *tb, ScreenshotData *sd)
{
  if (gtk_toggle_button_get_active (tb))
    sd->action |= POST_JIRA;
  else
    sd->action &= ~POST_JIRA;
}
```

- [ ] **Step 3: Add Cloud section checkboxes**

In `screenshooter_actions_dialog_new`, after the custom actions section (after the comment `/* Run the callback functions */` around line 1301), before the `/* Preview box */` comment (line 1304), insert:

```c
  /* Cloud section */
  {
    GError *cloud_err = NULL;
    CloudConfig *cloud_config = screenshooter_cloud_config_load (&cloud_err);
    gboolean r2_available = screenshooter_cloud_config_valid_r2 (cloud_config);
    gboolean jira_available = screenshooter_cloud_config_valid_jira (cloud_config);
    gchar *config_path = screenshooter_cloud_config_get_path ();
    gchar *tooltip_unavailable = g_strdup_printf (
      "Configure in %s", config_path);

    label = gtk_label_new ("");
    gtk_label_set_markup (GTK_LABEL (label),
      _("<span weight=\"bold\" stretch=\"semiexpanded\">Cloud</span>"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

    GtkWidget *cloud_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start (cloud_box, 12);
    gtk_box_pack_start (GTK_BOX (box), cloud_box, FALSE, FALSE, 0);

    /* Upload to R2 checkbox */
    GtkWidget *r2_check = gtk_check_button_new_with_label (
      _("Upload to Cloudflare R2"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (r2_check),
      (sd->action & UPLOAD_R2));
    gtk_widget_set_sensitive (r2_check, r2_available);
    if (!r2_available)
      gtk_widget_set_tooltip_text (r2_check, tooltip_unavailable);
    else
      gtk_widget_set_tooltip_text (r2_check,
        _("Upload the screenshot to Cloudflare R2 storage"));
    g_signal_connect (G_OBJECT (r2_check), "toggled",
      G_CALLBACK (cb_upload_r2_toggled), sd);
    gtk_box_pack_start (GTK_BOX (cloud_box), r2_check, FALSE, FALSE, 0);

    /* Post to Jira checkbox */
    GtkWidget *jira_check = gtk_check_button_new_with_label (
      _("Post to Jira issue"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (jira_check),
      (sd->action & POST_JIRA));
    gtk_widget_set_sensitive (jira_check, jira_available);
    if (!jira_available)
      gtk_widget_set_tooltip_text (jira_check, tooltip_unavailable);
    else
      gtk_widget_set_tooltip_text (jira_check,
        _("Post the screenshot as a comment on a Jira issue"));
    g_signal_connect (G_OBJECT (jira_check), "toggled",
      G_CALLBACK (cb_post_jira_toggled), sd);
    gtk_box_pack_start (GTK_BOX (cloud_box), jira_check, FALSE, FALSE, 0);

    g_free (tooltip_unavailable);
    g_free (config_path);
    screenshooter_cloud_config_free (cloud_config);
    g_clear_error (&cloud_err);
  }
```

- [ ] **Step 4: Build and verify**

Run: `meson compile -C build`
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add lib/screenshooter-dialogs.c
git commit -m "feat: add Cloud section with R2 and Jira checkboxes to actions dialog"
```

---

### Task 8: Add CLI flags for --upload-r2 and --jira

**Files:**
- Modify: `src/main.c:27-38` (add cli variables)
- Modify: `src/main.c:43-112` (add option entries)
- Modify: `src/main.c:252-315` (handle new flags)

- [ ] **Step 1: Add CLI variables**

In `src/main.c`, after line 35 (`gboolean show_in_folder = FALSE;`), add:

```c
gboolean upload_r2 = FALSE;
gchar *jira_issue = NULL;
```

- [ ] **Step 2: Add option entries**

In `src/main.c`, in the `entries` array, before the terminating `NULL` entry (line 107), add:

```c
  {
    "upload-r2", 'u', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &upload_r2,
    N_("Upload the screenshot to Cloudflare R2"),
    NULL
  },
  {
    "jira", 'j', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_STRING, &jira_issue,
    N_("Post to Jira issue (provide issue key, e.g. PROJ-123)"),
    NULL
  },
```

- [ ] **Step 3: Handle new flags in CLI mode**

In `src/main.c`, after the clipboard handling block (around line 293, after `sd->action_specified = TRUE;`), add:

```c
      if (upload_r2)
        {
          if (!sd->action_specified)
            sd->action = NONE;
          sd->action |= UPLOAD_R2;
          sd->action_specified = TRUE;
        }

      if (jira_issue != NULL)
        {
          if (!sd->action_specified)
            sd->action = NONE;
          sd->action |= POST_JIRA;
          sd->action_specified = TRUE;
          /* For CLI with --jira, also enable R2 upload */
          sd->action |= UPLOAD_R2;
          sd->jira_issue_key = g_strdup (jira_issue);
        }
```

- [ ] **Step 4: Add jira_issue to cleanup**

In `src/main.c`, in the cleanup section (around line 330), add:

```c
  g_free (sd->jira_issue_key);
  g_free (jira_issue);
```

- [ ] **Step 5: Build and verify**

Run: `meson compile -C build`
Expected: Compiles without errors.

- [ ] **Step 6: Test CLI help output**

Run: `./build/src/xfce4-screenshooter --help`
Expected: Shows `--upload-r2` and `--jira` options in help text.

- [ ] **Step 7: Commit**

```bash
git add src/main.c
git commit -m "feat: add --upload-r2 and --jira CLI flags"
```

---

### Task 9: Update libscreenshooter.h and add includes

**Files:**
- Modify: `lib/libscreenshooter.h` (add new header includes)

- [ ] **Step 1: Add new headers to libscreenshooter.h**

Read `lib/libscreenshooter.h` and add the following includes:

```c
#include "screenshooter-cloud-config.h"
#include "screenshooter-r2.h"
#include "screenshooter-jira.h"
#include "screenshooter-jira-dialog.h"
```

- [ ] **Step 2: Full build and test**

Run: `meson compile -C build`
Expected: Full project compiles without errors.

- [ ] **Step 3: Commit**

```bash
git add lib/libscreenshooter.h
git commit -m "feat: expose cloud integration headers via libscreenshooter.h"
```

---

### Task 10: Manual integration test

- [ ] **Step 1: Create default config**

Run: `./build/src/xfce4-screenshooter --help` to verify the binary runs.
Then manually create `~/.config/xfce4-screenshooter/cloud.toml` with your R2 and Jira credentials.

- [ ] **Step 2: Test GUI flow**

Run: `./build/src/xfce4-screenshooter`
Expected: Screenshot dialog shows "Cloud" section with R2 and Jira checkboxes.

- [ ] **Step 3: Test R2 upload**

Take a screenshot with "Upload to Cloudflare R2" checked.
Expected: File appears in R2 bucket under `captures/YYYY-MM-DD/`.

- [ ] **Step 4: Test Jira posting**

Take a screenshot with both "Upload to R2" and "Post to Jira" checked.
Expected: Jira dialog appears, search works, posting creates comment with embedded image.

- [ ] **Step 5: Test CLI**

Run: `./build/src/xfce4-screenshooter -f -u` (fullscreen + R2 upload)
Run: `./build/src/xfce4-screenshooter -f -j PROJ-123` (fullscreen + Jira post)
Expected: Both commands work without GUI prompts for action selection.
