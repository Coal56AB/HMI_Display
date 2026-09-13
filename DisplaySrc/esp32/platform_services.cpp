#include "hmi_module.h"
#include "hmi_board.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include <algorithm>

void hmi_module_send(const uint8_t *bytes, uint16_t count);

namespace {
const esp_partition_t *storage;
bool range(uint32_t at, uint32_t count) {
    return storage && at <= storage->size && count <= storage->size - at;
}
int read_flash(uint32_t at, void *data, uint32_t count) {
    return range(at, count) && esp_partition_read(storage, at, data, count) == ESP_OK;
}
int read_assets(uint32_t at, void *data, uint32_t count, void *) { return read_flash(at, data, count); }
int write_flash(uint32_t at, const void *data, uint32_t count) {
    return at >= display_module.assets_size && range(at, count) &&
           esp_partition_write(storage, at, data, count) == ESP_OK;
}
int erase_flash(uint32_t at) {
    return !(at & 4095) && at >= display_module.assets_size && range(at, 4096) &&
           esp_partition_erase_range(storage, at, 4096) == ESP_OK;
}
void restore_settings(void) {
    // Preserve settings before the expanded song store reuses their former sector.
    constexpr uint32_t settings = 0xdf000, legacy_settings = 0x30f000;
    uint8_t current[8], legacy[8];
    if (display_module.assets_size == 0 && storage->size == 0xe0000 &&
        esp_partition_read(storage, settings, current, sizeof(current)) == ESP_OK &&
        std::all_of(current, current + sizeof(current), [](uint8_t b){return b == 255;}) &&
        esp_flash_read(nullptr, legacy, legacy_settings, sizeof(legacy)) == ESP_OK &&
        legacy[0] == 'M' && legacy[1] == 'B' && legacy[2] == 1) {
        // The GUI validates the settings CRC before applying them.
        ESP_ERROR_CHECK(esp_partition_erase_range(storage, settings, 4096));
        ESP_ERROR_CHECK(esp_partition_write(storage, settings, legacy, sizeof(legacy)));
    }
}
}

void hmi_module_configure(DisplayPlatform *platform) {
    hmi_boot_status("STORAGE",25);
    storage=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"hmi_store");
    if(!storage)hmi_boot_status("STORAGE ERROR",25);
    configASSERT(storage);
    restore_settings();
    platform->read_assets=read_assets;
    platform->flash_read=read_flash;
    platform->flash_write=write_flash;
    platform->flash_erase=erase_flash;
    platform->send=hmi_module_send;
}
