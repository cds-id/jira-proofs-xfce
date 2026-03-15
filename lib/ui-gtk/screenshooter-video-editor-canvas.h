#ifndef __HAVE_VIDEO_EDITOR_CANVAS_H__
#define __HAVE_VIDEO_EDITOR_CANVAS_H__

#include <gtk/gtk.h>
#include <sc-video-editor-blur.h>

typedef void (*CanvasRegionDrawnCallback) (gint x, gint y, gint w, gint h,
                                           gpointer user_data);

typedef struct {
  GtkWidget *drawing_area;
  GdkPixbuf *frame_pixbuf;
  VideoEditorState *state;

  /* Coordinate mapping */
  gdouble scale;
  gdouble offset_x;
  gdouble offset_y;

  /* Drawing state */
  gboolean drawing;
  gdouble draw_start_x;
  gdouble draw_start_y;
  gdouble draw_current_x;
  gdouble draw_current_y;
  gboolean draw_enabled;

  /* Spinner while loading */
  gboolean loading;

  /* Current timeline position for time-aware overlay */
  gdouble current_time;

  CanvasRegionDrawnCallback region_drawn_cb;
  gpointer region_drawn_data;
} VideoEditorCanvas;

VideoEditorCanvas *video_editor_canvas_new          (VideoEditorState *state);
void               video_editor_canvas_free         (VideoEditorCanvas *canvas);
void               video_editor_canvas_set_pixbuf   (VideoEditorCanvas *canvas,
                                                     GdkPixbuf *pixbuf);
void               video_editor_canvas_set_loading  (VideoEditorCanvas *canvas,
                                                     gboolean loading);
void               video_editor_canvas_set_draw_enabled (VideoEditorCanvas *canvas,
                                                         gboolean enabled);
void               video_editor_canvas_set_region_drawn_callback (
                                                     VideoEditorCanvas *canvas,
                                                     CanvasRegionDrawnCallback cb,
                                                     gpointer user_data);
void               video_editor_canvas_set_time     (VideoEditorCanvas *canvas,
                                                     gdouble current_time);
void               video_editor_canvas_queue_draw   (VideoEditorCanvas *canvas);

#endif
