#include "screenshooter-video-editor-canvas.h"

#include <math.h>

#define CANVAS_MIN_WIDTH  640
#define CANVAS_MIN_HEIGHT 360


static void
canvas_compute_mapping (VideoEditorCanvas *canvas, gint widget_w, gint widget_h)
{
  if (canvas->state->video_width <= 0 || canvas->state->video_height <= 0)
    return;

  gdouble scale_x = (gdouble) widget_w / canvas->state->video_width;
  gdouble scale_y = (gdouble) widget_h / canvas->state->video_height;
  canvas->scale = MIN (scale_x, scale_y);
  canvas->offset_x = (widget_w - canvas->state->video_width * canvas->scale) / 2.0;
  canvas->offset_y = (widget_h - canvas->state->video_height * canvas->scale) / 2.0;
}


static void
widget_to_video (VideoEditorCanvas *canvas, gdouble wx, gdouble wy,
                 gdouble *vx, gdouble *vy)
{
  *vx = (wx - canvas->offset_x) / canvas->scale;
  *vy = (wy - canvas->offset_y) / canvas->scale;
}


static void
video_to_widget (VideoEditorCanvas *canvas, gdouble vx, gdouble vy,
                 gdouble *wx, gdouble *wy)
{
  *wx = vx * canvas->scale + canvas->offset_x;
  *wy = vy * canvas->scale + canvas->offset_y;
}


static gboolean
point_in_frame (VideoEditorCanvas *canvas, gdouble wx, gdouble wy)
{
  gdouble vx, vy;
  widget_to_video (canvas, wx, wy, &vx, &vy);
  return (vx >= 0 && vx < canvas->state->video_width &&
          vy >= 0 && vy < canvas->state->video_height);
}


static gboolean
cb_draw (GtkWidget *widget, cairo_t *cr, VideoEditorCanvas *canvas)
{
  gint widget_w = gtk_widget_get_allocated_width (widget);
  gint widget_h = gtk_widget_get_allocated_height (widget);

  canvas_compute_mapping (canvas, widget_w, widget_h);

  /* Dark background */
  cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
  cairo_paint (cr);

  /* Draw the video frame */
  if (canvas->frame_pixbuf != NULL)
    {
      gdouble frame_w = canvas->state->video_width * canvas->scale;
      gdouble frame_h = canvas->state->video_height * canvas->scale;

      cairo_save (cr);
      cairo_translate (cr, canvas->offset_x, canvas->offset_y);
      cairo_scale (cr,
                   frame_w / gdk_pixbuf_get_width (canvas->frame_pixbuf),
                   frame_h / gdk_pixbuf_get_height (canvas->frame_pixbuf));
      gdk_cairo_set_source_pixbuf (cr, canvas->frame_pixbuf, 0, 0);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  else if (canvas->loading)
    {
      /* Show "Loading..." text centered */
      cairo_set_source_rgb (cr, 0.7, 0.7, 0.7);
      cairo_select_font_face (cr, "sans-serif",
                              CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size (cr, 16);
      cairo_text_extents_t extents;
      cairo_text_extents (cr, "Loading...", &extents);
      cairo_move_to (cr,
                     (widget_w - extents.width) / 2,
                     (widget_h + extents.height) / 2);
      cairo_show_text (cr, "Loading...");
    }

  /* Draw existing blur region overlays (only if active at current time) */
  for (GList *l = canvas->state->regions; l != NULL; l = l->next)
    {
      BlurRegion *r = l->data;
      gdouble wx1, wy1, wx2, wy2;
      gboolean active = (canvas->current_time >= r->start_time &&
                         canvas->current_time <= r->end_time);

      if (r->full_frame)
        {
          video_to_widget (canvas, 0, 0, &wx1, &wy1);
          video_to_widget (canvas, canvas->state->video_width,
                          canvas->state->video_height, &wx2, &wy2);
        }
      else
        {
          video_to_widget (canvas, r->x, r->y, &wx1, &wy1);
          video_to_widget (canvas, r->x + r->w, r->y + r->h, &wx2, &wy2);
        }

      /* Active regions: bright blue; inactive: dim grey */
      if (active)
        {
          cairo_set_source_rgba (cr, 0.2, 0.5, 1.0, 0.25);
          cairo_rectangle (cr, wx1, wy1, wx2 - wx1, wy2 - wy1);
          cairo_fill (cr);
          cairo_set_source_rgba (cr, 0.2, 0.5, 1.0, 0.8);
        }
      else
        {
          cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.15);
          cairo_rectangle (cr, wx1, wy1, wx2 - wx1, wy2 - wy1);
          cairo_fill (cr);
          cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.4);
        }

      cairo_set_line_width (cr, 2.0);
      cairo_rectangle (cr, wx1, wy1, wx2 - wx1, wy2 - wy1);
      cairo_stroke (cr);
    }

  /* Draw the in-progress rectangle */
  if (canvas->drawing)
    {
      gdouble x = MIN (canvas->draw_start_x, canvas->draw_current_x);
      gdouble y = MIN (canvas->draw_start_y, canvas->draw_current_y);
      gdouble w = fabs (canvas->draw_current_x - canvas->draw_start_x);
      gdouble h = fabs (canvas->draw_current_y - canvas->draw_start_y);

      cairo_set_source_rgba (cr, 1.0, 0.3, 0.3, 0.3);
      cairo_rectangle (cr, x, y, w, h);
      cairo_fill (cr);

      cairo_set_source_rgba (cr, 1.0, 0.3, 0.3, 0.9);
      cairo_set_line_width (cr, 2.0);
      cairo_set_dash (cr, (double[]){6, 4}, 2, 0);
      cairo_rectangle (cr, x, y, w, h);
      cairo_stroke (cr);
    }

  return TRUE;
}


