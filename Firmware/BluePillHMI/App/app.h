#ifndef APP_H
#define APP_H
#include "spi.h"
/* Inspect app_debug.system.boot_stage if startup stops. */
enum AppBootStage {
    APP_BOOT_RESET, APP_BOOT_LCD, APP_BOOT_ASSETS, APP_BOOT_RENDER, APP_BOOT_READY
};
enum AppLoaderStage {
    LOADER_DISABLED, LOADER_WAIT, LOADER_NO_COMMAND, LOADER_LENGTH,
    LOADER_ERASE, LOADER_PROGRAM, LOADER_VERIFY, LOADER_DONE,
    LOADER_UART_ERROR, LOADER_BAD_LENGTH, LOADER_SPI_ERROR, LOADER_BAD_ID
};
/* Expand only the relevant group in Watch. All counters are retained in
 * optimized builds. SPI points to the live HAL handle, not a snapshot. */
typedef struct {
    enum AppBootStage boot_stage;
    SPI_HandleTypeDef *spi;
    uint32_t tick_ms, clock_hz;
    uint8_t test_enabled;
} AppDebugSystem;

typedef struct {
    /* Last nonempty render; preserved while idle. Times use SysTick ms.
     * During asset initialization flash counters cover the CRC scan; reset
     * before rendering. asset_loads means reads (external) or inflations (lite). */
    uint32_t total_ms, lcd_ms, flash_ms, asset_loads, flash_bytes;
    uint32_t flash_cycles; /* DWT sum before ms conversion; reset per render. */
} AppDebugRender;

typedef struct {
    uint32_t frames, writes; /* Cumulative since reset. */
    uint16_t scan_y;        /* Diagnostic pattern progress only. */
} AppDebugLcd;

typedef struct {
    uint32_t reads;
    uint16_t raw_x, raw_y;
    int16_t x, y;
    uint8_t irq, down; /* irq is the pin level; down is filtered contact. */
} AppDebugTouch;

typedef struct {
    uint32_t jedec_id, offset;
    uint8_t status, ok, header[4];
} AppDebugStorage;

typedef struct {
    enum AppLoaderStage stage;
    uint32_t rx_bytes, uart_error;
    uint8_t last_byte;
} AppDebugLoader;

typedef struct {
    AppDebugSystem system;
    AppDebugRender render;
    AppDebugLcd lcd;
    AppDebugTouch touch;
    AppDebugStorage storage;
    AppDebugLoader loader;
} AppDebug;
extern volatile AppDebug app_debug;
void app_init(void);
void app_tick(void);
/* Implement this hook to build your own interface. */
void app_touch(int16_t x,int16_t y,uint8_t pressed);
#endif
