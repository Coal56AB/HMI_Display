#pragma once
// ESP32-S3: GPIO1..13 fit the compact board. Native USB uses 19/20, UART0 43/44.
#define HMI_SPI_SCLK 12
#define HMI_SPI_MOSI 11
#define HMI_SPI_MISO 13
#define HMI_LCD_CS 10
#define HMI_LCD_DC 9
#define HMI_LCD_RST 6
#define HMI_TOUCH_CS 8
#define HMI_TOUCH_IRQ 7
#define HMI_LCD_RPI_ADAPTER 1
// Start at 20 MHz; 40 MHz may be tested after checking wiring/display stability.
#ifndef HMI_LCD_CLOCK_HZ
#define HMI_LCD_CLOCK_HZ 20000000
#endif
#define HMI_LCD_MADCTL 0x28
#define HMI_TOUCH_CLOCK_HZ 1000000
#define HMI_TOUCH_X_MIN 200
#define HMI_TOUCH_X_MAX 3900
#define HMI_TOUCH_Y_MIN 200
#define HMI_TOUCH_Y_MAX 3900
#define HMI_TOUCH_SWAP_XY 1
#define HMI_TOUCH_INVERT_X 1
#define HMI_TOUCH_INVERT_Y 1
#define HMI_TOUCH_MAX_SPREAD 80
#define HMI_TOUCH_POLL_MS 10
#define HMI_DMA_PIXELS (480 * 16)
