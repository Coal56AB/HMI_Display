#pragma once
#include "hmi_storage.h"
#define HMI_ASSET_LENGTH 56049u
#define HMI_ASSET_CRC 0x16b994eeu
#if HMI_STORAGE_VIRTUAL
#define hmi_generated_primitive_data ((const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+16u))
#define hmi_generated_primitive_offsets ((const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+13838u))
#define hmi_generated_templates ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+19088u))
#define hmi_generated_values ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+25226u))
#define hmi_generated_clip_rects ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+25736u))
#define hmi_generated_block_commands ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+25912u))
#define hmi_generated_block_offsets ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+29864u))
#define hmi_generated_scene_blocks ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+31662u))
#define hmi_generated_block_bounds ((const uint16_t *)(uintptr_t)(HMI_ASSET_BASE+41598u))
#define hmi_generated_strings ((const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+45190u))
#define UI_GLYPHS ((const ui_glyph_t *)(uintptr_t)(HMI_ASSET_BASE+50678u))
#define UI_FONT_BITS ((const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+52214u))
#endif
