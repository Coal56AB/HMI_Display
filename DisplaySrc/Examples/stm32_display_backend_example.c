#include "hmi_ui.h"
#include <stddef.h>

/* Implement in the board project. Switching SPI to FSMC changes only these
 * functions. Native uint16_t RGB565 is NOT a byte wire format: the board driver
 * supplies controller-specific command framing, byte order and RGB666 if needed. */
extern void board_lcd_set_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1);
extern void board_lcd_write_rgb565(const uint16_t *pixels,uint32_t count);
extern void board_lcd_wait_complete(void);
static HmiUi ui;
static void display_flush(uint16_t x,uint16_t y,uint16_t width,uint16_t height,
                          const uint16_t *rgb565,uint16_t stride,void *user){
    uint16_t row;(void)user;
    board_lcd_set_window(x,y,(uint16_t)(x+width-1u),(uint16_t)(y+height-1u));
    if(stride==width){
        board_lcd_write_rgb565(rgb565,(uint32_t)width*height);board_lcd_wait_complete();
    }else for(row=0;row<height;row++){
        board_lcd_write_rgb565(rgb565+(uint32_t)row*stride,width);board_lcd_wait_complete();
    }
    /* DMA must finish before return: the renderer reuses rgb565 immediately.
     * SPI must also be idle before selecting XPT2046 on the shared bus. */
}
void application_ui_init(void){
    hmi_ui_init(&ui,(HmiDisplay){display_flush,NULL});hmi_ui_render(&ui);
}
void application_ui_step(int16_t calibrated_x,int16_t calibrated_y,uint8_t pressed,uint32_t now_ms){
    /* Read XPT2046 first using its own slower SPI setting and CS.
     * Calibration/rotation maps ADC readings to x=0..319 and y=0..479. */
    hmi_ui_touch(&ui,calibrated_x,calibrated_y,pressed,now_ms);hmi_ui_render(&ui);
}
int application_ui_next_event(HmiEvent *event){
    /* Drain in foreground and encode type/id/value/flags/data into your protocol.
     * Do not transmit the raw C struct (padding, endian, float format).
     * A requested UI change is not an acknowledgement from the power controller. */
    return hmi_ui_poll_event(&ui,event);
}
void application_update_output_current(float current){
    HmiState previous=ui.state;ui.state.output_current=current;
    hmi_diff_and_invalidate(&previous,&ui.state);
}
