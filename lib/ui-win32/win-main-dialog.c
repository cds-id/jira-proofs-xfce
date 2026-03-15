#include "win-main-dialog.h"
#include "resource.h"
#include "win-wizard.h"
#include <sc-platform.h>

#ifdef G_OS_WIN32

#include <windows.h>
#include <commctrl.h>

/* Action indices in the combo box */
enum {
  ACTION_SAVE = 0,
  ACTION_CLIPBOARD,
  ACTION_OPEN,
  ACTION_UPLOAD_R2,
  ACTION_POST_JIRA,
  ACTION_COUNT
};

static const char *action_labels[] = {
  "Save",
  "Copy to clipboard",
  "Open with application",
  "Upload to R2",
  "Post to Jira",
};


static INT_PTR CALLBACK
main_dialog_proc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  (void) lp;

  switch (msg)
    {
    case WM_INITDIALOG:
      {
        /* Set fullscreen radio as default */
        CheckRadioButton (hwnd, IDC_RADIO_FULLSCREEN, IDC_RADIO_REGION,
                          IDC_RADIO_FULLSCREEN);

        /* Set default delay to 0 */
        SetDlgItemInt (hwnd, IDC_SPIN_DELAY, 0, FALSE);

        /* Populate action combo */
        HWND combo = GetDlgItem (hwnd, IDC_COMBO_ACTION);
        for (int i = 0; i < ACTION_COUNT; i++)
          SendMessageA (combo, CB_ADDSTRING, 0, (LPARAM) action_labels[i]);
        SendMessage (combo, CB_SETCURSEL, 0, 0);

        return TRUE;
      }

    case WM_COMMAND:
      switch (LOWORD (wp))
        {
        case IDC_BTN_CAPTURE:
          {
            /* Determine capture mode */
            gint mode = 0;
            if (IsDlgButtonChecked (hwnd, IDC_RADIO_WINDOW) == BST_CHECKED)
              mode = 1;
            else if (IsDlgButtonChecked (hwnd, IDC_RADIO_REGION) == BST_CHECKED)
              mode = 2;

            /* Determine action */
            HWND combo = GetDlgItem (hwnd, IDC_COMBO_ACTION);
            gint action = (gint) SendMessage (combo, CB_GETCURSEL, 0, 0);
            if (action == CB_ERR)
              action = 0;

            /* Encode: low byte = mode, high byte = action */
            gint result = mode | (action << 8);
            if (result == 0)
              result = 0x10000; /* ensure non-zero for fullscreen+save */

            EndDialog (hwnd, result);
            return TRUE;
          }

        case IDC_BTN_CLOUD_SETUP:
          {
            gchar *config_dir = sc_platform_config_dir ();
            win_wizard_run (config_dir);
            g_free (config_dir);
            return TRUE;
          }

        case IDCANCEL:
          EndDialog (hwnd, 0);
          return TRUE;
        }
      break;

    case WM_CLOSE:
      EndDialog (hwnd, 0);
      return TRUE;
    }

  return FALSE;
}


gint
win_main_dialog_run (void)
{
  INT_PTR result = DialogBoxParam (
      GetModuleHandle (NULL),
      MAKEINTRESOURCE (IDD_MAIN_DIALOG),
      NULL,
      main_dialog_proc,
      0);

  return (gint) result;
}

#else /* !G_OS_WIN32 */

gint
win_main_dialog_run (void)
{
  g_warning ("win_main_dialog_run() called on non-Windows platform");
  return 0;
}

#endif /* G_OS_WIN32 */
