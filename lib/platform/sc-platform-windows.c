#include "sc-platform.h"

#ifdef G_OS_WIN32

#include <windows.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>


static void
pixbuf_free_pixels (guchar *pixels, gpointer data)
{
  g_free (pixels);
}


gchar *
sc_platform_config_dir (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "xfce4-screenshooter", NULL);
}


void
sc_platform_restrict_file (const gchar *path)
{
  PSECURITY_DESCRIPTOR psd = NULL;
  BOOL ok;
  PACL dacl = NULL;
  BOOL dacl_present = FALSE;
  BOOL dacl_defaulted = FALSE;
  gunichar2 *wpath = NULL;

  ok = ConvertStringSecurityDescriptorToSecurityDescriptorW (
      L"D:P(A;;FA;;;OW)",
      SDDL_REVISION_1,
      &psd,
      NULL);
  if (!ok)
    {
      g_warning ("ConvertStringSecurityDescriptorToSecurityDescriptorW "
                 "failed: %lu", (unsigned long) GetLastError ());
      return;
    }

  GetSecurityDescriptorDacl (psd, &dacl_present, &dacl, &dacl_defaulted);
  if (!dacl_present || dacl == NULL)
    {
      g_warning ("Failed to get DACL from security descriptor");
      LocalFree (psd);
      return;
    }

  wpath = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
  if (wpath == NULL)
    {
      g_warning ("Failed to convert path to UTF-16");
      LocalFree (psd);
      return;
    }

  SetNamedSecurityInfoW ((LPWSTR) wpath,
                         SE_FILE_OBJECT,
                         DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                         NULL, NULL, dacl, NULL);

  g_free (wpath);
  LocalFree (psd);
}


GdkPixbuf *
sc_platform_capture (ScCaptureMode mode, ScRegion *region)
{
  HDC hdc_screen = NULL;
  HDC hdc_mem = NULL;
  HBITMAP hbm = NULL;
  HBITMAP old_bm = NULL;
  BITMAPINFO bmi;
  guchar *pixels = NULL;
  GdkPixbuf *pixbuf = NULL;
  gint x, y, width, height;
  gint i;
  gint stride;

  switch (mode)
    {
    case SC_CAPTURE_FULLSCREEN:
      x = GetSystemMetrics (SM_XVIRTUALSCREEN);
      y = GetSystemMetrics (SM_YVIRTUALSCREEN);
      width = GetSystemMetrics (SM_CXVIRTUALSCREEN);
      height = GetSystemMetrics (SM_CYVIRTUALSCREEN);
      break;

    case SC_CAPTURE_WINDOW:
      /* Fallback to primary monitor for now */
      x = 0;
      y = 0;
      width = GetSystemMetrics (SM_CXSCREEN);
      height = GetSystemMetrics (SM_CYSCREEN);
      break;

    case SC_CAPTURE_REGION:
      if (region == NULL)
        {
          g_warning ("sc_platform_capture: region is NULL for REGION mode");
          return NULL;
        }
      x = region->x;
      y = region->y;
      width = region->width;
      height = region->height;
      break;

    default:
      g_warning ("sc_platform_capture: unknown mode %d", mode);
      return NULL;
    }

  if (width <= 0 || height <= 0)
    {
      g_warning ("sc_platform_capture: invalid dimensions %dx%d", width, height);
      return NULL;
    }

  hdc_screen = GetDC (NULL);
  if (hdc_screen == NULL)
    {
      g_warning ("sc_platform_capture: GetDC failed");
      return NULL;
    }

  hdc_mem = CreateCompatibleDC (hdc_screen);
  if (hdc_mem == NULL)
    {
      g_warning ("sc_platform_capture: CreateCompatibleDC failed");
      ReleaseDC (NULL, hdc_screen);
      return NULL;
    }

  hbm = CreateCompatibleBitmap (hdc_screen, width, height);
  if (hbm == NULL)
    {
      g_warning ("sc_platform_capture: CreateCompatibleBitmap failed");
      DeleteDC (hdc_mem);
      ReleaseDC (NULL, hdc_screen);
      return NULL;
    }

  old_bm = SelectObject (hdc_mem, hbm);
  BitBlt (hdc_mem, 0, 0, width, height, hdc_screen, x, y, SRCCOPY);
  SelectObject (hdc_mem, old_bm);

  /* Extract pixels via GetDIBits */
  memset (&bmi, 0, sizeof (bmi));
  bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height; /* top-down */
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  pixels = g_malloc ((gsize) width * height * 4);
  GetDIBits (hdc_mem, hbm, 0, height, pixels, &bmi, DIB_RGB_COLORS);

  /* Convert BGRA to RGBA */
  stride = width * 4;
  for (i = 0; i < width * height; i++)
    {
      guchar tmp = pixels[i * 4 + 0]; /* B */
      pixels[i * 4 + 0] = pixels[i * 4 + 2]; /* R */
      pixels[i * 4 + 2] = tmp; /* B */
      pixels[i * 4 + 3] = 0xFF; /* A */
    }

  pixbuf = gdk_pixbuf_new_from_data (pixels,
                                      GDK_COLORSPACE_RGB,
                                      TRUE, /* has_alpha */
                                      8,
                                      width, height,
                                      stride,
                                      pixbuf_free_pixels,
                                      NULL);

  DeleteObject (hbm);
  DeleteDC (hdc_mem);
  ReleaseDC (NULL, hdc_screen);

  return pixbuf;
}


