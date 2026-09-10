#pragma once
#include <stdint.h>
void esp_link_start();
int esp_link_send(const uint8_t *data, unsigned length);
int esp_link_read(uint8_t *data, unsigned capacity);
