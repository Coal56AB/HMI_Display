#pragma once
#include "hmi_storage.h"
#define HMI_ASSET_LENGTH 1200045u
#define HMI_ASSET_CRC 0x6e9398e6u
#if HMI_STORAGE_VIRTUAL
#define hmi_generated_primitive_data ((const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+16u))
#define hmi_generated_primitive_offsets ((const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+57756u))
#define hmi_generated_templates ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+86550u))
#define hmi_generated_values ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+93942u))
#define hmi_generated_clip_rects ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+94452u))
#define hmi_generated_block_commands ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+94628u))
#define hmi_generated_block_offsets ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+119076u))
#define hmi_generated_scene_blocks ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+120874u))
#define hmi_generated_block_bounds ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+130810u))
#define hmi_generated_strings ((const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+134402u))
#define UI_GLYPHS ((const ui_glyph_t *)(uintptr_t)(HMI_ASSET_BASE+139890u))
#define UI_FONT_BITS ((const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+173682u))
#endif
