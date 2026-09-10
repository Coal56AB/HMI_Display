#ifndef TELEMETRY_H
#define TELEMETRY_H
#include "hmi_ui.h"
/* ISR only queues bytes. All parsing and UI writes happen between renders. */
void telemetry_init(HmiUi *ui);
void telemetry_byte(uint8_t byte);
void telemetry_rx_error(void);
void telemetry_poll(HmiUi *ui,uint32_t now);
void telemetry_send(const uint8_t *bytes,uint16_t length);
void telemetry_selected(uint32_t now);
void telemetry_notice(unsigned code,float value);
void telemetry_clock(uint32_t stamp);
#endif
