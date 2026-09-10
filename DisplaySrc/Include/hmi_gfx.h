#ifndef HMI_GFX_H
#define HMI_GFX_H

#include <stdint.h>
#include "hmi_config.h"

#define HMI_WIDTH 320
#define HMI_HEIGHT 480
#ifndef HMI_RENDER_BUFFER_PIXELS
#if HMI_EXTERNAL_ASSETS
#define HMI_RENDER_BUFFER_PIXELS 2560u /* Eight full rows per transfer. */
#else
#define HMI_RENDER_BUFFER_PIXELS 2048u
#endif
#endif
#if HMI_RENDER_BUFFER_PIXELS < HMI_WIDTH
#error "HMI_RENDER_BUFFER_PIXELS must hold at least one full display row"
#endif
#define HMI_RENDER_BUFFER_BYTES (HMI_RENDER_BUFFER_PIXELS * 2u)

int ui_draw_raster_scene(unsigned scene);
void ui_draw_header_help(void);
void ui_text_layers_begin(int home);
void ui_text_layers_end(void);
void ui_text_replace(int x,int y,int w,int h);
int ui_static_text_hidden(int x,int baseline);
int ui_text_layers_home(void);
uint32_t ui_strip_ptr(void);
const uint16_t *ui_strip_data(void);
/* Recolor a flat raster background without covering its symbols or border. */
void ui_replace_color_rect(int x,int y,int w,int h,uint16_t from,uint16_t to);
void ui_set_live_graph_background(int enabled);
int ui_live_graph_background(void);
uint32_t ui_text_ptr(void);
int ui_strip_bytes(void);
int ui_strip_height(void);
void ui_begin(int y0, int rows, uint16_t background_rgb565);
void ui_begin_rect(int x, int y, int width, int height,
                   uint16_t background_rgb565);
int ui_buffer_width(void);
int ui_buffer_height(void);
/* Intersects the current render tile, independent of the mutable drawing clip. */
int ui_rect_visible(int x,int y,int w,int h);
uint32_t ui_font_data_bytes(void);
void ui_dim_content(void);
void ui_set_clip(int x, int y, int w, int h);
void ui_fill_rect(int x, int y, int w, int h, uint16_t color);
void ui_fill_gradient_v(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
void ui_line(int x0, int y0, int x1, int y1, uint16_t color);
void ui_line_aa(int x0, int y0, int x1, int y1, uint16_t color);
void ui_line_width(int x0, int y0, int x1, int y1, int width, uint16_t color);
void ui_rect(int x, int y, int w, int h, uint16_t color);
void ui_fill_round_rect(int x, int y, int w, int h, int radius, uint16_t color);
void ui_round_rect(int x, int y, int w, int h, int radius, uint16_t color);
void ui_circle(int cx, int cy, int radius, uint16_t color, int filled);
void ui_circle_width(int cx, int cy, int radius, int width, uint16_t color);
void ui_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
int ui_measure_text(int px, int utf8_length);
void ui_text(int x, int baseline, int px, uint16_t color, int utf8_length);
void ui_text_weight(int x, int baseline, int px, uint16_t color, int utf8_length, int bold);
void ui_text_weight_fit(int x, int baseline, int px, uint16_t color, int utf8_length, int bold, int target_width);
int ui_measure_cstr(int px, const char *utf8);
void ui_text_cstr(int x, int baseline, int px, uint16_t color, const char *utf8, int bold);
void ui_text_fit_cstr(int x, int baseline, int px, uint16_t color,
                      const char *utf8, int bold, int target_width);

#endif