gboolean
sc_platform_select_region (ScRegion *out_region)
{
  g_warning ("sc_platform_select_region: not yet implemented");
  return FALSE;
}


gchar **
sc_platform_recorder_args (ScCaptureMode mode, ScRegion *region)
{
  GPtrArray *args;

  args = g_ptr_array_new ();
  g_ptr_array_add (args, g_strdup ("-f"));
  g_ptr_array_add (args, g_strdup ("gdigrab"));
  g_ptr_array_add (args, g_strdup ("-i"));
  g_ptr_array_add (args, g_strdup ("desktop"));

  if (mode == SC_CAPTURE_REGION && region != NULL)
    {
      g_ptr_array_add (args, g_strdup ("-offset_x"));
      g_ptr_array_add (args, g_strdup_printf ("%d", region->x));
      g_ptr_array_add (args, g_strdup ("-offset_y"));
      g_ptr_array_add (args, g_strdup_printf ("%d", region->y));
      g_ptr_array_add (args, g_strdup ("-video_size"));
      g_ptr_array_add (args, g_strdup_printf ("%dx%d",
                                               region->width,
                                               region->height));
    }

  g_ptr_array_add (args, NULL);
  return (gchar **) g_ptr_array_free (args, FALSE);
}


void
sc_platform_recorder_args_free (gchar **args)
{
  if (args == NULL)
    return;
  g_strfreev (args);
}


gboolean
sc_platform_clipboard_copy_image (GdkPixbuf *pixbuf)
{
  gint width, height, rowstride, n_channels;
  const guchar *src_pixels;
  HGLOBAL hGlobal;
  guchar *dst;
  BITMAPINFOHEADER *bih;
  gsize data_size;
  gint y, x;

  g_return_val_if_fail (pixbuf != NULL, FALSE);

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  n_channels = gdk_pixbuf_get_n_channels (pixbuf);
  src_pixels = gdk_pixbuf_get_pixels (pixbuf);

  data_size = sizeof (BITMAPINFOHEADER) + (gsize) width * height * 4;
  hGlobal = GlobalAlloc (GMEM_MOVEABLE, data_size);
  if (hGlobal == NULL)
    {
      g_warning ("sc_platform_clipboard_copy_image: GlobalAlloc failed");
      return FALSE;
    }

  dst = (guchar *) GlobalLock (hGlobal);
  if (dst == NULL)
    {
      g_warning ("sc_platform_clipboard_copy_image: GlobalLock failed");
      GlobalFree (hGlobal);
      return FALSE;
    }

  /* Write BITMAPINFOHEADER */
  bih = (BITMAPINFOHEADER *) dst;
  memset (bih, 0, sizeof (BITMAPINFOHEADER));
  bih->biSize = sizeof (BITMAPINFOHEADER);
  bih->biWidth = width;
  bih->biHeight = -height; /* top-down */
  bih->biPlanes = 1;
  bih->biBitCount = 32;
  bih->biCompression = BI_RGB;
  bih->biSizeImage = (DWORD) width * height * 4;

  /* Write BGRA pixel data */
  dst += sizeof (BITMAPINFOHEADER);
  for (y = 0; y < height; y++)
    {
      const guchar *row = src_pixels + y * rowstride;
      for (x = 0; x < width; x++)
        {
          gint src_off = x * n_channels;
          gint dst_off = (y * width + x) * 4;
          dst[dst_off + 0] = row[src_off + 2]; /* B */
          dst[dst_off + 1] = row[src_off + 1]; /* G */
          dst[dst_off + 2] = row[src_off + 0]; /* R */
          dst[dst_off + 3] = (n_channels == 4) ? row[src_off + 3] : 0xFF; /* A */
        }
    }

  GlobalUnlock (hGlobal);

  if (!OpenClipboard (NULL))
    {
      g_warning ("sc_platform_clipboard_copy_image: OpenClipboard failed");
      GlobalFree (hGlobal);
      return FALSE;
    }

  EmptyClipboard ();
  SetClipboardData (CF_DIB, hGlobal);
  CloseClipboard ();
  /* Do NOT GlobalFree -- clipboard owns it */

  return TRUE;
}


