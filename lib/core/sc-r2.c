#include "sc-r2.h"

#include <curl/curl.h>
#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <time.h>


const gchar *
sc_r2_content_type (const gchar *extension)
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
sc_r2_build_object_key (const gchar *filename)
{
  GDateTime *now = g_date_time_new_now_local ();
  gchar *date = g_date_time_format (now, "%Y-%m-%d");
  gchar *key = g_strdup_printf ("captures/%s/%s", date, filename);
  g_free (date);
  g_date_time_unref (now);
  return key;
}


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
sc_r2_upload (const CloudConfig *config,
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

  if (!sc_cloud_config_valid_r2 (config))
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
  content_type_str = g_strdup (sc_r2_content_type (extension));
  object_key = sc_r2_build_object_key (filename);

  host = g_strdup_printf ("%s.r2.cloudflarestorage.com",
                           config->r2.account_id);
  url = g_strdup_printf ("https://%s/%s/%s", host,
                          config->r2.bucket, object_key);

  payload_hash = sha256_hex ((const guchar *) file_contents, file_len);

  now = g_date_time_new_now_utc ();
  amz_date = g_date_time_format (now, "%Y%m%dT%H%M%SZ");
  date_stamp = g_date_time_format (now, "%Y%m%d");
  g_date_time_unref (now);

  gchar *encoded_key = g_uri_escape_string (object_key, "/", FALSE);
  signed_headers = g_strdup ("content-type;host;x-amz-content-sha256;x-amz-date");
  canonical_headers = g_strdup_printf (
    "content-type:%s\nhost:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
    content_type_str, host, payload_hash, amz_date);
  canonical_request = g_strdup_printf (
    "PUT\n/%s/%s\n\n%s\n%s\n%s",
    config->r2.bucket, encoded_key,
    canonical_headers, signed_headers, payload_hash);

  credential_scope = g_strdup_printf ("%s/auto/s3/aws4_request", date_stamp);
  canonical_request_hash = sha256_hex (
    (const guchar *) canonical_request, strlen (canonical_request));
  string_to_sign = g_strdup_printf (
    "AWS4-HMAC-SHA256\n%s\n%s\n%s",
    amz_date, credential_scope, canonical_request_hash);

  signing_key = derive_signing_key (config->r2.secret_access_key,
                                     date_stamp, "auto", "s3",
                                     &signing_key_len);
  signature = hmac_sha256_hex (signing_key, signing_key_len,
                                string_to_sign, strlen (string_to_sign));

  auth_header = g_strdup_printf (
    "Authorization: AWS4-HMAC-SHA256 Credential=%s/%s, "
    "SignedHeaders=%s, Signature=%s",
    config->r2.access_key_id, credential_scope,
    signed_headers, signature);

  curl = curl_easy_init ();
  if (curl == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to initialize curl");
      public_url = NULL;
      goto cleanup;
    }

  h_sha = g_strdup_printf ("x-amz-content-sha256: %s", payload_hash);
  h_date = g_strdup_printf ("x-amz-date: %s", amz_date);
  h_ct = g_strdup_printf ("Content-Type: %s", content_type_str);
  headers = curl_slist_append (headers, auth_header);
  headers = curl_slist_append (headers, h_sha);
  headers = curl_slist_append (headers, h_date);
  headers = curl_slist_append (headers, h_ct);

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt (curl, CURLOPT_POSTFIELDS, file_contents);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, (long) file_len);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
  {
    const gchar *ct = sc_r2_content_type (extension);
    long timeout = 30L;
    if (g_str_has_prefix (ct, "video/"))
      timeout = 120L;
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, timeout);
  }
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


gboolean
sc_r2_test_connection (const CloudConfig *config, GError **error)
{
  CURL *curl;
  CURLcode res;
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
  gchar *h_sha = NULL, *h_date = NULL;
  struct curl_slist *headers = NULL;
  long http_code;
  gboolean success = FALSE;

  if (!sc_cloud_config_valid_r2 (config))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "R2 configuration is incomplete");
      return FALSE;
    }

  host = g_strdup_printf ("%s.r2.cloudflarestorage.com",
                           config->r2.account_id);
  url = g_strdup_printf ("https://%s/%s", host, config->r2.bucket);

  /* Empty payload for HEAD request */
  payload_hash = sha256_hex ((const guchar *) "", 0);

  now = g_date_time_new_now_utc ();
  amz_date = g_date_time_format (now, "%Y%m%dT%H%M%SZ");
  date_stamp = g_date_time_format (now, "%Y%m%d");
  g_date_time_unref (now);

  signed_headers = g_strdup ("host;x-amz-content-sha256;x-amz-date");
  canonical_headers = g_strdup_printf (
    "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
    host, payload_hash, amz_date);
  canonical_request = g_strdup_printf (
    "HEAD\n/%s\n\n%s\n%s\n%s",
    config->r2.bucket,
    canonical_headers, signed_headers, payload_hash);

  credential_scope = g_strdup_printf ("%s/auto/s3/aws4_request", date_stamp);
  canonical_request_hash = sha256_hex (
    (const guchar *) canonical_request, strlen (canonical_request));
  string_to_sign = g_strdup_printf (
    "AWS4-HMAC-SHA256\n%s\n%s\n%s",
    amz_date, credential_scope, canonical_request_hash);

  signing_key = derive_signing_key (config->r2.secret_access_key,
                                     date_stamp, "auto", "s3",
                                     &signing_key_len);
  signature = hmac_sha256_hex (signing_key, signing_key_len,
                                string_to_sign, strlen (string_to_sign));

  auth_header = g_strdup_printf (
    "Authorization: AWS4-HMAC-SHA256 Credential=%s/%s, "
    "SignedHeaders=%s, Signature=%s",
    config->r2.access_key_id, credential_scope,
    signed_headers, signature);

  curl = curl_easy_init ();
  if (curl == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to initialize curl");
      goto cleanup;
    }

  h_sha = g_strdup_printf ("x-amz-content-sha256: %s", payload_hash);
  h_date = g_strdup_printf ("x-amz-date: %s", amz_date);
  headers = curl_slist_append (headers, auth_header);
  headers = curl_slist_append (headers, h_sha);
  headers = curl_slist_append (headers, h_date);

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 10L);

  res = curl_easy_perform (curl);
  if (res != CURLE_OK)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "R2 connection failed: %s", curl_easy_strerror (res));
    }
  else
    {
      curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
      if (http_code == 200 || http_code == 404)
        {
          success = TRUE;
        }
      else if (http_code == 403)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                       "R2 authentication failed (HTTP 403)");
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "R2 test connection failed with HTTP %ld", http_code);
        }
    }

  curl_easy_cleanup (curl);

cleanup:
  curl_slist_free_all (headers);
  g_free (h_sha);
  g_free (h_date);
  g_free (host);
  g_free (url);
  g_free (payload_hash);
  g_free (amz_date);
  g_free (date_stamp);
  g_free (signed_headers);
  g_free (canonical_headers);
  g_free (canonical_request);
  g_free (credential_scope);
  g_free (canonical_request_hash);
  g_free (string_to_sign);
  g_free (signing_key);
  g_free (signature);
  g_free (auth_header);

  return success;
}
