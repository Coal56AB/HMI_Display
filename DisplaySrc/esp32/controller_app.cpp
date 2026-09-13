#include "hmi_board.h"
#include "hmi_module.h"
#include "controller_link.h"
#include "music_box_control.h"
#include "ui_bridge.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "midi_board_config.h"
#include "midi_app.h"
#include "song_store.h"
#include "esp_link.h"
#include "startup_link.h"
#include "usb_role.h"
#include "esp_private/usb_phy.h"
#include "driver/usb_serial_jtag.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <cstring>
#include <atomic>

namespace {
// Separate bounded TX queues preserve partially sent packets on either transport.
struct Output {
    uint8_t frames[8][48]{}, lengths[8]{};
    unsigned head = 0, tail = 0, offset = 0;
    void add(const uint8_t *bytes, unsigned count) {
        if (count && count<=48 && head-tail<8) {
            memcpy(frames[head%8],bytes,count);lengths[head%8]=uint8_t(count);++head;
        }
    }
    void discard_unsent() { if (offset) head = tail + 1; else tail = head; }
    void flush(control::Source source) {
        if (head == tail) return;
        const auto *bytes = frames[tail % 8] + offset;
        const unsigned length=lengths[tail%8];
        const int n = source == control::Uart ?
            esp_link_send(bytes, length - offset) :
            usb_serial_jtag_write_bytes(bytes, length - offset, 0);
        if (n > 0) offset += unsigned(n);
        if (offset == length) { ++tail; offset = 0; }
    }
} outputs[2];
uint32_t last_heartbeat;
uint32_t last_diagnostics;
uint32_t last_link_report;
bool heartbeat_due = true;
std::atomic<bool> usb_device{false};
usb_phy_handle_t serial_phy=nullptr;
UsbRole usb_role;
bool midi_connected;
bool pc_control_active;
struct UiCommand {bool diagnostic;unsigned length;uint8_t bytes[48];};
QueueHandle_t ui_commands;
TaskHandle_t controller_task;
void send(control::Source source, const uint8_t *bytes, unsigned count) {
    if(source==control::Uart && count>=8 && bytes[4]==0x56) {
        esp_link_send(bytes,count);return; // Bounded queue; PC retries if full.
    }
    if (source == control::Uart || (source == control::Usb && usb_device)) outputs[source - 1].add(bytes, count);
}
void changed(control::Source source) {
    for (auto &out : outputs) out.discard_unsent();
    heartbeat_due = true;
    ui_bridge::music_box_control_source(unsigned(source));
    if(usb_device) {
        ui_bridge::music_box_midi_name("USB-симулятор");
    }
}
control::Link controller_link(ui_bridge::music_box_control_frame, changed, send);
void put32(uint8_t *p, uint32_t n) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i)); }
void serial_start() {
    usb_phy_config_t phy{};phy.controller=USB_PHY_CTRL_SERIAL_JTAG;phy.target=USB_PHY_TARGET_INT;
    ESP_ERROR_CHECK(usb_new_phy(&phy,&serial_phy));
    usb_serial_jtag_driver_config_t usb{};usb.tx_buffer_size=512;usb.rx_buffer_size=4096;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    usb_device=true;
}
void serial_stop() {
    usb_device=false;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_uninstall());
    ESP_ERROR_CHECK(usb_del_phy(serial_phy));serial_phy=nullptr;
    outputs[1]=Output{};
}
void update_usb_role(uint32_t now) {
    const bool peer=usb_device?usb_serial_jtag_is_connected():midi_app_connected();
    switch(usb_role.tick(now,peer,midi_app_stopped())) {
    case UsbRole::StartHost:
        serial_stop();controller_link.disconnect(control::Usb,now);
        ui_bridge::music_box_midi_name("USB MIDI");
        midi_app_start();ESP_LOGI("hmi","USB: probing MIDI host");break;
    case UsbRole::StopHost:
        midi_app_stop();break;
    case UsbRole::StartSerial:
        serial_start();ui_bridge::music_box_midi_name("USB-симулятор");
        ESP_LOGI("hmi","USB: probing PC");break;
    default:break;
    }
}
}
bool hmi_module_start() {
    hmi_boot_status("UART LINK",55);
    esp_link_start();
    serial_start();
    // ROM USB serial/JTAG sees host SOF even when no terminal has opened the port.
    // Allow enumeration after reset, then give the one internal PHY to the chosen role.
    bool initial_pc = false;
    hmi_boot_status("USB DETECT",65);
    for(unsigned i=0;i<10;++i) {
        if(usb_serial_jtag_is_connected()){initial_pc=true;break;}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    hmi_boot_status("UART LINK",85);
    // Discard pre-check data: success must come from the new heartbeat.
    uint8_t stale[256];
    while (esp_link_read(stale, sizeof(stale)) > 0) {}
    StartupLink startup(hmi_platform.now_ms());
    bool waiting_message = false;
    while (startup.state(hmi_platform.now_ms()) != StartupLink::State::Ready) {
        const uint32_t now = hmi_platform.now_ms();
        if (startup.state(now) == StartupLink::State::Failed) {
            if (usb_serial_jtag_is_connected()) {initial_pc=true;break;}
            if (!waiting_message) {
                music_box_boot_error(&hmi_platform);
                ESP_LOGW("hmi", "Waiting for STM32; UART probes continue");
                waiting_message = true;
            }
        }
        uint8_t bytes[256];
        const int count = esp_link_read(bytes, sizeof(bytes));
        for (int i = 0; i < count; ++i) startup.feed(bytes[i], now);
        if (startup.state(now) == StartupLink::State::Ready) break;
        if (startup.probe_due(now)) {
            // Register the screen link, but do not request motor authority or
            // issue START/boot-test commands during the health check.
            uint8_t frame[13], payload[6] = {255, 0, 0, 0, 0, 0};
            control::encode(frame, 0x50, payload, sizeof(payload));
            if (esp_link_send(frame, sizeof(frame)) == sizeof(frame)) startup.probe_sent(now);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    song_store_start();
    initial_pc=usb_serial_jtag_is_connected();
    ESP_LOGI("hmi", "USB auto role: %s",initial_pc ? "PC simulator" : "MIDI host");
    if(!initial_pc) {
        serial_stop();
        hmi_boot_status("MIDI HOST",90,0);
        midi_app_start();
    }
    if(usb_device)hmi_boot_status("USB PC",90,0);
    usb_role.begin(usb_device,hmi_platform.now_ms());
    return true;
}
static void handle_action(const uint8_t *bytes,uint16_t count) {
    static uint8_t previous[13];static bool previous_valid=false;static uint32_t previous_at=0;
    const uint32_t now=hmi_platform.now_ms();
    bool duplicate=count==13&&previous_valid&&now-previous_at<2000&&!memcmp(previous,bytes,13);
    if(duplicate||(count==13&&song_store_action(bytes[5],bytes[6],uint32_t(bytes[7])|uint32_t(bytes[8])<<8|uint32_t(bytes[9])<<16|uint32_t(bytes[10])<<24))) {
        if(!duplicate){memcpy(previous,bytes,13);previous_valid=true;previous_at=now;}
        uint8_t reply[8],ok=0;control::encode(reply,0x51,&ok,1,bytes[3]);ui_bridge::music_box_control_frame(reply,8);return;
    }
    controller_link.action(bytes,count,now);
}
static void control_tick(uint32_t now) {
    update_usb_role(now);
    uint8_t bytes[256];
    // UART first every iteration. USB input is parsed separately, never byte-mixed with UART.
    int count;
    for(unsigned batch=0;batch<16;++batch) {
        count=esp_link_read(bytes,sizeof(bytes));
        for(int i=0;i<count;++i){song_store_feed(bytes[i]);controller_link.feed(control::Uart,bytes[i],now);}
        if(count<int(sizeof(bytes)))break;
    }
    if(usb_device) {
        count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), 0);
        for (int i = 0; i < count; ++i) controller_link.feed(control::Usb, bytes[i], now);
    }
    midi_connected=usb_device ? (controller_link.source_flags(control::Usb,now)&8)!=0 : midi_app_connected();
    controller_link.tick(hmi_platform.now_ms());
    // A fresh simulator STATE advertises a virtual MIDI instrument, even at rest.
    // The lease removes its connection icon when the app closes or USB is lost.
    const bool simulated_midi = usb_device && (controller_link.source_flags(control::Usb,now)&8);
    const bool selected_simulated_midi = simulated_midi && controller_link.active()==control::Usb &&
        !(controller_link.source_flags(control::Usb,now)&4);
    UiCommand action;
    for(unsigned n=0;n<16&&xQueueReceive(ui_commands,&action,0)==pdTRUE;++n) {
        if(action.diagnostic) {if(usb_device)outputs[1].add(action.bytes,action.length);}
        else handle_action(action.bytes,action.length);
    }
    ui_bridge::music_box_midi_input(selected_simulated_midi ||
        (midi_connected && !(controller_link.source_flags(control::Uart,now)&4)));
    pc_control_active=(controller_link.source_flags(control::Uart,now)&16)!=0;
    song_store_boot(controller_link.active()==control::Uart,
        (controller_link.source_flags(control::Uart,now)&16)||midi_connected);
    song_store_tick(now);
    ui_bridge::music_box_control_connections((controller_link.active() == control::Uart ? 1u : 0u) |
        ((controller_link.source_flags(control::Uart, now) & 16) ? 2u : 0u) |
        (simulated_midi || (!usb_device && midi_connected) ? 4u : 0u));
    if (heartbeat_due || now - last_heartbeat >= 100) {
        uint8_t frame[13], payload[6] = {255, 0, uint8_t(controller_link.active() == control::Uart), 0, 0, 0};
        control::encode(frame, 0x50, payload, 6);
        outputs[0].add(frame, 13); last_heartbeat = now; heartbeat_due = false;
    }
    outputs[0].flush(control::Uart);
    if(usb_device&&now-last_link_report>=500) {
        uint32_t values[6];uint8_t p[24],frame[31];esp_link_stats(values);
        for(unsigned i=0;i<6;++i)put32(p+4*i,values[i]);
        control::encode(frame,0x61,p,24);outputs[1].add(frame,31);last_link_report=now;
    }
    if(usb_device)outputs[1].flush(control::Usb);
}
void hmi_module_run() {
    ui_commands=xQueueCreate(16,sizeof(UiCommand));configASSERT(ui_commands);
    configASSERT(xTaskCreatePinnedToCore([](void*) {
        for(;;){control_tick(hmi_platform.now_ms());ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1));}
    },"song-control",8192,nullptr,8,&controller_task,1)==pdPASS);
}
void hmi_module_tick(uint32_t now) {
    ui_bridge::drain();
    ui_bridge::set_screen_ready(music_box_screen_ready());
    if(usb_device && now-last_diagnostics>=500) {
        uint8_t p[32]{},b[39];
        put32(p,hmi_debug.touch_polls);put32(p+4,hmi_debug.touch_rejects);
        put32(p+8,hmi_debug.touch_cancels);put32(p+12,hmi_debug.touch_events);
        put32(p+16,hmi_debug.max_loop_ms);put32(p+20,hmi_debug.touch_down);
        put32(p+24,hmi_debug.raw_x);put32(p+28,hmi_debug.raw_y);
        UiCommand diagnostic{};diagnostic.diagnostic=true;
        diagnostic.length=control::encode(b,0x60,p,sizeof(p));memcpy(diagnostic.bytes,b,diagnostic.length);
        xQueueSend(ui_commands,&diagnostic,0);
        last_diagnostics=now;hmi_debug.max_loop_ms=0;
    }
}
void hmi_module_send(const uint8_t *bytes, uint16_t count) {
    if(!ui_commands||count>48)return;
    UiCommand action{};action.length=count;memcpy(action.bytes,bytes,count);
    if(xQueueSend(ui_commands,&action,0)==pdTRUE)xTaskNotifyGive(controller_task);
}
