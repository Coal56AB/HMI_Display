#ifndef HMI_H
#define HMI_H

#include "hmi_backend.h"
#include "hmi_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void hmi_init(void);
void hmi_invalidate(HmiRect rect);
void hmi_invalidate_all(void);
void hmi_diff_and_invalidate(const HmiState *old_state,
                             const HmiState *new_state);
void hmi_render_dirty(const HmiState *state, HmiFlushRectFn flush, void *user);
/* Called after the scene for each buffer fragment, before synchronous flush. */
typedef void (*HmiPaintFn)(void *user);
int hmi_dirty_pending(void);
void hmi_render_dirty_ex(const HmiState *state, HmiFlushRectFn flush, void *user,
                         HmiPaintFn paint, void *paint_user);
void hmi_render_full(const HmiState *state, HmiFlushRectFn flush, void *user);
uint32_t hmi_dirty_pixel_count(void);
uint32_t hmi_static_data_bytes(void);
uint32_t hmi_working_ram_bytes(void);

#ifdef __cplusplus
}
#endif
#endif
