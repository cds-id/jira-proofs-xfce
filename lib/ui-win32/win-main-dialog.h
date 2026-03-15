#ifndef __WIN_MAIN_DIALOG_H__
#define __WIN_MAIN_DIALOG_H__

#include <glib.h>

/* Encoded result: low byte = capture mode (0=full,1=window,2=region),
   high byte = action (0=save,1=clipboard,2=open,3=upload-r2,4=post-jira).
   Returns 0 if cancelled. */
gint win_main_dialog_run (void);

#endif
