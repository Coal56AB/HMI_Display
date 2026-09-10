#ifndef DISPLAY_MODULE_H
#define DISPLAY_MODULE_H
#include <stdint.h>
typedef void (*DisplayWriteRect)(uint16_t,uint16_t,uint16_t,uint16_t,const uint16_t *,uint16_t,void *);
typedef int (*DisplayReadAssets)(uint32_t,void *,uint32_t,void *);
void display_init(DisplayWriteRect write);
void display_render(void);
void display_touch(int16_t x,int16_t y,uint8_t down,uint32_t now);
void display_poll(uint32_t now);
void display_receive(uint8_t byte);
void display_receive_error(void);
int display_assets_init(DisplayReadAssets read);
unsigned display_assets_error(void);
uint32_t display_assets_size(void);
void display_asset_loads_reset(void);
unsigned display_asset_loads(void);
void display_console_begin(unsigned y);
void display_console_pixel(int x,int y,unsigned size,uint16_t color);
const uint16_t *display_console_pixels(void);
/* Platform services supplied by the common STM32 application. */
void display_transport_send(const uint8_t *bytes,uint16_t length);
void display_system_reset(void);
#endif