static gboolean
cb_button_press (GtkWidget *widget, GdkEventButton *event, VideoEditorCanvas *canvas)
{
  if (event->button != 1 || !canvas->draw_enabled)
    return FALSE;

  if (!point_in_frame (canvas, event->x, event->y))
    return FALSE;

  canvas->drawing = TRUE;
  canvas->draw_start_x = event->x;
  canvas->draw_start_y = event->y;
  canvas->draw_current_x = event->x;
  canvas->draw_current_y = event->y;

  return TRUE;
}


static gboolean
cb_motion_notify (GtkWidget *widget, GdkEventMotion *event, VideoEditorCanvas *canvas)
{
  if (!canvas->drawing)
    return FALSE;

  canvas->draw_current_x = event->x;
  canvas->draw_current_y = event->y;
  gtk_widget_queue_draw (canvas->drawing_area);

  return TRUE;
}


static gboolean
cb_button_release (GtkWidget *widget, GdkEventButton *event, VideoEditorCanvas *canvas)
{
  if (event->button != 1 || !canvas->drawing)
    return FALSE;

  canvas->drawing = FALSE;
  canvas->draw_current_x = event->x;
  canvas->draw_current_y = event->y;

  /* Convert widget-space rectangle to video-space */
  gdouble wx = MIN (canvas->draw_start_x, canvas->draw_current_x);
  gdouble wy = MIN (canvas->draw_start_y, canvas->draw_current_y);
  gdouble ww = fabs (canvas->draw_current_x - canvas->draw_start_x);
  gdouble wh = fabs (canvas->draw_current_y - canvas->draw_start_y);

  gdouble vx1, vy1, vx2, vy2;
  widget_to_video (canvas, wx, wy, &vx1, &vy1);
  widget_to_video (canvas, wx + ww, wy + wh, &vx2, &vy2);

  gint rx = (gint) CLAMP (vx1, 0, canvas->state->video_width);
  gint ry = (gint) CLAMP (vy1, 0, canvas->state->video_height);
  gint rw = (gint) CLAMP (vx2 - vx1, 0, canvas->state->video_width - rx);
  gint rh = (gint) CLAMP (vy2 - vy1, 0, canvas->state->video_height - ry);

  /* Minimum size check */
  if (rw >= VIDEO_EDITOR_MIN_REGION_PX && rh >= VIDEO_EDITOR_MIN_REGION_PX)
    {
      if (canvas->region_drawn_cb)
        canvas->region_drawn_cb (rx, ry, rw, rh, canvas->region_drawn_data);
    }

  gtk_widget_queue_draw (canvas->drawing_area);
  return TRUE;
}


VideoEditorCanvas *
video_editor_canvas_new (VideoEditorState *state)
{
  VideoEditorCanvas *canvas = g_new0 (VideoEditorCanvas, 1);
  canvas->state = state;
  canvas->draw_enabled = TRUE;
  canvas->loading = FALSE;

  canvas->drawing_area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (canvas->drawing_area, CANVAS_MIN_WIDTH, CANVAS_MIN_HEIGHT);
  gtk_widget_set_hexpand (canvas->drawing_area, TRUE);
  gtk_widget_set_vexpand (canvas->drawing_area, TRUE);

  gtk_widget_add_events (canvas->drawing_area,
                         GDK_BUTTON_PRESS_MASK |
                         GDK_BUTTON_RELEASE_MASK |
                         GDK_POINTER_MOTION_MASK);

  g_signal_connect (canvas->drawing_area, "draw",
                    G_CALLBACK (cb_draw), canvas);
  g_signal_connect (canvas->drawing_area, "button-press-event",
                    G_CALLBACK (cb_button_press), canvas);
  g_signal_connect (canvas->drawing_area, "motion-notify-event",
                    G_CALLBACK (cb_motion_notify), canvas);
  g_signal_connect (canvas->drawing_area, "button-release-event",
                    G_CALLBACK (cb_button_release), canvas);

  return canvas;
}


void
video_editor_canvas_free (VideoEditorCanvas *canvas)
{
  if (canvas == NULL)
    return;
  if (canvas->frame_pixbuf)
    g_object_unref (canvas->frame_pixbuf);
  g_free (canvas);
}


void
video_editor_canvas_set_pixbuf (VideoEditorCanvas *canvas, GdkPixbuf *pixbuf)
{
  if (canvas->frame_pixbuf)
    g_object_unref (canvas->frame_pixbuf);
  canvas->frame_pixbuf = pixbuf ? g_object_ref (pixbuf) : NULL;
  canvas->loading = FALSE;
  gtk_widget_queue_draw (canvas->drawing_area);
}


void
video_editor_canvas_set_loading (VideoEditorCanvas *canvas, gboolean loading)
{
  canvas->loading = loading;
  gtk_widget_queue_draw (canvas->drawing_area);
}


void
video_editor_canvas_set_draw_enabled (VideoEditorCanvas *canvas, gboolean enabled)
{
  canvas->draw_enabled = enabled;
}


void
video_editor_canvas_set_region_drawn_callback (VideoEditorCanvas *canvas,
                                               CanvasRegionDrawnCallback cb,
                                               gpointer user_data)
{
  canvas->region_drawn_cb = cb;
  canvas->region_drawn_data = user_data;
}


void
video_editor_canvas_set_time (VideoEditorCanvas *canvas, gdouble current_time)
{
  canvas->current_time = current_time;
  gtk_widget_queue_draw (canvas->drawing_area);
}


void
video_editor_canvas_queue_draw (VideoEditorCanvas *canvas)
{
  gtk_widget_queue_draw (canvas->drawing_area);
}
