#include "app.h"
#include "board.h"
#include "main.h"
#include "display_api.h"
#include "usart.h"
volatile AppDebug app_debug={.system={.spi=&hspi1}};
static uint32_t last_poll;
static uint32_t last_led;
int app_uart_byte(uint8_t b){
#if !LCD_TEST_PATTERN
    DisplayEvent e={.type=DISPLAY_RX_BYTE,.byte=b};display_event(&e);return 1;
#else
    (void)b;return 0;
#endif
}
void app_uart_error(void){DisplayEvent e={.type=DISPLAY_RX_ERROR};display_event(&e);}
static void platform_reset(void){NVIC_SystemReset();}
static void platform_send(const uint8_t *b,uint16_t n){
    (void)HAL_UART_Transmit(&huart1,(uint8_t *)b,n,20);
}
#if !LCD_TEST_PATTERN
static void poll_ui(uint32_t now);
static DisplayEvent touch_queue[8];
static unsigned touch_in,touch_out;
#endif
static void write_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,
                       const uint16_t *pixels,uint16_t stride,void *user){
    uint32_t start=HAL_GetTick();
    board_write_rect(x,y,w,h,pixels,stride,user);
    app_debug.render.lcd_ms+=HAL_GetTick()-start;app_debug.lcd.writes++;
#if !LCD_TEST_PATTERN
    /* LCD CS is released and the transfer is finished before sharing SPI
     * with touch. No recursive render and no drawing from an interrupt. */
    poll_ui(HAL_GetTick());
#endif
}
static uint8_t sample_touch(int16_t *x,int16_t *y){
    uint16_t rx=0,ry=0;
    app_debug.touch.down=board_touch_sample(x,y,&rx,&ry);
    app_debug.touch.irq=(uint8_t)HAL_GPIO_ReadPin(TOUCH_IRQ_GPIO_Port,TOUCH_IRQ_Pin);
    app_debug.touch.raw_x=rx;app_debug.touch.raw_y=ry;
    app_debug.touch.x=*x;app_debug.touch.y=*y;app_debug.touch.reads++;
    return app_debug.touch.down;
}
#if LCD_TEST_PATTERN
static uint16_t test_color(unsigned x){
    static const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
    return colors[x/80];
}
static void marker(int16_t x,int16_t y,int visible){
    uint16_t row[17];int x0=x-8,x1=x+8,y0=y-8,y1=y+8,xx,yy;
    if(x0<0)x0=0;if(x1>319)x1=319;
    if(y0<0)y0=0;if(y1>479)y1=479;
    for(yy=y0;yy<=y1;yy++){
        for(xx=x0;xx<=x1;xx++){
            int dx=xx-x,dy=yy-y;
            uint16_t c=test_color((unsigned)xx);
            if(visible&&(dx==0||dy==0))c=0;
            else if(visible&&(dx==1||dx==-1||dy==1||dy==-1))c=0xffff;
            row[xx-x0]=c;
        }
        write_rect((uint16_t)x0,(uint16_t)yy,(uint16_t)(x1-x0+1),1,row,(uint16_t)(x1-x0+1),0);
    }
}
static void test_tick(uint32_t now){
    static int16_t old_x,old_y;static uint8_t old_down;
    uint16_t row[320];unsigned x,i;int16_t tx=0,ty=0;uint8_t down;
    if(app_debug.lcd.scan_y<480){
        for(x=0;x<320;x++)row[x]=test_color(x);
        for(i=0;i<8&&app_debug.lcd.scan_y<480;i++){
            write_rect(0,app_debug.lcd.scan_y++,320,1,row,320,0);
        }
        if(app_debug.lcd.scan_y==480)app_debug.lcd.frames++;
        return;
    }
    if(now-last_poll<UI_POLL_INTERVAL_MS)return;
    last_poll=now;down=sample_touch(&tx,&ty);
    if(down!=old_down||(down&&(tx!=old_x||ty!=old_y))){
        if(old_down)marker(old_x,old_y,0);
        if(down)marker(tx,ty,1);
        old_x=tx;old_y=ty;old_down=down;
    }
}
#else
static void render_ui(void){
    uint32_t start=HAL_GetTick(),writes=app_debug.lcd.writes,old_lcd_ms=app_debug.render.lcd_ms;
    uint32_t old_flash_ms=app_debug.render.flash_ms,old_flash_bytes=app_debug.render.flash_bytes;
    uint32_t old_flash_cycles=app_debug.render.flash_cycles;
    unsigned old_loads=display_module.asset_loads?display_module.asset_loads():0;
    app_debug.render.lcd_ms=0;
    app_debug.render.flash_ms=0;app_debug.render.flash_bytes=0;
    app_debug.render.flash_cycles=0;
    display_step(HAL_GetTick());
    if(app_debug.lcd.writes!=writes){
        app_debug.render.total_ms=HAL_GetTick()-start;
        app_debug.render.asset_loads=(display_module.asset_loads?display_module.asset_loads()-old_loads:0);app_debug.lcd.frames++;
    }else{
        app_debug.render.lcd_ms=old_lcd_ms;
        app_debug.render.flash_ms=old_flash_ms;app_debug.render.flash_bytes=old_flash_bytes;
        app_debug.render.flash_cycles=old_flash_cycles;
    }
}
#endif
#if !LCD_TEST_PATTERN
static void poll_ui(uint32_t now){
    int16_t x=0,y=0;uint8_t down;
    if(now-last_poll<UI_POLL_INTERVAL_MS)return;
    last_poll=now;app_debug.system.tick_ms=now;down=sample_touch(&x,&y);
    /* Preserve samples during SPI flushes; dispatch only between renders. */
    if(touch_in-touch_out<8)touch_queue[touch_in++%8]=(DisplayEvent){DISPLAY_TOUCH,now,x,y,down,0};
}
#endif
const DisplayPlatform board_platform={DISPLAY_API_VERSION,320,480,write_rect,board_assets_read,
    board_flash_read,board_flash_write,board_flash_erase,HAL_GetTick,platform_send,platform_reset,board_boot_progress};
