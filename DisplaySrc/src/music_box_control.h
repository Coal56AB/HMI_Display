#pragma once
#include <stdint.h>
#include "display_api.h"
#ifdef __cplusplus
extern "C" {
#endif
// Complete validated controller frames, UI task only. Never feed partial packets here.
void music_box_boot_error(const DisplayPlatform *platform);
void music_box_control_frame(const uint8_t *frame, unsigned length);
void music_box_control_source(unsigned source); /* 0 offline, 1 UART, 2 USB */
/* Independent indicators: bit 0 UART ready, bit 1 USB PC, bit 2 MIDI device. */
void music_box_control_connections(unsigned status);
void music_box_midi_input(unsigned running);
void music_box_saved_song(unsigned slot,const char *title,unsigned present,uint32_t duration);
void music_box_save_progress(unsigned stage,unsigned percent);
unsigned music_box_screen_ready(void);
void music_box_playback_error(const char *reason);
void music_box_saved_offset(uint32_t offset);
void music_box_saved_range(uint8_t low,uint8_t high);
void music_box_saved_playing(unsigned playing);
/* Preparation is visible until STM acknowledges STREAM_START; stop clears it. */
void music_box_saved_loading(unsigned loading);
#ifdef __cplusplus
}
#endif
