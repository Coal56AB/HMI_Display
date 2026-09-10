#pragma once

/* LCD: 1 = Raspberry Pi adapter, RGB565; 0 = bare ILI9486 SPI, RGB666. */
#define LCD_RPI_ADAPTER 1

/* 1: static bars and a touch marker (Watch: app_debug), without assets.
 * 0: normal UI. PC13 lights during startup, then blinks in the main loop. */
#ifndef LCD_TEST_PATTERN
#define LCD_TEST_PATTERN 0
#endif

/* XPT2046 calibration, applied after optional axis swap. Raw ADC: 0..4095.
 * Replace these initial limits with measured values from your touch panel. */
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3900
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 1
#define TOUCH_INVERT_Y 1

/* Maximum spread of the middle three ADC samples (about 10 screen pixels).
 * Reject unstable contacts before mapping coordinates to a button. */
#define TOUCH_MAX_SPREAD 80u

/* SPI1 clock is 72 MHz: /64 gives 1.125 MHz while reading the touch panel.
 * LCD/Flash SPI1 and UART clocks and pin assignments are configured in CubeMX (.ioc). */
#define TOUCH_SPI_PRESCALER SPI_BAUDRATEPRESCALER_64
#define UI_POLL_INTERVAL_MS 10u
#define FLASH_BOOT_WINDOW_MS 2000u

#if TOUCH_X_MIN < 0 || TOUCH_X_MAX > 4095 || TOUCH_X_MIN >= TOUCH_X_MAX
#error "Touch X limits must satisfy 0 <= MIN < MAX <= 4095"
#endif
#if TOUCH_Y_MIN < 0 || TOUCH_Y_MAX > 4095 || TOUCH_Y_MIN >= TOUCH_Y_MAX
#error "Touch Y limits must satisfy 0 <= MIN < MAX <= 4095"
#endif
#if (LCD_RPI_ADAPTER != 0 && LCD_RPI_ADAPTER != 1) || \
    (LCD_TEST_PATTERN != 0 && LCD_TEST_PATTERN != 1) || \
    (TOUCH_SWAP_XY != 0 && TOUCH_SWAP_XY != 1) || \
    (TOUCH_INVERT_X != 0 && TOUCH_INVERT_X != 1) || \
    (TOUCH_INVERT_Y != 0 && TOUCH_INVERT_Y != 1)
#error "LCD and touch orientation switches must be 0 or 1"
#endif
#if UI_POLL_INTERVAL_MS == 0 || FLASH_BOOT_WINDOW_MS == 0
#error "Polling interval and flash boot window must be positive"
#endif
