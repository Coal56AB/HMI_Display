#pragma once
#include <stdint.h>
#include "live_controller.h"
void midi_app_start();
void midi_app_tick(uint32_t now);
void midi_app_send(const uint8_t *bytes, uint16_t count);
void app_midi_snapshot(const live::Snapshot &snapshot, bool reset_history);
void app_midi_ack(uint8_t sequence, uint8_t result);
