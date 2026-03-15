#ifndef __SC_VIDEO_EDITOR_BLUR_H__
#define __SC_VIDEO_EDITOR_BLUR_H__

#include <glib.h>

typedef struct {
  gint x, y, w, h;       /* video-space pixel coordinates (not widget-space) */
  gdouble start_time;     /* seconds */
  gdouble end_time;       /* seconds */
  gboolean full_frame;    /* if TRUE, x/y/w/h are ignored */
} BlurRegion;

typedef struct {
  gchar *input_path;
  gint video_width;       /* original video resolution */
  gint video_height;
  gdouble duration;       /* seconds */
  gdouble fps;
  gboolean has_audio;     /* whether input has an audio stream */
  GList *regions;         /* list of BlurRegion* */
  gint blur_radius;       /* boxblur luma_radius, 2-30 */
} VideoEditorState;

#define VIDEO_EDITOR_MAX_REGIONS   10
#define VIDEO_EDITOR_MIN_REGION_PX 10
#define VIDEO_EDITOR_MIN_BLUR      2
#define VIDEO_EDITOR_MAX_BLUR      30
#define VIDEO_EDITOR_DEFAULT_BLUR  10

VideoEditorState *video_editor_state_new         (const gchar *input_path);
void              video_editor_state_free        (VideoEditorState *state);

BlurRegion       *blur_region_new                (gint x, gint y, gint w, gint h,
                                                  gdouble start_time, gdouble end_time,
                                                  gboolean full_frame);
void              blur_region_free               (BlurRegion *region);
BlurRegion       *blur_region_copy               (const BlurRegion *region);

gboolean          video_editor_add_region        (VideoEditorState *state,
                                                  BlurRegion *region);
void              video_editor_remove_region     (VideoEditorState *state,
                                                  guint index);
void              video_editor_clamp_region      (BlurRegion *region,
                                                  gint video_width,
                                                  gint video_height);

gboolean          video_editor_probe_metadata    (VideoEditorState *state,
                                                  GError **error);

gchar            *video_editor_build_filter_chain (VideoEditorState *state);

gchar           **video_editor_build_ffmpeg_argv (VideoEditorState *state,
                                                  const gchar *output_path);
void              video_editor_free_argv         (gchar **argv);

gboolean          video_editor_save_config       (VideoEditorState *state,
                                                  const gchar *path,
                                                  GError **error);
gboolean          video_editor_load_config       (VideoEditorState *state,
                                                  const gchar *path,
                                                  GError **error);

#endif
