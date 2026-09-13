#include "hmi_board.h"
#include "hmi_module.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

void __attribute__((weak)) hmi_module_configure(DisplayPlatform *) {}
bool __attribute__((weak)) hmi_module_start() { return true; }
void __attribute__((weak)) hmi_module_tick(uint32_t) {}
void __attribute__((weak)) hmi_module_run() {}

static void ui_task(void *) {
    hmi_board_init();
    hmi_module_configure(&hmi_platform);
    hmi_boot_status("ASSETS",35);
    if (display_module.api_version != DISPLAY_API_VERSION ||
        (display_module.validate && !display_module.validate(&hmi_platform))) {
        hmi_boot_status("ASSET ERROR",35);
        ESP_LOGE("hmi", "Incompatible display module or invalid assets");
        vTaskDelete(nullptr);
        return;
    }
    if (!hmi_module_start()) {
        ESP_LOGE("hmi", "Application startup stopped");
        vTaskDelete(nullptr);
        return;
    }
    hmi_boot_status("INTERFACE",95,0);
    display_init(&hmi_platform);
    hmi_module_run();
    hmi_boot_status("READY",100,0);
    hmi_touch_poll(hmi_platform.now_ms());
    ESP_LOGI("hmi", "LCD/touch ready; free heap: %lu", (unsigned long)esp_get_free_heap_size());
    uint32_t previous_loop = hmi_platform.now_ms();
    for (;;) {
        const uint32_t now = hmi_platform.now_ms();
        if(now-previous_loop>hmi_debug.max_loop_ms)hmi_debug.max_loop_ms=now-previous_loop;
        previous_loop=now;
        hmi_touch_poll(now);
        hmi_module_tick(now);
        // Use fresh time after the application hook has finished.
        display_step(hmi_platform.now_ms());
        // Yield even with continuous dirty regions so other tasks can run.
        vTaskDelay(1);
    }
}
extern "C" void app_main() {
    // First operation: show boot progress before initializing the remaining services.
    hmi_lcd_init();
    configASSERT(xTaskCreatePinnedToCore(ui_task, "hmi", 8192, nullptr, 3, nullptr, 0) == pdPASS);
}
