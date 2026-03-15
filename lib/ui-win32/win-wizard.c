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

/* Per-wizard shared state passed through lParam */
typedef struct {
  const gchar *config_dir;
  CloudConfig *config;
} WizardData;


static gchar *
get_dialog_text (HWND hwnd, int id)
{
  WCHAR buf[1024];
  GetDlgItemTextW (hwnd, id, buf, G_N_ELEMENTS (buf));
  return g_utf16_to_utf8 ((gunichar2 *) buf, -1, NULL, NULL, NULL);
}


static INT_PTR CALLBACK
welcome_page_proc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_NOTIFY:
      {
        NMHDR *nm = (NMHDR *) lp;
        if (nm->code == PSN_SETACTIVE)
          {
            /* Enable Next button */
            PropSheet_SetWizButtons (GetParent (hwnd), PSWIZB_NEXT);
            SetWindowLongPtr (hwnd, DWLP_MSGRESULT, 0);
            return TRUE;
          }
      }
      break;
    }
  return FALSE;
}


static INT_PTR CALLBACK
r2_page_proc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_INITDIALOG:
      {
        PROPSHEETPAGE *psp = (PROPSHEETPAGE *) lp;
        SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR) psp->lParam);
        return TRUE;
      }

    case WM_COMMAND:
      if (LOWORD (wp) == IDC_R2_TEST_BTN)
        {
          WizardData *wd = (WizardData *) GetWindowLongPtr (hwnd, GWLP_USERDATA);
          CloudConfig *tmp = sc_cloud_config_create_default ();
          GError *err = NULL;

          g_free (tmp->r2.account_id);
          g_free (tmp->r2.access_key_id);
          g_free (tmp->r2.secret_access_key);
          g_free (tmp->r2.bucket);
          g_free (tmp->r2.public_url);

          tmp->r2.account_id = get_dialog_text (hwnd, IDC_R2_ACCOUNT_ID);
          tmp->r2.access_key_id = get_dialog_text (hwnd, IDC_R2_ACCESS_KEY);
          tmp->r2.secret_access_key = get_dialog_text (hwnd, IDC_R2_SECRET_KEY);
          tmp->r2.bucket = get_dialog_text (hwnd, IDC_R2_BUCKET);
          tmp->r2.public_url = get_dialog_text (hwnd, IDC_R2_PUBLIC_URL);

          SetDlgItemTextW (hwnd, IDC_R2_TEST_RESULT, L"Testing...");

          if (sc_r2_test_connection (tmp, &err))
            SetDlgItemTextW (hwnd, IDC_R2_TEST_RESULT, L"Connection successful!");
          else
            {
              gchar *msg_text = g_strdup_printf ("Failed: %s",
                  err ? err->message : "unknown error");
              WCHAR *wmsg = (WCHAR *) g_utf8_to_utf16 (msg_text, -1, NULL, NULL, NULL);
              SetDlgItemTextW (hwnd, IDC_R2_TEST_RESULT, wmsg);
              g_free (wmsg);
              g_free (msg_text);
              g_clear_error (&err);
            }

          sc_cloud_config_free (tmp);
          (void) wd;
          return TRUE;
        }
      break;

    case WM_NOTIFY:
      {
        NMHDR *nm = (NMHDR *) lp;
        if (nm->code == PSN_SETACTIVE)
          {
            PropSheet_SetWizButtons (GetParent (hwnd), PSWIZB_BACK | PSWIZB_NEXT);
            SetWindowLongPtr (hwnd, DWLP_MSGRESULT, 0);
            return TRUE;
          }
        if (nm->code == PSN_WIZNEXT)
          {
            /* Store R2 entries into shared config */
            WizardData *wd = (WizardData *) GetWindowLongPtr (hwnd, GWLP_USERDATA);
            if (wd && wd->config)
              {
                g_free (wd->config->r2.account_id);
                g_free (wd->config->r2.access_key_id);
                g_free (wd->config->r2.secret_access_key);
                g_free (wd->config->r2.bucket);
                g_free (wd->config->r2.public_url);

                wd->config->r2.account_id = get_dialog_text (hwnd, IDC_R2_ACCOUNT_ID);
                wd->config->r2.access_key_id = get_dialog_text (hwnd, IDC_R2_ACCESS_KEY);
                wd->config->r2.secret_access_key = get_dialog_text (hwnd, IDC_R2_SECRET_KEY);
                wd->config->r2.bucket = get_dialog_text (hwnd, IDC_R2_BUCKET);
                wd->config->r2.public_url = get_dialog_text (hwnd, IDC_R2_PUBLIC_URL);
              }
            SetWindowLongPtr (hwnd, DWLP_MSGRESULT, 0);
            return TRUE;
          }
      }
      break;
    }
  return FALSE;
}