void
sc_platform_notify (const gchar *title, const gchar *body)
{
  WNDCLASSW wc;
  HWND hwnd;
  NOTIFYICONDATAW nid;
  gunichar2 *wtitle = NULL;
  gunichar2 *wbody = NULL;

  memset (&wc, 0, sizeof (wc));
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandle (NULL);
  wc.lpszClassName = L"ScNotifyClass";
  RegisterClassW (&wc);

  hwnd = CreateWindowW (L"ScNotifyClass", L"",
                        0, 0, 0, 0, 0,
                        HWND_MESSAGE, NULL,
                        GetModuleHandle (NULL), NULL);
  if (hwnd == NULL)
    {
      g_warning ("sc_platform_notify: CreateWindowW failed");
      return;
    }

  memset (&nid, 0, sizeof (nid));
  nid.cbSize = sizeof (nid);
  nid.hWnd = hwnd;
  nid.uID = 1;
  nid.uFlags = NIF_INFO | NIF_ICON;
  nid.dwInfoFlags = NIIF_INFO;
  nid.hIcon = LoadIcon (NULL, IDI_INFORMATION);

  if (title != NULL)
    {
      wtitle = g_utf8_to_utf16 (title, -1, NULL, NULL, NULL);
      if (wtitle != NULL)
        {
          wcsncpy (nid.szInfoTitle, (const wchar_t *) wtitle, 63);
          nid.szInfoTitle[63] = L'\0';
          g_free (wtitle);
        }
    }

  if (body != NULL)
    {
      wbody = g_utf8_to_utf16 (body, -1, NULL, NULL, NULL);
      if (wbody != NULL)
        {
          wcsncpy (nid.szInfo, (const wchar_t *) wbody, 255);
          nid.szInfo[255] = L'\0';
          g_free (wbody);
        }
    }

  Shell_NotifyIconW (NIM_ADD, &nid);
  Shell_NotifyIconW (NIM_MODIFY, &nid);

  /* Brief delay so the balloon is visible before cleanup */
  Sleep (100);

  Shell_NotifyIconW (NIM_DELETE, &nid);
  DestroyWindow (hwnd);
}


#else /* !G_OS_WIN32 */


gchar *
sc_platform_config_dir (void)
{
  g_assert_not_reached ();
  return NULL;
}


void
sc_platform_restrict_file (const gchar *path)
{
  g_assert_not_reached ();
}


GdkPixbuf *
sc_platform_capture (ScCaptureMode mode, ScRegion *region)
{
  g_assert_not_reached ();
  return NULL;
}


gboolean
sc_platform_select_region (ScRegion *out_region)
{
  g_assert_not_reached ();
  return FALSE;
}


gchar **
sc_platform_recorder_args (ScCaptureMode mode, ScRegion *region)
{
  g_assert_not_reached ();
  return NULL;
}


void
sc_platform_recorder_args_free (gchar **args)
{
  g_assert_not_reached ();
}


gboolean
sc_platform_clipboard_copy_image (GdkPixbuf *pixbuf)
{
  g_assert_not_reached ();
  return FALSE;
}


void
sc_platform_notify (const gchar *title, const gchar *body)
{
  g_assert_not_reached ();
}


#endif /* G_OS_WIN32 */
