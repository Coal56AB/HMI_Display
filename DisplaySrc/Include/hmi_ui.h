#ifndef HMI_UI_H
#define HMI_UI_H

#include "hmi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UI owns presentation and requested settings. The application owns hardware,
 * transport, telemetry and acceptance/rejection of commands. No HAL dependency.
 * All calls run in one foreground task; never call from an interrupt. */
#define HMI_UI_EVENT_CAPACITY 16u
#define HMI_UI_PARAMETER_CAPACITY 56u
#define HMI_UI_DEBOUNCE_MS 20u

typedef enum {
    HMI_VALUE_MODULATION, HMI_VALUE_ROTATION_HZ, HMI_VALUE_CURRENT_LIMIT,
    HMI_VALUE_MAINS_VOLTAGE, HMI_VALUE_MAINS_FREQUENCY, HMI_VALUE_BRIGHTNESS,
    HMI_VALUE_PWM_KHZ, HMI_VALUE_DEADTIME_US, HMI_VALUE_MAX_FREQUENCY,
    HMI_VALUE_MOTOR_POWER_KW, HMI_VALUE_MOTOR_VOLTAGE, HMI_VALUE_MOTOR_CURRENT,
    HMI_VALUE_MOTOR_FREQUENCY, HMI_VALUE_MOTOR_RPM, HMI_VALUE_POLE_PAIRS,
    HMI_VALUE_EFFICIENCY, HMI_VALUE_POWER_FACTOR,
    HMI_VALUE_MAX_CURRENT_PU, HMI_VALUE_DC_HIGH_PU, HMI_VALUE_DC_LOW_PU,
    HMI_VALUE_MAX_SPEED_PU, HMI_VALUE_MAX_POWER_PU,
    HMI_VALUE_INVERTER_TEMP_PU, HMI_VALUE_MOTOR_TEMP_PU,
    HMI_VALUE_MODBUS_ADDRESS, HMI_VALUE_BAUD, HMI_VALUE_TIMEOUT_MS,
    HMI_VALUE_GAIN_A, HMI_VALUE_GAIN_B, HMI_VALUE_GAIN_C, HMI_VALUE_GAIN_DC,
    HMI_VALUE_CURRENT_OFFSET,
    HMI_VALUE_AXIS_I_MIN, HMI_VALUE_AXIS_I_MAX, HMI_VALUE_AXIS_U_MIN, HMI_VALUE_AXIS_U_MAX,
    HMI_VALUE_MOD_MIN, HMI_VALUE_MOD_MAX, HMI_VALUE_ROT_MIN, HMI_VALUE_ROT_MAX,
    HMI_VALUE_LIMIT_MIN, HMI_VALUE_LIMIT_MAX,
    HMI_VALUE_JOURNAL_DATE,
    HMI_VALUE_RS,HMI_VALUE_RR,HMI_VALUE_LLS,HMI_VALUE_LLR,HMI_VALUE_LM,
    HMI_VALUE_V_WARN_LOW,HMI_VALUE_V_WARN_HIGH,HMI_VALUE_V_FAULT_LOW,HMI_VALUE_V_FAULT_HIGH,
    HMI_VALUE_F_WARN_LOW,HMI_VALUE_F_WARN_HIGH,HMI_VALUE_F_FAULT_LOW,HMI_VALUE_F_FAULT_HIGH,
    HMI_VALUE_COUNT
} HmiValueId;

typedef enum {
    HMI_ACTION_NONE, HMI_ACTION_PAGE, HMI_ACTION_SECTION, HMI_ACTION_DIALOG,
    HMI_ACTION_CLOSE, HMI_ACTION_SELECT_CONTROL, HMI_ACTION_EDIT,
    HMI_ACTION_KEY, HMI_ACTION_APPLY, HMI_ACTION_CANCEL,
    HMI_ACTION_GRAPH, HMI_ACTION_GRAPH_ZOOM, HMI_ACTION_GRAPH_RUN,
    HMI_ACTION_JOURNAL_PAGE, HMI_ACTION_JOURNAL_EXPORT,
    HMI_ACTION_JOURNAL_CLEAR, HMI_ACTION_FILTER, HMI_ACTION_TOGGLE,
    HMI_ACTION_MODE, HMI_ACTION_TIME_PART, HMI_ACTION_ROM,
    HMI_ACTION_AUTO_START, HMI_ACTION_AUTO_CANCEL, HMI_ACTION_CUSTOM
} HmiAction;

