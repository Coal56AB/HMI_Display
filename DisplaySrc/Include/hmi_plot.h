#ifndef HMI_PLOT_H
#define HMI_PLOT_H
#include "hmi_state.h"

#define HMI_PLOT_POINTS 240u
/* Exact screen coordinates of the last completed live plot. No sample
 * pointers: telemetry is allowed to overwrite its buffers in place. */
typedef struct {
    uint8_t y[HMI_GRAPH_CHANNELS][HMI_PLOT_POINTS];
    uint16_t count[HMI_GRAPH_CHANNELS],color[HMI_GRAPH_CHANNELS],width;
    uint8_t cursor,valid_count;
} HmiPlotImage;
#define HMI_PLOT_DIFF_PIXELS 712u
typedef struct {
    uint16_t pixels[HMI_PLOT_DIFF_PIXELS];
    HmiPlotImage previous;
    uint8_t old[(HMI_PLOT_DIFF_PIXELS+1u)/2u];
    uint8_t changed[(HMI_PLOT_DIFF_PIXELS+7u)/8u];
} HmiPlotWorkspace;
HmiPlotWorkspace *ui_plot_workspace(void);
void hmi_plot_forget(void);
uint32_t ui_working_buffer_bytes(void);
int hmi_plot_capture(const HmiState *state,HmiPlotImage *image);
void hmi_plot_draw(const HmiPlotImage *image);
#endif
