#include "hmi_module.h"
#include "hmi_board.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
extern "C" {
#include "journal_store.h"
extern const uint8_t pch_assets_start[],pch_assets_end[];
}
#include <cstring>

static const esp_partition_t *journal;
#if !PCH_USE_USB
static QueueHandle_t uart_events;
#endif
static bool uart_ready;
static int read_assets(uint32_t at,void *data,uint32_t size,void *) {
    const size_t length=pch_assets_end-pch_assets_start;
    if(at>length || size>length-at)return 0;
    memcpy(data,pch_assets_start+at,size);return 1;
}
static bool journal_range(uint32_t at,uint32_t size) {
    return journal && at>=JOURNAL_BASE && at-JOURNAL_BASE<=journal->size && size<=journal->size-(at-JOURNAL_BASE);
}
static int flash_read(uint32_t at,void *data,uint32_t size) {
    return journal_range(at,size) && esp_partition_read(journal,at-JOURNAL_BASE,data,size)==ESP_OK;
}
static int flash_write(uint32_t at,const void *data,uint32_t size) {
    return journal_range(at,size) && esp_partition_write(journal,at-JOURNAL_BASE,data,size)==ESP_OK;
}
static int flash_erase(uint32_t at) {
    return !(at&4095u) && journal_range(at,4096) && esp_partition_erase_range(journal,at-JOURNAL_BASE,4096)==ESP_OK;
}
static void send(const uint8_t *data,uint16_t size) {
    if(!uart_ready)return;
#if PCH_USE_USB
    usb_serial_jtag_write_bytes(data,size,pdMS_TO_TICKS(20));
#else
    uart_write_bytes(PCH_UART_PORT,data,size);
#endif
}
static void progress(unsigned stage,unsigned percent) {
    static unsigned previous_stage=~0u,previous_percent=~0u;
    if(stage==previous_stage&&percent==previous_percent)return;
    previous_stage=stage;previous_percent=percent;
    hmi_boot_status(stage==5?"JOURNAL":"ASSETS",percent,0);
    vTaskDelay(1);
}
void hmi_module_configure(DisplayPlatform *platform) {
    journal=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"pch_journal");
    platform->read_assets=read_assets;platform->flash_read=flash_read;
    platform->flash_write=flash_write;platform->flash_erase=flash_erase;
    platform->send=send;platform->boot_progress=progress;
}
bool hmi_module_start() {
    if(!journal||journal->size<2u*JOURNAL_BANK){hmi_boot_status("JOURNAL ERROR",35,0);return false;}
#if PCH_USE_USB
    usb_serial_jtag_driver_config_t config={4096,4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
#else
    uart_config_t config{};
    config.baud_rate=PCH_UART_BAUD;config.data_bits=UART_DATA_8_BITS;
    config.parity=UART_PARITY_DISABLE;config.stop_bits=UART_STOP_BITS_1;
    config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;config.source_clk=UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(PCH_UART_PORT,&config));
    ESP_ERROR_CHECK(uart_set_pin(PCH_UART_PORT,PCH_UART_TX,PCH_UART_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(PCH_UART_PORT,4096,4096,16,&uart_events,0));
#endif
    uart_ready=true;return true;
}
void hmi_module_tick(uint32_t now) {
#if !PCH_USE_USB
    uart_event_t event;
    while(xQueueReceive(uart_events,&event,0)==pdTRUE) {
        if(event.type==UART_FIFO_OVF||event.type==UART_BUFFER_FULL||event.type==UART_FRAME_ERR||event.type==UART_PARITY_ERR) {
            uart_flush_input(PCH_UART_PORT);
            const DisplayEvent error={DISPLAY_RX_ERROR,now,0,0,0,0};display_event(&error);
        }
    }
#endif
    uint8_t bytes[128];
#if PCH_USE_USB
    const int count=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),0);
#else
    const int count=uart_read_bytes(PCH_UART_PORT,bytes,sizeof(bytes),0);
#endif
    for(int i=0;i<count;++i){const DisplayEvent rx={DISPLAY_RX_BYTE,now,0,0,0,bytes[i]};display_event(&rx);}
}
