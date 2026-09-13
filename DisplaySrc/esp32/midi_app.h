#pragma once
#include <stdint.h>
void midi_app_start();
void midi_app_stop();
bool midi_app_stopped();
bool midi_app_connected();
void midi_app_ack(uint8_t sequence,uint8_t result);
