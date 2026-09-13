#include "hmi_board.h"
#include "board_config.h"
#include "lcd_damage.h"
#include "hmi_boot_render.h"
#ifndef HMI_BOOT_WIDTH
#define HMI_BOOT_WIDTH 480
#endif
#ifndef HMI_BOOT_HEIGHT
#define HMI_BOOT_HEIGHT 320
#endif
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstring>

volatile HmiDiagnostics hmi_debug{};
namespace {
spi_device_handle_t lcd, touch;
uint8_t *dma_pixels;
uint32_t last_touch;
uint32_t last_probe;
uint32_t last_valid_touch;
LcdDamage damage;
bool previous_down;
int16_t previous_x, previous_y;
void level(int pin, int value) { ESP_ERROR_CHECK(gpio_set_level(gpio_num_t(pin), value)); }
void output(int pin) {
    ESP_ERROR_CHECK(gpio_reset_pin(gpio_num_t(pin)));
    ESP_ERROR_CHECK(gpio_set_direction(gpio_num_t(pin), GPIO_MODE_OUTPUT));
    level(pin, 1);
}
void delay_ms(unsigned ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
void transfer(spi_device_handle_t device, const void *data, unsigned bytes) {
    spi_transaction_t t{};
    t.length = bytes * 8;
    if (bytes <= 4) {
        t.flags = SPI_TRANS_USE_TXDATA;
        memcpy(t.tx_data, data, bytes);
    } else t.tx_buffer = data;
    // Interrupt-driven DMA; waiting suspends this task, so other tasks can run.
    if (bytes <= 64) ESP_ERROR_CHECK(spi_device_polling_transmit(device, &t));
    else ESP_ERROR_CHECK(spi_device_transmit(device, &t));
}
void lcd_byte(uint8_t b) {
    const uint8_t encoded[] = {0, b};
    transfer(lcd, HMI_LCD_RPI_ADAPTER ? encoded : &b, HMI_LCD_RPI_ADAPTER ? 2 : 1);
}
void reg(uint8_t command, const uint8_t *bytes, unsigned count) {
    level(HMI_LCD_CS, 0); level(HMI_LCD_DC, 0); lcd_byte(command);
    level(HMI_LCD_DC, 1);
    // Pi adapter requires 16-bit command/parameter transfers, RGB565 pixel data.
    uint8_t encoded[64];
    configASSERT(count <= 32);
    unsigned n = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (HMI_LCD_RPI_ADAPTER) encoded[n++] = 0;
        encoded[n++] = bytes[i];
    }
    if (n) transfer(lcd, encoded, n);
    level(HMI_LCD_CS, 1);
}
void write_rect_raw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                const uint16_t *pixels, uint16_t stride, void *) {
    if (!w || !h) return;
    configASSERT(x + w <= 480 && y + h <= 320 && stride >= w && pixels);
    const int64_t started = esp_timer_get_time();
    const uint16_t xe = x + w - 1, ye = y + h - 1;
    const uint8_t cols[] = {uint8_t(x >> 8), uint8_t(x), uint8_t(xe >> 8), uint8_t(xe)};
    const uint8_t rows[] = {uint8_t(y >> 8), uint8_t(y), uint8_t(ye >> 8), uint8_t(ye)};
    reg(0x2a, cols, 4); reg(0x2b, rows, 4);
    level(HMI_LCD_CS, 0); level(HMI_LCD_DC, 0); lcd_byte(0x2c); level(HMI_LCD_DC, 1);
    unsigned used = 0, count = 0;
    for (unsigned row = 0; row < h; ++row) for (unsigned col = 0; col < w; ++col) {
        const uint16_t c = pixels[row * stride + col];
        if (HMI_LCD_RPI_ADAPTER) {
            dma_pixels[used++] = c >> 8; dma_pixels[used++] = c;
        } else {
            dma_pixels[used++] = (c >> 8) & 0xf8;
            dma_pixels[used++] = (c >> 3) & 0xfc;
            dma_pixels[used++] = (c << 3) & 0xf8;
        }
        if (++count == HMI_DMA_PIXELS) { transfer(lcd, dma_pixels, used); used = count = 0; }
    }
    if (used) transfer(lcd, dma_pixels, used);
    level(HMI_LCD_CS, 1);
    // DMA has completed before returning: the renderer may reuse its strip now.
    const uint32_t elapsed = uint32_t(esp_timer_get_time() - started);
    hmi_debug.rectangles = hmi_debug.rectangles + 1;
    hmi_debug.pixels = hmi_debug.pixels + uint32_t(w) * h;
    hmi_debug.last_write_us = elapsed;
    if (elapsed > hmi_debug.max_write_us) hmi_debug.max_write_us = elapsed;
}
void write_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *pixels,uint16_t stride,void *) {
    configASSERT(x+w<=480 && y+h<=320 && stride>=w && pixels);
    if(w<=32) {
        // Small GUI rectangles, including exact pixel deltas: avoid tile-splitting overhead.
        damage.invalidate(x,y,w,h);
        write_rect_raw(x,y,w,h,pixels,stride,nullptr);
        return;
    }
    damage.draw(x,y,w,h,pixels,stride,[](unsigned xx,unsigned yy,unsigned ww,unsigned hh,const uint16_t *p,unsigned s){
        write_rect_raw(xx,yy,ww,hh,p,s,nullptr);
    });
}
int scale(int raw, int low, int high, int extent) {
    return std::clamp((raw - low) * (extent - 1) / (high - low), 0, extent - 1);
}
uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }
}
DisplayPlatform hmi_platform = {DISPLAY_API_VERSION, 480, 320, write_rect, nullptr,
    nullptr, nullptr, nullptr, now_ms, nullptr, esp_restart, nullptr};

