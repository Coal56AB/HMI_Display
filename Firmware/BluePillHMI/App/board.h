#ifndef BOARD_H
#define BOARD_H
#include "project_config.h"
#include <stdint.h>
#include "display_api.h"
extern const DisplayPlatform board_platform;
void board_lcd_init(void);
void board_boot_error(unsigned reason);
void board_boot_progress(unsigned stage,unsigned percent);
void board_write_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *pixels,uint16_t stride,void *user);
uint8_t board_touch(int16_t *x,int16_t *y);
/* Diagnostic read even with IRQ high; raw values are meaningful while pressed. */
uint8_t board_touch_sample(int16_t *x,int16_t *y,uint16_t *raw_x,uint16_t *raw_y);
int board_assets_read(uint32_t offset,void *dest,uint32_t length,void *user);
void board_assets_boot(void);
/* PCHR on UART1 requests NVIC_SystemReset. Poll also works with IRQs disabled. */
void board_reset_enable(void);
void board_reset_poll(void);
int board_flash_read(uint32_t at,void *data,uint32_t length);
int board_flash_write(uint32_t at,const void *data,uint32_t length);
int board_flash_erase(uint32_t at);
#endif