typedef enum {
    HMI_EVENT_NAVIGATED, HMI_EVENT_VALUE_CHANGED, HMI_EVENT_SETTINGS_CHANGED,
    HMI_EVENT_GRAPH_CHANGED, HMI_EVENT_JOURNAL_EXPORT, HMI_EVENT_JOURNAL_CLEAR,
    HMI_EVENT_TIME_CHANGED, HMI_EVENT_ROM_CHANGED,
    HMI_EVENT_AUTO_START, HMI_EVENT_AUTO_CANCEL, HMI_EVENT_CUSTOM
} HmiEventType;

/* IDs are stable protocol-facing UI identifiers, not Modbus addresses.
 * Parameters 0..2 are setpoints; remaining IDs are defined in hmi_ui.c. */
typedef struct {
    uint32_t sequence;
    HmiEventType type;
    uint16_t id;
    float value;
    uint32_t flags;
    uint8_t data[8];
} HmiEvent;

typedef struct {
    HmiRect rect;
    HmiAction action;
    int16_t argument;
} HmiHit;

typedef struct {
    HmiSceneId scene; /* HMI_SCENE_COUNT means every scene. */
    HmiHit hit;
} HmiTouchRegion;

typedef struct {
    HmiFlushRectFn write_rect;
    void *user;
} HmiDisplay;

typedef struct {
    HmiState state;
    HmiDisplay display;
    float parameters[HMI_UI_PARAMETER_CAPACITY];
    uint32_t settings[6]; /* control, output, motor, sensors, journal filter, axes */
    uint32_t draft_flags;
    float draft_limits[10]; /* axis and control limits; committed by Save */
    uint8_t choices[4]; /* PWM mode, motor connection, parity, language request */
    uint8_t units[3][4],draft_units[4];
    uint32_t graph_time_ms;
    uint8_t graph_running,auto_progress;
    uint8_t rom[5][8];
    char clock[6];
    char edit[24];
    uint8_t edit_length, edit_fresh, edit_error, edit_time_part, edit_rom;
    uint16_t edit_parameter;
    HmiDialog return_dialog;
    HmiEvent events[HMI_UI_EVENT_CAPACITY],last_event;
    uint8_t event_head,event_count;
    uint32_t event_overflows;
    uint8_t touching,touch_cancelled;
    int16_t touch_x,touch_y;
    uint32_t touch_started;
    uint32_t last_tap_ms;
    int16_t last_tap_x,last_tap_y,help_scroll,help_drag_y;
    uint8_t last_tap_card,help_dragging;
    HmiHit pressed;
    HmiSceneId pressed_scene;
    const HmiTouchRegion *regions;
    uint16_t region_count;
} HmiUi;

void hmi_ui_init(HmiUi *ui,HmiDisplay display);
/* Input is calibrated, rotated logical 320x480 pixels; pressed=0 on release.
 * Call regularly (e.g. every 10 ms). Outside-screen samples cancel the gesture. */
void hmi_ui_touch(HmiUi *ui,int16_t x,int16_t y,uint8_t pressed,uint32_t now_ms);
int hmi_ui_hit_test(const HmiUi *ui,int16_t x,int16_t y,HmiHit *hit);
void hmi_ui_prepare_text_layers(HmiUi *ui);
void hmi_ui_render(HmiUi *ui);
int hmi_ui_poll_event(HmiUi *ui,HmiEvent *event);
/* Optional application-defined regions take precedence over built-in controls. */
void hmi_ui_set_regions(HmiUi *ui,const HmiTouchRegion *regions,uint16_t count);
/* Non-touch input (encoder/buttons) can use the same controller. */
void hmi_ui_dispatch(HmiUi *ui,HmiAction action,int16_t argument);

#ifdef __cplusplus
}
#endif
#endif
