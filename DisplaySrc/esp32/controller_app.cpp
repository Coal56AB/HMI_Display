#include "hmi_board.h"
#include "controller_link.h"
#include "music_box_control.h"
#include "midi_board_config.h"
#include "midi_app.h"
#include "song_store.h"
#include "esp_link.h"
#include "driver/usb_serial_jtag.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <cstring>

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
bool heartbeat_due = true;
bool usb_device;
bool midi_connected;
bool pc_control_active;
uint32_t boot_pressed;
uint8_t midi_notes[6] = {255,255,255,255,255,255};
uint32_t midi_clock;
void send(control::Source source, const uint8_t *bytes, unsigned count) {
    // Stop/pause must gate the MIDI producer, otherwise its next heartbeat restarts notes.
    if (control::routes_to_midi(source,bytes[5],usb_device,pc_control_active)) {
        midi_app_send(bytes, count);
        // Global stop also reaches STM32 when the PC currently owns motors.
        if(source!=control::Uart || bytes[5]!=0)return;
    }
    if (source == control::Uart || source == control::Usb) outputs[source - 1].add(bytes, count);
}
void changed(control::Source source) {
    for (auto &out : outputs) out.discard_unsent();
    heartbeat_due = true;
    music_box_control_source(unsigned(source));
}
control::Link link(music_box_control_frame, changed, send);
void midi_frame(uint8_t cmd, const uint8_t *payload, unsigned size, uint32_t now) {
    uint8_t bytes[64];
    const auto n = control::encode(bytes, cmd, payload, size);
    for (unsigned i=0;i<n;++i) link.feed(control::Midi, bytes[i], now);
}
void put32(uint8_t *p, uint32_t n) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i)); }
}
void app_midi_snapshot(const live::Snapshot &s, bool reset_history) {
    midi_connected = s.connected;
    const uint32_t now = hmi_platform.now_ms();
    uint8_t p[50]{};
    if (reset_history) {
        put32(p,midi_clock-1);midi_frame(0x43,p,4,now);
        memset(midi_notes,255,sizeof(midi_notes));
    }
    put32(p,s.at);midi_frame(0x43,p,4,now);
    midi_clock=s.at;
    for(unsigned i=0;i<6;++i) {
        if(midi_notes[i]==s.notes[i])continue;
        p[4]=uint8_t(i);
        if(midi_notes[i]<128){p[5]=midi_notes[i];p[6]=0;midi_frame(0x42,p,7,now);}
        if(s.notes[i]<128){p[5]=s.notes[i];p[6]=100;midi_frame(0x42,p,7,now);}
        midi_notes[i]=s.notes[i];
    }
    memset(p,0,sizeof(p));p[0]=1;p[1]=s.connected?9:0;p[5]=63;
    if(!s.enabled)p[1]|=4;
    for(unsigned i=0;i<6;++i) {
        const bool active=s.notes[i]<128;
        p[14+i*6]=active?3:0;p[15+i*6]=s.notes[i];
        put32(p+16+i*6,live::frequency_mhz(s.notes[i]));
        if(active)p[1]|=2;
    }
    midi_frame(0x40,p,50,now);
}
void app_midi_ack(uint8_t sequence, uint8_t result) {
    uint8_t bytes[8];control::encode(bytes,0x51,&result,1,sequence);
    music_box_control_frame(bytes,sizeof(bytes));
}
void hmi_module_start() {
    hmi_boot_status("UART LINK",55);
    esp_link_start();
    song_store_start();
    usb_serial_jtag_driver_config_t usb{};
    usb.tx_buffer_size = 512; usb.rx_buffer_size = 4096;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    // ROM USB serial/JTAG sees host SOF even when no terminal has opened the port.
    // Allow enumeration after reset, then give the one internal PHY to the chosen role.
    usb_device = false;
    hmi_boot_status("USB DETECT",65);
    for(unsigned i=0;i<100;++i) {
        if(usb_serial_jtag_is_connected())usb_device=true;
        if(i%20==0)hmi_boot_status("USB DETECT",65+i/5);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI("hmi", "USB role: %s; hold BOOT 2s after changing cable to select again",
             usb_device ? "PC simulator" : "MIDI host");
    if(!usb_device) {
        ESP_ERROR_CHECK(usb_serial_jtag_driver_uninstall());
        hmi_boot_status("MIDI HOST",85);
        midi_app_start();
    }
    if(usb_device)hmi_boot_status("USB PC",85);
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_0,GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(GPIO_NUM_0,GPIO_PULLUP_ONLY));
}
void hmi_module_tick(uint32_t now) {
    uint8_t bytes[256];
    // UART first every iteration. USB input is parsed separately, never byte-mixed with UART.
    int count = esp_link_read(bytes, sizeof(bytes));
    for (int i = 0; i < count; ++i) {song_store_feed(bytes[i]);link.feed(control::Uart, bytes[i], now);}
    if(usb_device) {
        count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), 0);
        for (int i = 0; i < count; ++i) link.feed(control::Usb, bytes[i], now);
    } else midi_app_tick(now);
    link.tick(hmi_platform.now_ms());
    pc_control_active=(link.source_flags(control::Uart,now)&16)!=0;
    song_store_boot(link.active()==control::Uart,
        (link.source_flags(control::Uart,now)&16)||midi_connected);
    song_store_tick(now);
    music_box_control_connections((link.active() == control::Uart ? 1u : 0u) |
        ((usb_device && usb_serial_jtag_is_connected()) ||
         (link.source_flags(control::Uart, now) & 16) ? 2u : 0u) |
        (!usb_device && midi_connected ? 4u : 0u));
    if (heartbeat_due || now - last_heartbeat >= 100) {
        uint8_t frame[13], payload[6] = {255, 0, uint8_t(link.active() == control::Uart), 0, 0, 0};
        control::encode(frame, 0x50, payload, 6);
        outputs[0].add(frame, 13); last_heartbeat = now; heartbeat_due = false;
    }
    outputs[0].flush(control::Uart);
    if(usb_device && now-last_diagnostics>=500 && outputs[1].head==outputs[1].tail) {
        uint8_t p[32]{},b[39];
        put32(p,hmi_debug.touch_polls);put32(p+4,hmi_debug.touch_rejects);
        put32(p+8,hmi_debug.touch_cancels);put32(p+12,hmi_debug.touch_events);
        put32(p+16,hmi_debug.max_loop_ms);put32(p+20,hmi_debug.touch_down);
        put32(p+24,hmi_debug.raw_x);put32(p+28,hmi_debug.raw_y);
        outputs[1].add(b,control::encode(b,0x60,p,sizeof(p)));
        last_diagnostics=now;hmi_debug.max_loop_ms=0;
    }
    if(usb_device)outputs[1].flush(control::Usb);
    if(!gpio_get_level(GPIO_NUM_0)) {
        if(!boot_pressed)boot_pressed=now;
    } else if(boot_pressed) {
        const bool restart=now-boot_pressed>=2000;boot_pressed=0;
        // Restart on release so BOOT is not held while ROM samples the boot strap.
        if(restart)esp_restart();
    }
}
void hmi_module_send(const uint8_t *bytes, uint16_t count) {
    if(count==13&&song_store_action(bytes[5],bytes[6],uint32_t(bytes[7])|uint32_t(bytes[8])<<8|uint32_t(bytes[9])<<16|uint32_t(bytes[10])<<24)) {
        uint8_t reply[8],ok=0;control::encode(reply,0x51,&ok,1,bytes[3]);music_box_control_frame(reply,8);return;
    }
    link.action(bytes, count, hmi_platform.now_ms());
}
