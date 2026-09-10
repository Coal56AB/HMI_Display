#pragma once
#include <stdint.h>
void song_store_start();
void song_store_feed(uint8_t byte);
void song_store_tick(uint32_t now);
bool song_store_action(unsigned action,unsigned slot,uint32_t position=0);
bool song_store_busy();
void song_store_boot(bool ready,bool pc_connected);
