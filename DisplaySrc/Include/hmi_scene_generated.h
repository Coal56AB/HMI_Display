#ifndef HMI_SCENE_GENERATED_H
#define HMI_SCENE_GENERATED_H

#include <stdint.h>

typedef enum {
    HMI_SCENE_DIALOG_AXIS = 0,
    HMI_SCENE_DIALOG_CONFIRM = 1,
    HMI_SCENE_DIALOG_FILTER = 2,
    HMI_SCENE_DIALOG_KEYPAD = 3,
    HMI_SCENE_DIALOG_PANEL_CONTROL = 4,
    HMI_SCENE_DIALOG_PANEL_MOTOR = 5,
    HMI_SCENE_DIALOG_PANEL_OUTPUT = 6,
    HMI_SCENE_DIALOG_ROM = 7,
    HMI_SCENE_DIALOG_SENSOR_DISPLAY = 8,
    HMI_SCENE_DIALOG_SENSOR_SCHEMES = 9,
    HMI_SCENE_DIALOG_TIME = 10,
    HMI_SCENE_GRAPHS_DQD = 11,
    HMI_SCENE_GRAPHS_DQQ = 12,
    HMI_SCENE_GRAPHS_LIMIT = 13,
    HMI_SCENE_GRAPHS_POWER = 14,
    HMI_SCENE_GRAPHS_SPEED_SF = 15,
    HMI_SCENE_GRAPHS_SPEED_VF = 16,
    HMI_SCENE_GRAPHS_SPEED = 17,
    HMI_SCENE_HELP_GRAPHS = 18,
    HMI_SCENE_HELP_HOME = 19,
    HMI_SCENE_HELP_JOURNAL = 20,
    HMI_SCENE_HELP_PARAMS = 21,
    HMI_SCENE_HOME_CHARGE = 22,
    HMI_SCENE_HOME_DEFAULT = 23,
    HMI_SCENE_HOME_FAULT = 24,
    HMI_SCENE_HOME_OFF = 25,
    HMI_SCENE_HOME_READY = 26,
    HMI_SCENE_HOME_REGULATING = 27,
    HMI_SCENE_HOME_RUN = 28,
    HMI_SCENE_HOME_SELECT_FREQ = 29,
    HMI_SCENE_HOME_SELECT_LIMIT = 30,
    HMI_SCENE_HOME_SELECT_MOD = 31,
    HMI_SCENE_HOME_SF = 32,
    HMI_SCENE_HOME_TARGET_EQUAL = 33,
    HMI_SCENE_HOME_UF = 34,
    HMI_SCENE_HOME_VF = 35,
    HMI_SCENE_JOURNAL_PAGE1 = 36,
    HMI_SCENE_JOURNAL_PAGE2 = 37,
    HMI_SCENE_PARAMS_AUTO_DONE = 38,
    HMI_SCENE_PARAMS_AUTO_RUNNING = 39,
    HMI_SCENE_PARAMS_AUTO = 40,
    HMI_SCENE_PARAMS_CALIBRATION = 41,
    HMI_SCENE_PARAMS_COMMUNICATION = 42,
    HMI_SCENE_PARAMS_INVERTER = 43,
    HMI_SCENE_PARAMS_MOTOR = 44,
    HMI_SCENE_PARAMS_PROTECTIONS = 45,
    HMI_SCENE_PARAMS_SYSTEM = 46,
    HMI_SCENE_COUNT
} HmiSceneId;

typedef struct { uint16_t first_block; uint16_t block_count; const char *name; } HmiGeneratedScene;

extern const HmiGeneratedScene hmi_generated_scenes[HMI_SCENE_COUNT];
extern const uint8_t hmi_generated_primitive_data[];
extern const uint16_t hmi_generated_templates[];
extern const uint16_t hmi_generated_values[];
extern const uint8_t hmi_generated_primitive_offsets[];
extern const uint16_t hmi_generated_clip_rects[];
extern const uint16_t hmi_generated_block_commands[];
extern const uint16_t hmi_generated_block_offsets[];
extern const uint16_t hmi_generated_block_bounds[];
extern const uint16_t hmi_generated_scene_blocks[];
extern const uint8_t hmi_generated_strings[];
extern const uint32_t hmi_generated_data_bytes;

#endif
