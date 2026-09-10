#include "esp_link.h"
#include "half_duplex_wire.h"
#include "midi_board_config.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include <cstring>

namespace {
constexpr uart_port_t port=UART_NUM_1;
constexpr gpio_num_t wire=gpio_num_t(MIDI_UART_TX);
static_assert(MIDI_UART_TX==MIDI_UART_RX,"Single-wire UART requires one GPIO");
struct Packet {uint8_t length;uint8_t data[208];};
QueueHandle_t commands, notes;
StreamBufferHandle_t received;
TaskHandle_t worker;
void task(void *) {
    uint8_t sequence=0;
    for(;;) {
        Packet p{};
        if(xQueueReceive(commands,&p,0)!=pdTRUE)xQueueReceive(notes,&p,0);
        uint8_t bytes[HD_MAX_FRAME];
        const uint8_t seq=sequence++;
        const unsigned n=hd_encode(bytes,HD_REQUEST,seq,p.data,p.length);
        // Only this task accesses the physical UART. Keep RX enabled: it sees
        // our echo, which is rejected by its REQUEST type instead of flushing
        // a possibly early slave response after TX completion.
        uart_flush_input(port);
        uart_write_bytes(port,bytes,n);
        HdParser parser{};
        const int64_t deadline=esp_timer_get_time()+HD_MASTER_TIMEOUT_MS*1000;
        bool done=false;
        while(!done && esp_timer_get_time()<deadline) {
            uint8_t input[128];
            const int count=uart_read_bytes(port,input,sizeof(input),pdMS_TO_TICKS(1));
            for(int i=0;i<count;++i)if(hd_feed(&parser,input[i]) &&
                parser.bytes[2]==HD_REPLY && parser.bytes[3]==seq) {
                const unsigned size=parser.bytes[4];
                // UI backpressure must not stall MIDI or retain half a reply.
                if(xStreamBufferSpacesAvailable(received)>=size)
                    xStreamBufferSend(received,parser.bytes+5,size,0);
                done=true;
            }
        }
        // A reply completes the slave's turn. Without one, the 20 ms timeout
        // exceeds the slave's start deadline plus maximum wire duration.
        // New MIDI/actions wake us immediately; idle polling runs every 5 ms.
        if(!uxQueueMessagesWaiting(commands)&&!uxQueueMessagesWaiting(notes))
            ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5));
    }
}
}
void esp_link_start() {
    commands=xQueueCreate(8,sizeof(Packet));notes=xQueueCreate(1,sizeof(Packet));
    received=xStreamBufferCreate(4096,1);
    configASSERT(commands&&notes&&received);
    uart_config_t cfg{};
    cfg.baud_rate=HD_BAUD;cfg.data_bits=UART_DATA_8_BITS;
    cfg.parity=UART_PARITY_DISABLE;cfg.stop_bits=UART_STOP_BITS_1;
    cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;cfg.source_clk=UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(port,&cfg));
    ESP_ERROR_CHECK(uart_set_pin(port,wire,wire,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    // Explicit matrix routes also work on IDF versions whose uart_set_pin()
    // changes the direction while configuring RX equal to TX.
    ESP_ERROR_CHECK(gpio_set_direction(wire,GPIO_MODE_INPUT_OUTPUT_OD));
    ESP_ERROR_CHECK(gpio_set_pull_mode(wire,GPIO_PULLUP_ONLY));
    esp_rom_gpio_connect_out_signal(wire,U1TXD_OUT_IDX,false,false);
    esp_rom_gpio_connect_in_signal(wire,U1RXD_IN_IDX,false);
    ESP_ERROR_CHECK(uart_driver_install(port,2048,0,0,nullptr,0));
    configASSERT(xTaskCreatePinnedToCore(task,"stm-link",4096,nullptr,21,&worker,1)==pdPASS);
}
int esp_link_send(const uint8_t *data,unsigned length) {
    if(!commands||!length||length>208)return 0;
    Packet p{};p.length=(uint8_t)length;memcpy(p.data,data,length);
    const bool midi=length==14&&data[0]==0xd3&&data[1]==0x91;
    const BaseType_t ok=midi?xQueueOverwrite(notes,&p):xQueueSend(commands,&p,0);
    if(ok!=pdTRUE)return 0;
    xTaskNotifyGive(worker);return (int)length;
}
int esp_link_read(uint8_t *data,unsigned capacity) {
    return (int)xStreamBufferReceive(received,data,capacity,0);
}
