#pragma once
#include <stdint.h>
// Producer: controller task. Consumer: UI task. No drawing on the producer.
namespace ui_bridge {
void drain();
void set_screen_ready(bool ready);
unsigned music_box_screen_ready();
void music_box_control_frame(const uint8_t *,unsigned);
void music_box_control_source(unsigned);
void music_box_control_connections(unsigned);
void music_box_midi_input(unsigned);
void music_box_midi_name(const char *);
void music_box_saved_song(unsigned,const char *,unsigned,uint32_t);
void music_box_save_progress(unsigned,unsigned);
void music_box_playback_error(const char *);
void music_box_saved_offset(uint32_t);
void music_box_saved_range(uint8_t,uint8_t);
void music_box_saved_playing(unsigned);
void music_box_saved_loading(unsigned);
}
