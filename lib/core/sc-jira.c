#include "sc-jira.h"

#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <gio/gio.h>
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
sc_jira_search (const CloudConfig *config,
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

  if (!sc_cloud_config_valid_jira (config))
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
      /* If query looks like an issue key (e.g. PROJ-123), search by key */
      if (g_regex_match_simple ("^[A-Z]+-[0-9]+$", safe, 0, 0))
        jql = g_strdup_printf ("key = %s", safe);
      else
        jql = g_strdup_printf (
          "project = %s AND summary ~ \\\"%s\\\" AND status != Done "
          "ORDER BY updated DESC",
          config->jira.default_project, safe);
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

  auth = build_auth_header (config->jira.email, config->jira.api_token);
  url = g_strdup_printf ("%s/rest/api/3/search/jql", config->jira.base_url);

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


static gchar *
build_adf_comment_json (const gchar *preset_title,
                         const gchar *description,
                         const gchar *image_url)
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

  /* Embedded image */
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


gboolean
sc_jira_post_comment (const CloudConfig *config,
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

  g_return_val_if_fail (config != NULL, FALSE);
  if (!sc_cloud_config_valid_jira (config))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Jira configuration is incomplete");
      return FALSE;
    }
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


gboolean
sc_jira_test_connection (const CloudConfig *config, GError **error)
{
  if (!sc_cloud_config_valid_jira (config))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Jira configuration is incomplete");
      return FALSE;
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Jira test connection not yet implemented");
  return FALSE;
}


void
sc_jira_issue_free (JiraIssue *issue)
{
  if (issue == NULL)
    return;
  g_free (issue->key);
  g_free (issue->summary);
  g_free (issue);
}


void
sc_jira_issue_list_free (GList *issues)
{
  g_list_free_full (issues, (GDestroyNotify) sc_jira_issue_free);
}