static INT_PTR CALLBACK
jira_page_proc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_INITDIALOG:
      {
        PROPSHEETPAGE *psp = (PROPSHEETPAGE *) lp;
        SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR) psp->lParam);
        return TRUE;
      }

    case WM_COMMAND:
      if (LOWORD (wp) == IDC_JIRA_TEST_BTN)
        {
          WizardData *wd = (WizardData *) GetWindowLongPtr (hwnd, GWLP_USERDATA);
          CloudConfig *tmp = sc_cloud_config_create_default ();
          GError *err = NULL;

          g_free (tmp->jira.base_url);
          g_free (tmp->jira.email);
          g_free (tmp->jira.api_token);
          g_free (tmp->jira.default_project);

          tmp->jira.base_url = get_dialog_text (hwnd, IDC_JIRA_BASE_URL);
          tmp->jira.email = get_dialog_text (hwnd, IDC_JIRA_EMAIL);
          tmp->jira.api_token = get_dialog_text (hwnd, IDC_JIRA_API_TOKEN);
          tmp->jira.default_project = get_dialog_text (hwnd, IDC_JIRA_PROJECT);

          SetDlgItemTextW (hwnd, IDC_JIRA_TEST_RESULT, L"Testing...");

          if (sc_jira_test_connection (tmp, &err))
            SetDlgItemTextW (hwnd, IDC_JIRA_TEST_RESULT, L"Connection successful!");
          else
            {
              gchar *msg_text = g_strdup_printf ("Failed: %s",
                  err ? err->message : "unknown error");
              WCHAR *wmsg = (WCHAR *) g_utf8_to_utf16 (msg_text, -1, NULL, NULL, NULL);
              SetDlgItemTextW (hwnd, IDC_JIRA_TEST_RESULT, wmsg);
              g_free (wmsg);
              g_free (msg_text);
              g_clear_error (&err);
            }

          sc_cloud_config_free (tmp);
          (void) wd;
          return TRUE;
        }
      break;

    case WM_NOTIFY:
      {
        NMHDR *nm = (NMHDR *) lp;
        if (nm->code == PSN_SETACTIVE)
          {
            PropSheet_SetWizButtons (GetParent (hwnd), PSWIZB_BACK | PSWIZB_NEXT);
            SetWindowLongPtr (hwnd, DWLP_MSGRESULT, 0);
            return TRUE;
          }
        if (nm->code == PSN_WIZNEXT)
          {
            /* Store Jira entries into shared config */
            WizardData *wd = (WizardData *) GetWindowLongPtr (hwnd, GWLP_USERDATA);
            if (wd && wd->config)
              {
                g_free (wd->config->jira.base_url);
                g_free (wd->config->jira.email);
                g_free (wd->config->jira.api_token);
                g_free (wd->config->jira.default_project);

                wd->config->jira.base_url = get_dialog_text (hwnd, IDC_JIRA_BASE_URL);
                wd->config->jira.email = get_dialog_text (hwnd, IDC_JIRA_EMAIL);
                wd->config->jira.api_token = get_dialog_text (hwnd, IDC_JIRA_API_TOKEN);
                wd->config->jira.default_project = get_dialog_text (hwnd, IDC_JIRA_PROJECT);
              }
            SetWindowLongPtr (hwnd, DWLP_MSGRESULT, 0);
            return TRUE;
          }
      }
      break;
    }
  return FALSE;
}


