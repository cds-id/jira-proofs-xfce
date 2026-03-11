#ifndef __HAVE_VIDEO_EDITOR_TIMELINE_H__
#define __HAVE_VIDEO_EDITOR_TIMELINE_H__

#include <gtk/gtk.h>
#include "screenshooter-video-editor-blur.h"

typedef void (*TimelineScrubCallback)  (gdouble timestamp, gpointer user_data);
typedef void (*TimelineRegionChanged)  (gpointer user_data);

typedef struct {
  GtkWidget *container;      /* main VBox */
  GtkWidget *slider;         /* GtkScale for timeline */
  GtkWidget *time_label;     /* current/total time display */
  GtkWidget *regions_box;    /* VBox listing blur regions */
  GtkWidget *blur_slider;    /* blur intensity GtkScale */
  GtkWidget *blur_label;     /* blur intensity value label */
  GtkWidget *fullframe_check;/* full frame checkbox */
  GtkWidget *add_button;     /* add region button */

  VideoEditorState *state;
  gdouble current_time;

  /* Debounce scrubbing */
  guint scrub_timeout_id;

  TimelineScrubCallback scrub_cb;
  gpointer scrub_data;
  TimelineRegionChanged region_changed_cb;
  gpointer region_changed_data;
} VideoEditorTimeline;

VideoEditorTimeline *video_editor_timeline_new        (VideoEditorState *state);
void                 video_editor_timeline_free       (VideoEditorTimeline *tl);
void                 video_editor_timeline_refresh    (VideoEditorTimeline *tl);
gdouble              video_editor_timeline_get_time   (VideoEditorTimeline *tl);
void                 video_editor_timeline_set_scrub_callback (
                                                       VideoEditorTimeline *tl,
                                                       TimelineScrubCallback cb,
                                                       gpointer user_data);
void                 video_editor_timeline_set_region_changed_callback (
                                                       VideoEditorTimeline *tl,
                                                       TimelineRegionChanged cb,
                                                       gpointer user_data);

#endif
