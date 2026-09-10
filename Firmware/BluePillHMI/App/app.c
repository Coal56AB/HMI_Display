#include "app.h"
#include "board.h"
#include "main.h"
volatile AppDebug app_debug={.system={.spi=&hspi1}};
__weak void app_touch(int16_t x,int16_t y,uint8_t pressed){(void)x;(void)y;(void)pressed;}
void app_init(void){
 static const uint16_t black[320]={0};
 app_debug.system.clock_hz=SystemCoreClock;app_debug.system.boot_stage=APP_BOOT_LCD;
 board_lcd_init();board_assets_boot();board_reset_enable();
 for(unsigned y=0;y<480;y++)board_write_rect(0,(uint16_t)y,320,1,black,320,0);
 app_debug.system.boot_stage=APP_BOOT_READY;
}
void app_tick(void){
 static uint32_t last_touch,last_led;uint32_t now=HAL_GetTick();
 app_debug.system.tick_ms=now;
 if(now-last_led>=500){last_led=now;HAL_GPIO_TogglePin(LED_GPIO_Port,LED_Pin);}
 if(now-last_touch>=UI_POLL_INTERVAL_MS){
  int16_t x=0,y=0;uint16_t rx=0,ry=0;last_touch=now;
  uint8_t down=board_touch_sample(&x,&y,&rx,&ry);
  app_debug.touch.x=x;app_debug.touch.y=y;app_debug.touch.down=down;
  app_debug.touch.raw_x=rx;app_debug.touch.raw_y=ry;app_debug.touch.reads++;
  app_touch(x,y,down);
 }
}
