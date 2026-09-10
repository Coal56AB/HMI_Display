#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// UI-task-only API; no concurrent calls from MIDI/USB tasks.
void music_box_live_state(uint32_t at, uint8_t connected, uint8_t enabled,
                          const uint8_t notes[6], const uint32_t frequencies[6]);
void music_box_live_ack(uint8_t sequence, uint8_t result);
void music_box_live_history_reset(uint32_t at);
#ifdef __cplusplus
}
#endif