static INT_PTR CALLBACK
done_page_proc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  (void) wp;

  switch (msg)
    {
    case WM_INITDIALOG:
      {
        PROPSHEETPAGE *psp = (PROPSHEETPAGE *) lp;
        SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR) psp->lParam);
        return TRUE;
      }

    case WM_NOTIFY:
      {
        NMHDR *nm = (NMHDR *) lp;
        if (nm->code == PSN_SETACTIVE)
          {
            PropSheet_SetWizButtons (GetParent (hwnd), PSWIZB_BACK | PSWIZB_FINISH);

            /* Build summary text */
            WizardData *wd = (WizardData *) GetWindowLongPtr (hwnd, GWLP_USERDATA);
            if (wd && wd->config)
              {
                GString *summary = g_string_new ("");

                if (sc_cloud_config_valid_r2 (wd->config))
                  g_string_append_printf (summary, "R2: %s / %s\r\n",
                      wd->config->r2.bucket, wd->config->r2.public_url);
                else
                  g_string_append (summary, "R2: not configured\r\n");

                if (sc_cloud_config_valid_jira (wd->config))
                  g_string_append_printf (summary, "Jira: %s (%s)\r\n",
                      wd->config->jira.base_url, wd->config->jira.default_project);
                else
                  g_string_append (summary, "Jira: not configured\r\n");

                g_string_append (summary, "\r\nClick Finish to save.");

                WCHAR *wsummary = (WCHAR *) g_utf8_to_utf16 (summary->str, -1, NULL, NULL, NULL);
                SetDlgItemTextW (hwnd, IDC_DONE_SUMMARY, wsummary);
                g_free (wsummary);
                g_string_free (summary, TRUE);
              }

            SetWindowLongPtr (hwnd, DWLP_MSGRESULT, 0);
            return TRUE;
          }

        if (nm->code == PSN_WIZFINISH)
          {
            WizardData *wd = (WizardData *) GetWindowLongPtr (hwnd, GWLP_USERDATA);
            if (wd && wd->config)
              {
                GError *err = NULL;
                wd->config->loaded = TRUE;
                if (!sc_cloud_config_save (wd->config, wd->config_dir, &err))
                  {
                    gchar *msg_text = g_strdup_printf ("Failed to save config: %s",
                        err ? err->message : "unknown");
                    MessageBoxA (hwnd, msg_text, "Error", MB_OK | MB_ICONERROR);
                    g_free (msg_text);
                    g_clear_error (&err);
                  }
              }
            SetWindowLongPtr (hwnd, DWLP_MSGRESULT, 0);
            return TRUE;
          }
      }
      break;
    }
  return FALSE;
}


gboolean
win_wizard_run (const gchar *config_dir)
{
  WizardData wd = { 0 };
  wd.config_dir = config_dir;
  wd.config = sc_cloud_config_create_default ();

  PROPSHEETPAGE pages[4];
  memset (pages, 0, sizeof (pages));

  /* Welcome page */
  pages[0].dwSize = sizeof (PROPSHEETPAGE);
  pages[0].dwFlags = PSP_DEFAULT;
  pages[0].hInstance = GetModuleHandle (NULL);
  pages[0].pszTemplate = MAKEINTRESOURCE (IDD_WIZARD_WELCOME);
  pages[0].pfnDlgProc = welcome_page_proc;
  pages[0].lParam = (LPARAM) &wd;

  /* R2 page */
  pages[1].dwSize = sizeof (PROPSHEETPAGE);
  pages[1].dwFlags = PSP_DEFAULT;
  pages[1].hInstance = GetModuleHandle (NULL);
  pages[1].pszTemplate = MAKEINTRESOURCE (IDD_WIZARD_R2);
  pages[1].pfnDlgProc = r2_page_proc;
  pages[1].lParam = (LPARAM) &wd;

  /* Jira page */
  pages[2].dwSize = sizeof (PROPSHEETPAGE);
  pages[2].dwFlags = PSP_DEFAULT;
  pages[2].hInstance = GetModuleHandle (NULL);
  pages[2].pszTemplate = MAKEINTRESOURCE (IDD_WIZARD_JIRA);
  pages[2].pfnDlgProc = jira_page_proc;
  pages[2].lParam = (LPARAM) &wd;

  /* Done page */
  pages[3].dwSize = sizeof (PROPSHEETPAGE);
  pages[3].dwFlags = PSP_DEFAULT;
  pages[3].hInstance = GetModuleHandle (NULL);
  pages[3].pszTemplate = MAKEINTRESOURCE (IDD_WIZARD_DONE);
  pages[3].pfnDlgProc = done_page_proc;
  pages[3].lParam = (LPARAM) &wd;

  PROPSHEETHEADER psh;
  memset (&psh, 0, sizeof (psh));
  psh.dwSize = sizeof (PROPSHEETHEADER);
  psh.dwFlags = PSH_WIZARD | PSH_PROPSHEETPAGE;
  psh.hwndParent = NULL;
  psh.hInstance = GetModuleHandle (NULL);
  psh.pszCaption = "Cloud Services Setup";
  psh.nPages = 4;
  psh.nStartPage = 0;
  psh.ppsp = pages;

  INT_PTR result = PropertySheet (&psh);

  if (result <= 0)
    {
      /* Cancelled — save empty default so wizard doesn't re-trigger */
      GError *err = NULL;
      CloudConfig *empty = sc_cloud_config_create_default ();
      empty->loaded = TRUE;
      sc_cloud_config_save (empty, config_dir, &err);
      sc_cloud_config_free (empty);
      g_clear_error (&err);

      sc_cloud_config_free (wd.config);
      return FALSE;
    }

  sc_cloud_config_free (wd.config);
  return TRUE;
}

#else /* !G_OS_WIN32 */

gboolean
win_wizard_run (const gchar *config_dir)
{
  (void) config_dir;
  g_warning ("win_wizard_run() called on non-Windows platform");
  return FALSE;
}

#endif /* G_OS_WIN32 */