void app_init(void){
    app_debug.system.test_enabled=LCD_TEST_PATTERN;
    app_debug.system.clock_hz=SystemCoreClock;
    app_debug.system.tick_ms=HAL_GetTick();
    HAL_GPIO_WritePin(LED_GPIO_Port,LED_Pin,GPIO_PIN_RESET);
    app_debug.system.boot_stage=APP_BOOT_LCD;
    board_lcd_init();
#if !LCD_TEST_PATTERN
    app_debug.system.boot_stage=APP_BOOT_ASSETS;
    if(display_module.api_version!=DISPLAY_API_VERSION)board_boot_error(3);
    board_assets_boot();
    app_debug.storage.ok=(uint8_t)(!display_module.validate||display_module.validate(&board_platform));
    if(!app_debug.storage.ok)board_boot_error((unsigned)(display_module.error?display_module.error():0));
    app_debug.system.boot_stage=APP_BOOT_RENDER;
    display_init(&board_platform);board_reset_enable();board_boot_progress(6,100);render_ui();
    if((display_module.error?display_module.error():0))board_boot_error((unsigned)(display_module.error?display_module.error():0));
#else
    board_reset_enable();
#endif
    app_debug.system.boot_stage=APP_BOOT_READY;
}
void app_tick(void){
    uint32_t now=HAL_GetTick();
    app_debug.system.tick_ms=now;
    if(now-last_led>=500u){last_led=now;HAL_GPIO_TogglePin(LED_GPIO_Port,LED_Pin);}
#if LCD_TEST_PATTERN
    test_tick(now);
#else
    /* Apply complete packets only between renders, never inside LCD flush. */
    poll_ui(now);
    while(touch_out!=touch_in){display_event(&touch_queue[touch_out%8]);touch_out++;}
    render_ui();
    if((display_module.error?display_module.error():0))board_boot_error((unsigned)(display_module.error?display_module.error():0));
#endif
}