void hmi_boot_status(const char *label, unsigned percent, unsigned dwell_ms) {
    static bool painted=false;
    static DisplayPlatform boot_platform;
    boot_platform=hmi_platform;boot_platform.write_rect=write_rect_raw;
    hmi_boot_draw(&boot_platform,HMI_BOOT_WIDTH,HMI_BOOT_HEIGHT,label,percent,painted);
    painted=true;
    damage.invalidate(0,0,480,320);
    if (dwell_ms) vTaskDelay(pdMS_TO_TICKS(dwell_ms));
}
void hmi_lcd_init() {
    output(HMI_LCD_CS); output(HMI_LCD_DC); output(HMI_LCD_RST); output(HMI_TOUCH_CS);
    spi_bus_config_t bus{};
    bus.mosi_io_num = HMI_SPI_MOSI; bus.miso_io_num = HMI_SPI_MISO;
    bus.sclk_io_num = HMI_SPI_SCLK; bus.quadwp_io_num = bus.quadhd_io_num = -1;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    bus.max_transfer_sz = HMI_DMA_PIXELS * 3;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t device{};
    device.clock_speed_hz = HMI_LCD_CLOCK_HZ; device.spics_io_num = -1; device.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &device, &lcd));
    dma_pixels = static_cast<uint8_t *>(heap_caps_malloc(HMI_DMA_PIXELS * 3, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    configASSERT(dma_pixels);
    level(HMI_LCD_RST, 0); delay_ms(20); level(HMI_LCD_RST, 1); delay_ms(120);
    reg(1, nullptr, 0); delay_ms(120); reg(0x11, nullptr, 0); delay_ms(120);
    const uint8_t fmt = HMI_LCD_RPI_ADAPTER ? 0x55 : 0x66, rotation = HMI_LCD_MADCTL;
    const uint8_t p0[] = {14, 14}, p1[] = {0x41, 0}, p2[] = {0x55}, vcom[] = {0, 0, 0, 0};
    const uint8_t g0[] = {15,31,28,12,15,8,0x48,0x98,0x37,10,19,4,17,13,0};
    const uint8_t g1[] = {15,50,46,11,13,5,0x47,0x75,0x37,6,16,3,36,32,0};
    reg(0x3a, &fmt, 1); reg(0xc0, p0, 2); reg(0xc1, p1, 2); reg(0xc2, p2, 1);
    reg(0xc5, vcom, 4); reg(0xe0, g0, 15); reg(0xe1, g1, 15);
    reg(0x20, nullptr, 0); reg(0x36, &rotation, 1);
    hmi_boot_status("LCD READY",5); // Fill GRAM before turning the panel on, avoiding a white frame.
    reg(0x29, nullptr, 0); delay_ms(150);
}
void hmi_board_init() {
    hmi_boot_status("TOUCH",15);
    ESP_ERROR_CHECK(gpio_set_direction(gpio_num_t(HMI_TOUCH_IRQ),GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(gpio_num_t(HMI_TOUCH_IRQ),GPIO_PULLUP_ONLY));
    spi_device_interface_config_t device{};
    device.clock_speed_hz=HMI_TOUCH_CLOCK_HZ;device.spics_io_num=-1;device.queue_size=1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST,&device,&touch));

}
void hmi_touch_poll(uint32_t now) {
    if (now - last_touch < HMI_TOUCH_POLL_MS) return;
    last_touch = now;
    bool irq = gpio_get_level(gpio_num_t(HMI_TOUCH_IRQ)) == 0;
    // Periodically re-arm PENIRQ even if it got disabled or the IRQ wire is noisy.
    if(!irq&&!previous_down&&now-last_probe<100)return;
    last_probe=now;
    hmi_debug.touch_polls=hmi_debug.touch_polls+1;
    bool down=false;
    int16_t x = previous_x, y = previous_y;
    {
        uint16_t xs[5], ys[5];
        uint8_t tx[45]{},rx[45]{};
        tx[0]=0xb1;tx[3]=0xc1; // Z1/Z2 pressure, ADC stays on until the last conversion.
        for(unsigned i=2;i<8;++i)tx[i*3]=0xd1;
        for(unsigned i=8;i<14;++i)tx[i*3]=0x91;
        tx[42]=0x90; // PD0=0 restores touch interrupt after every sample, including rejects.
        spi_transaction_t t{};t.length=t.rxlength=sizeof(tx)*8;t.tx_buffer=tx;t.rx_buffer=rx;
        level(HMI_TOUCH_CS, 0);
        ESP_ERROR_CHECK(spi_device_polling_transmit(touch,&t));
        level(HMI_TOUCH_CS, 1);
        auto sample=[&](unsigned i){return uint16_t((uint16_t(rx[i*3+1])<<5)|(rx[i*3+2]>>3))&4095;};
        down=sample(0)>50 && sample(0)+4095-sample(1)>100;
        for(unsigned i=0;i<5;++i){xs[i]=sample(i+3);ys[i]=sample(i+9);}
        std::sort(xs, xs + 5); std::sort(ys, ys + 5);
        hmi_debug.raw_x = xs[2]; hmi_debug.raw_y = ys[2];
        // Ignore a noisy sample rather than generating a false release/tap.
        if (down && (xs[3] - xs[1] > HMI_TOUCH_MAX_SPREAD || ys[3] - ys[1] > HMI_TOUCH_MAX_SPREAD ||
            !xs[2] || xs[2] == 4095 || !ys[2] || ys[2] == 4095)) {
            hmi_debug.touch_rejects=hmi_debug.touch_rejects+1;
            if(previous_down && now-last_valid_touch>=100) {
                const DisplayEvent cancel={DISPLAY_TOUCH_CANCEL,now,0,0,0,0};
                display_event(&cancel);previous_down=false;hmi_debug.touch_down=0;
                hmi_debug.touch_cancels=hmi_debug.touch_cancels+1;
            }
            return;
        }
        last_valid_touch=now;
        int px = scale(HMI_TOUCH_SWAP_XY ? ys[2] : xs[2], HMI_TOUCH_X_MIN, HMI_TOUCH_X_MAX, 320);
        int py = scale(HMI_TOUCH_SWAP_XY ? xs[2] : ys[2], HMI_TOUCH_Y_MIN, HMI_TOUCH_Y_MAX, 480);
        if (HMI_TOUCH_INVERT_X) px = 319 - px;
        if (HMI_TOUCH_INVERT_Y) py = 479 - py;
        if(down){x = py; y = 319 - px;}
    }
    if (down || previous_down) {
        hmi_debug.touch_events=hmi_debug.touch_events+1;
        const DisplayEvent event = {DISPLAY_TOUCH, now, x, y, uint8_t(down), 0};
        display_event(&event);
    }
    previous_down = down; previous_x = x; previous_y = y;
    hmi_debug.touch_down = down; hmi_debug.touch_x = x; hmi_debug.touch_y = y;
}
