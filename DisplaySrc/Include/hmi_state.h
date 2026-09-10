#ifndef HMI_STATE_H
#define HMI_STATE_H

#include <stdint.h>
#include "hmi_scene_generated.h"

#define HMI_GRAPH_CHANNELS 4u
#define HMI_JOURNAL_ITEMS 20u

typedef enum { HMI_PAGE_HOME, HMI_PAGE_GRAPHS, HMI_PAGE_PARAMETERS, HMI_PAGE_JOURNAL } HmiPage;
typedef enum { HMI_DRIVE_UF, HMI_DRIVE_SF, HMI_DRIVE_VF } HmiDriveMode;
typedef enum { HMI_POWER_OFF, HMI_POWER_CHARGE, HMI_POWER_READY, HMI_POWER_RUN,
               HMI_POWER_REGULATING, HMI_POWER_FAULT } HmiPowerState;
typedef enum { HMI_GRAPH_POWER, HMI_GRAPH_SPEED, HMI_GRAPH_LIMIT, HMI_GRAPH_D,
               HMI_GRAPH_Q } HmiGraphPage;
typedef enum { HMI_PARAM_SYSTEM, HMI_PARAM_INVERTER, HMI_PARAM_MOTOR,
               HMI_PARAM_PROTECTIONS, HMI_PARAM_COMMUNICATION,
               HMI_PARAM_CALIBRATION, HMI_PARAM_AUTO, HMI_PARAM_AUTO_RUNNING,
               HMI_PARAM_AUTO_DONE, HMI_PARAM_EQUIVALENT, HMI_PARAM_TEMPERATURE, HMI_PARAM_NETWORK } HmiParamSection;
typedef enum { HMI_DIALOG_NONE, HMI_DIALOG_KEYPAD, HMI_DIALOG_FILTER,
               HMI_DIALOG_CONFIRM, HMI_DIALOG_SENSOR_DISPLAY,
               HMI_DIALOG_SENSOR_SCHEMES, HMI_DIALOG_ROM,
               HMI_DIALOG_PANEL_CONTROL, HMI_DIALOG_PANEL_OUTPUT,
               HMI_DIALOG_PANEL_MOTOR, HMI_DIALOG_AXIS, HMI_DIALOG_TIME,
               HMI_DIALOG_HELP } HmiDialog;

typedef struct {
    const int16_t *samples;
    uint16_t sample_count;
    int16_t minimum;
    int16_t maximum;
    uint16_t color_rgb565;
    uint8_t visible;
    const char *label;
    const char *unit;
} HmiGraphChannel;

typedef struct { const char *timestamp; const char *type; const char *message; } HmiJournalItem;

typedef struct {
    HmiPage page;
    HmiDriveMode drive_mode;
    HmiPowerState power_state;
    HmiGraphPage graph_page;
    HmiParamSection param_section;
    HmiDialog dialog;
    HmiSceneId scene_override; /* HMI_SCENE_COUNT means select from fields above. */
    uint8_t help_context;
    uint8_t selected_control;
    uint8_t inverter_enabled;
    uint8_t regulation_active;
    uint8_t setpoint_matches;
    uint8_t dynamic_values; /* Draw numeric fields/metadata from this structure. */
    uint8_t sensor_mask; /* Local visibility only; never determines process status. */
    uint8_t visual_valid,visual[12]; /* off, normal, warning, fault; controller-owned. */
    uint8_t telemetry_flags; /* active=128, online=1, mains=2, bypass=4, passive=8, inverter discharge=16 */
    const char *clock;
    const char *run_state_label;

    float mains_voltage;
    float mains_frequency;
    float dc_bus_voltage;
    float precharge_current;
    float precharge_seconds;
    float temperature_rectifier;
    float temperature_precharge;
    float temperature_dc;
    float temperature_inverter;
    float temperature_motor;
    float modulation_set;
    float modulation_actual;
    float rotation_set;
    float rotation_actual;
    float current_limit_set;
    float current_limit_actual;
    float pending_setpoints[3]; /* modulation %, rotation Hz, current limit % */
    uint8_t control_warning_mask;
    uint8_t journal_warning;
    uint8_t journal_progress;
    uint8_t journal_paged;
    uint8_t pending_mask; /* bits 0..2: show pending above the applied setpoint */
    float output_voltage;
    float output_current;
    float output_power;
    float rotor_frequency;
    float stator_frequency;
    float slip;
    float motor_load;

    const char *control_labels[3];
    const char *control_units[3];
    const char *graph_tabs[5];
    const char *graph_title;
    HmiGraphChannel graph[HMI_GRAPH_CHANNELS];
    uint16_t graph_scale[HMI_GRAPH_CHANNELS]; /* integer samples per displayed unit; 0 means legacy 10 */
    uint32_t graph_division_ms;
    uint32_t graph_offset_ms;
    uint8_t graph_valid_count;
    uint8_t output_mask, motor_mask;
    uint8_t graph_cursor; /* 0: history; 1..240: live sweep position + 1 */
    const HmiJournalItem *journal;
    uint16_t journal_count;
    uint16_t journal_page;
} HmiState;

void hmi_state_defaults(HmiState *state);
HmiSceneId hmi_scene_for_state(const HmiState *state);

#endif
