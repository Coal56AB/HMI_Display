#include "board.h"
#include "app.h"
#include "main.h"
#include "spi.h"
#include "usart.h"
#include "display_api.h"
#include <string.h>
__weak int app_uart_byte(uint8_t b){(void)b;return 0;}
__weak void app_uart_error(void){}
void board_reset_poll(void){
    static unsigned matched;
    static const uint8_t command[]={'P','C','H','R'};
    uint32_t status;uint8_t b;
    if(!huart1.Instance)return;
    status=huart1.Instance->SR;
    if(!(status&(USART_SR_RXNE|USART_SR_ORE)))return;
    b=(uint8_t)huart1.Instance->DR; /* SR then DR also clears receive errors. */
    if(status&(USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)){matched=0;app_uart_error();return;}
    /* Error_Handler disables interrupts and never drains the telemetry queue.
     * Recognize reset directly there; normal IRQ traffic stays frame-aware. */
    if(!__get_PRIMASK()&&app_uart_byte(b))return;
    matched=b==command[matched]?matched+1u:(b=='P'?1u:0u);
    if(matched==4){matched=0;NVIC_SystemReset();}
}
void board_reset_enable(void){
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    HAL_NVIC_SetPriority(USART1_IRQn,2,0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    __HAL_UART_ENABLE_IT(&huart1,UART_IT_RXNE);
}
static void pin(GPIO_TypeDef *port,uint16_t p,int high){HAL_GPIO_WritePin(port,p,high?GPIO_PIN_SET:GPIO_PIN_RESET);}
static void tx(SPI_HandleTypeDef *spi,const uint8_t *data,uint16_t n){
    if(HAL_SPI_Transmit(spi,(uint8_t *)data,n,200)!=HAL_OK)Error_Handler();
}
static void lcd_byte(uint8_t b){
#if LCD_RPI_ADAPTER
    uint8_t data[2]={0,b};tx(&hspi1,data,2);
#else
    tx(&hspi1,&b,1);
#endif
}
static void lcd_reg(uint8_t command,const uint8_t *data,unsigned n){
    pin(LCD_CS_GPIO_Port,LCD_CS_Pin,0);pin(LCD_DC_GPIO_Port,LCD_DC_Pin,0);lcd_byte(command);
    pin(LCD_DC_GPIO_Port,LCD_DC_Pin,1);while(n--)lcd_byte(*data++);
    pin(LCD_CS_GPIO_Port,LCD_CS_Pin,1);
}

/* The platform owns boot drawing. One scanline costs 640 bytes, no GUI buffer. */
static uint16_t boot_pixels[320];
static unsigned boot_y;
static void boot_begin(unsigned y){boot_y=y;memset(boot_pixels,0,sizeof(boot_pixels));}
static void boot_pixel(int x,int y,unsigned size,uint16_t color){
    if((int)boot_y<y||(int)boot_y>=y+(int)size)return;
    for(unsigned dx=0;dx<size;dx++)if(x+(int)dx>=0&&x+(int)dx<320)boot_pixels[x+dx]=color;
}
#include "boot_font.h"
static void boot_text(int x,int y,const char *text,unsigned scale,uint16_t color){
    const unsigned char *p=(const unsigned char *)text;
    while(*p){unsigned cp=*p++,i;
        if(cp>=0xc0){cp=((cp&31u)<<6)|(*p++&63u);}
        for(i=0;i<sizeof(boot_codepoints)/sizeof(*boot_codepoints);i++)if(boot_codepoints[i]==cp)break;
        if(i<sizeof(boot_codepoints)/sizeof(*boot_codepoints))for(unsigned row=0;row<12;row++)for(unsigned col=0;col<8;col++)
            if(boot_glyphs[i][row]&(1u<<col))boot_pixel(x+(int)(col*scale),y+(int)(row*scale),scale,color);
        x+=(int)(8*scale);
    }
}
static unsigned boot_line_count;
/* Minimal boot console. The font is internal, independent of external assets. */
void board_boot_progress(unsigned stage,unsigned percent){
    static const char *const names[]={"ДИСПЛЕЙ","МИКРОСХЕМА FLASH","СТИРАНИЕ FLASH","ЗАПИСЬ РЕСУРСОВ","ПРОВЕРКА РЕСУРСОВ","МОДУЛЬ","СИСТЕМА ГОТОВА"};
    static unsigned last_stage=99,last_percent=99;
    static unsigned seen,completed;
    char number[5];unsigned start,end,row=0;
    if(stage>6)stage=6;
    if(percent>100)percent=100;
    if(stage==last_stage&&(percent==last_percent||(percent<100&&percent>=last_percent&&percent-last_percent<5)))return;
    for(unsigned i=0;i<stage;i++)if(seen&(1u<<i))row++;
    start=last_stage==99?0:36+row*20;end=last_stage==99?480:start+20;
    seen|=1u<<stage;if(percent==100)completed|=1u<<stage;else completed&=~(1u<<stage);
    last_stage=stage;last_percent=percent;
    number[0]=(char)('0'+percent/100);number[1]=(char)('0'+percent/10%10);number[2]=(char)('0'+percent%10);number[3]='%';number[4]=0;
    for(unsigned y=start;y<end;y++){
        boot_begin(y);
        boot_text(8,16,"ИНИЦИАЛИЗАЦИЯ СИСТЕМЫ",1,61342u);
        row=0;for(unsigned i=0;i<7;i++)if(seen&(1u<<i)){
            boot_text(8,40+row*20,names[i],1,44503u);
            boot_text(272,40+row*20,completed&(1u<<i)?"OK":i==stage?number+(percent<100?(percent<10?2:1):0):"...",1,completed&(1u<<i)?36454u:61342u);row++;
        }
        boot_line_count=row;board_write_rect(0,y,320,1,boot_pixels,320,0);
    }
}
void board_boot_error(unsigned reason){
    static const char *const reasons[]={"ОШИБКА FLASH","ОШИБКА ЧТЕНИЯ FLASH","РЕСУРСЫ НЕ НАЙДЕНЫ","РЕСУРСЫ НЕСОВМЕСТИМЫ","ОШИБКА CRC РЕСУРСОВ","FLASH НЕ ОБНАРУЖЕНА","ОБРЫВ ЗАПИСИ РЕСУРСОВ"};
    if(reason>6)reason=0;
    unsigned first=40+boot_line_count*20;
    for(unsigned y=first;y<480;y++){
        boot_begin(y);
        boot_text(8,first,"ОШИБКА ИНИЦИАЛИЗАЦИИ",1,62154u);
        boot_text(8,first+20,reasons[reason],1,62154u);
        boot_text(8,first+40,reason==1||reason==5?"ПРОВЕРЬТЕ ПОДКЛЮЧЕНИЕ FLASH":"ОБНОВИТЕ РЕСУРСЫ ЭКРАНА",1,44503u);
        boot_text(8,first+60,"ЗАГРУЗЧИК UART ДОСТУПЕН",1,44503u);
        board_write_rect(0,y,320,1,boot_pixels,320,0);
    }
    Error_Handler();
}
void board_lcd_init(void){
    static const uint8_t p0[]={14,14},p1[]={0x41,0},p2[]={0x55},vcom[]={0,0,0,0};
    static const uint8_t gamma0[]={0x0f,0x1f,0x1c,0x0c,0x0f,8,0x48,0x98,0x37,0x0a,0x13,4,0x11,0x0d,0};
    static const uint8_t gamma1[]={0x0f,0x32,0x2e,0x0b,0x0d,5,0x47,0x75,0x37,6,0x10,3,0x24,0x20,0};
    const uint8_t format=LCD_RPI_ADAPTER?0x55:0x66,rotation=0x48;
    pin(TOUCH_CS_GPIO_Port,TOUCH_CS_Pin,1);pin(LCD_CS_GPIO_Port,LCD_CS_Pin,1);
    pin(LCD_RST_GPIO_Port,LCD_RST_Pin,0);HAL_Delay(20);pin(LCD_RST_GPIO_Port,LCD_RST_Pin,1);HAL_Delay(120);
    lcd_reg(1,0,0);HAL_Delay(120);lcd_reg(0x11,0,0);HAL_Delay(120);
    lcd_reg(0x3a,&format,1);lcd_reg(0xc0,p0,2);lcd_reg(0xc1,p1,2);lcd_reg(0xc2,p2,1);
    lcd_reg(0xc5,vcom,4);lcd_reg(0xe0,gamma0,15);lcd_reg(0xe1,gamma1,15);
    lcd_reg(0x20,0,0);lcd_reg(0x36,&rotation,1);board_boot_progress(0,0);lcd_reg(0x29,0,0);HAL_Delay(150);board_boot_progress(0,100);
}
void board_write_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *pixels,uint16_t stride,void *user){
    uint8_t coords[4],bytes[192];uint32_t count=(uint32_t)w*h;uint16_t end;unsigned n=0;
    (void)user;if(!w||!h)return;
    end=x+w-1;coords[0]=x>>8;coords[1]=x;coords[2]=end>>8;coords[3]=end;lcd_reg(0x2a,coords,4);
    end=y+h-1;coords[0]=y>>8;coords[1]=y;coords[2]=end>>8;coords[3]=end;lcd_reg(0x2b,coords,4);
    pin(LCD_CS_GPIO_Port,LCD_CS_Pin,0);pin(LCD_DC_GPIO_Port,LCD_DC_Pin,0);lcd_byte(0x2c);pin(LCD_DC_GPIO_Port,LCD_DC_Pin,1);
    while(count--){uint16_t c=*pixels++;
#if LCD_RPI_ADAPTER
        bytes[n++]=c>>8;bytes[n++]=c;
#else
        bytes[n++]=(c>>8)&0xf8;bytes[n++]=(c>>3)&0xfc;bytes[n++]=(c<<3)&0xf8;
#endif
        if(n==sizeof(bytes)){tx(&hspi1,bytes,n);n=0;}
        if(count&&count%w==0)pixels+=stride-w;
    }
    if(n)tx(&hspi1,bytes,n);
    pin(LCD_CS_GPIO_Port,LCD_CS_Pin,1);
}
static uint16_t touch_adc(uint8_t command){
    uint8_t out[3]={command,0,0},in[3];
    if(HAL_SPI_TransmitReceive(&hspi1,out,in,3,20)!=HAL_OK)Error_Handler();
    return (uint16_t)((((uint16_t)in[1]<<5)|(in[2]>>3))&0x0fffu);
}
static int16_t scale(int raw,int low,int high,int extent){
    int v=(raw-low)*(extent-1)/(high-low);return (int16_t)(v<0?0:v>=extent?extent-1:v);
}
uint8_t board_touch_sample(int16_t *x,int16_t *y,uint16_t *raw_x,uint16_t *raw_y){
    uint32_t old=hspi1.Instance->CR1;uint16_t xs[5],ys[5];unsigned i,j;
    uint8_t down=HAL_GPIO_ReadPin(TOUCH_IRQ_GPIO_Port,TOUCH_IRQ_Pin)==GPIO_PIN_RESET;
    __HAL_SPI_DISABLE(&hspi1);MODIFY_REG(hspi1.Instance->CR1,SPI_CR1_BR,TOUCH_SPI_PRESCALER);__HAL_SPI_ENABLE(&hspi1);
    pin(TOUCH_CS_GPIO_Port,TOUCH_CS_Pin,0);
    /* Keep the panel drivers on during a burst. Discard the conversion after
     * changing axis so the panel can settle before the five useful samples. */
    (void)touch_adc(0xd1);
    for(i=0;i<5;i++)xs[i]=touch_adc(0xd1);
    (void)touch_adc(0x91);
    for(i=0;i<5;i++)ys[i]=touch_adc(0x91);
    (void)touch_adc(0x90); /* Power down and re-enable PENIRQ for the next poll. */
    pin(TOUCH_CS_GPIO_Port,TOUCH_CS_Pin,1);
    __HAL_SPI_DISABLE(&hspi1);hspi1.Instance->CR1=old;
    for(i=1;i<5;i++)for(j=i;j>0;j--){uint16_t t;if(xs[j]<xs[j-1]){t=xs[j];xs[j]=xs[j-1];xs[j-1]=t;}if(ys[j]<ys[j-1]){t=ys[j];ys[j]=ys[j-1];ys[j-1]=t;}}
    if(raw_x)*raw_x=xs[2];
    if(raw_y)*raw_y=ys[2];
    if((unsigned)(xs[3]-xs[1])>TOUCH_MAX_SPREAD||(unsigned)(ys[3]-ys[1])>TOUCH_MAX_SPREAD||
       xs[2]==0||xs[2]==4095||ys[2]==0||ys[2]==4095)down=0;
    *x=scale(TOUCH_SWAP_XY?ys[2]:xs[2],TOUCH_X_MIN,TOUCH_X_MAX,320);
    *y=scale(TOUCH_SWAP_XY?xs[2]:ys[2],TOUCH_Y_MIN,TOUCH_Y_MAX,480);
    if(TOUCH_INVERT_X)*x=319-*x;
    if(TOUCH_INVERT_Y)*y=479-*y;
    return down;
}
uint8_t board_touch(int16_t *x,int16_t *y){
    if(HAL_GPIO_ReadPin(TOUCH_IRQ_GPIO_Port,TOUCH_IRQ_Pin)!=GPIO_PIN_RESET)return 0;
    return board_touch_sample(x,y,0,0);
}
int board_assets_read(uint32_t offset,void *dest,uint32_t length,void *user){
    uint32_t start;
#if defined(DWT)
    /* Accumulate sub-millisecond transfers before converting to ms. */
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    start=DWT->CYCCNT;
#else
    start=HAL_GetTick(); /* Host HAL model. */
#endif
    uint8_t command[4]={3,(uint8_t)(offset>>16),(uint8_t)(offset>>8),(uint8_t)offset};HAL_StatusTypeDef status;
    (void)user;if(length>65535u)return 0;
    pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,0);
    status=HAL_SPI_Transmit(&hspi1,command,4,100);
    if(status==HAL_OK)status=HAL_SPI_Receive(&hspi1,dest,(uint16_t)length,200);
    pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,1);
#if defined(DWT)
    app_debug.render.flash_cycles+=DWT->CYCCNT-start;
    app_debug.render.flash_ms=app_debug.render.flash_cycles/(SystemCoreClock/1000u);
#else
    app_debug.render.flash_ms+=HAL_GetTick()-start;
#endif
    app_debug.render.flash_bytes+=length;
    if(status!=HAL_OK){app_debug.storage.offset=offset;app_debug.loader.stage=LOADER_SPI_ERROR;}
    else if(offset==0&&length>=4){unsigned i;for(i=0;i<4;i++)app_debug.storage.header[i]=((uint8_t *)dest)[i];}
    return status==HAL_OK;
}
#if 1

static void flash_command(uint8_t command){
    pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,0);tx(&hspi1,&command,1);pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,1);
}
static void flash_probe(void){
    uint8_t command[4]={0x9f,0,0,0},reply[4];HAL_StatusTypeDef status;
    flash_command(0xab);HAL_Delay(1);
    pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,0);
    status=HAL_SPI_TransmitReceive(&hspi1,command,reply,4,100);
    pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,1);
    if(status!=HAL_OK){app_debug.loader.stage=LOADER_SPI_ERROR;board_boot_error(1);}
    app_debug.storage.jedec_id=((uint32_t)reply[1]<<16)|((uint32_t)reply[2]<<8)|reply[3];
    if(app_debug.storage.jedec_id==0||app_debug.storage.jedec_id==0xffffffu){
        app_debug.loader.stage=LOADER_BAD_ID;board_boot_error(5);
    }
}
static void flash_wait(void){
    uint32_t start=HAL_GetTick();uint8_t command=5,status;
    do{
        pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,0);tx(&hspi1,&command,1);
        if(HAL_SPI_Receive(&hspi1,&status,1,100)!=HAL_OK)Error_Handler();
        app_debug.storage.status=status;
        pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,1);
        if(HAL_GetTick()-start>3000u)Error_Handler();
    }while(status&1u);
}
static void ack(uint8_t b){if(HAL_UART_Transmit(&huart1,&b,1,100)!=HAL_OK){
    app_debug.loader.uart_error=huart1.ErrorCode;app_debug.loader.stage=LOADER_UART_ERROR;board_boot_error(6);
}}
int board_flash_read(uint32_t at,void *data,uint32_t n){
    if(at<((display_module.assets_size+4095u)&~4095u)||at>0x200000u||n>0x200000u-at)return 0;
    return board_assets_read(at,data,n,0);
}
int board_flash_erase(uint32_t at){
    uint8_t cmd[4]={0x20,(uint8_t)(at>>16),(uint8_t)(at>>8),(uint8_t)at};
    if(at<((display_module.assets_size+4095u)&~4095u)||at>=0x200000u||(at&4095u))return 0;
    flash_wait();flash_command(6);pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,0);
    tx(&hspi1,cmd,4);pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,1);flash_wait();return 1;
}
int board_flash_write(uint32_t at,const void *data,uint32_t n){
    const uint8_t *p=data;
    if(at<((display_module.assets_size+4095u)&~4095u)||at>0x200000u||n>0x200000u-at)return 0;
    while(n){
        uint32_t part=256u-(at&255u);uint8_t cmd[4]={2,(uint8_t)(at>>16),(uint8_t)(at>>8),(uint8_t)at};
        if(part>n)part=n;
        flash_wait();flash_command(6);pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,0);
        tx(&hspi1,cmd,4);tx(&hspi1,p,(uint16_t)part);pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,1);flash_wait();
        p+=part;at+=part;n-=part;
    }return 1;
}
static void receive(uint8_t *dest,uint16_t n,uint32_t timeout){
    if(HAL_UART_Receive(&huart1,dest,n,timeout)!=HAL_OK){
        app_debug.loader.uart_error=huart1.ErrorCode;app_debug.loader.stage=LOADER_UART_ERROR;board_boot_error(6);
    }
    app_debug.loader.rx_bytes+=n;
}
static int boot_request(void){
    static const uint8_t magic[]={'P','C','H','W'};
    uint32_t start=HAL_GetTick();unsigned matched=0;
    app_debug.loader.stage=LOADER_WAIT;
    /* The host has already been sending during LCD reset delays. Discard the
     * stale RX byte/overrun, then find a complete token at any byte boundary. */
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    while(HAL_GetTick()-start<FLASH_BOOT_WINDOW_MS){
        uint8_t b;HAL_StatusTypeDef status=HAL_UART_Receive(&huart1,&b,1,20);
        if(status==HAL_OK){
            app_debug.loader.rx_bytes++;app_debug.loader.last_byte=b;
            /* After reset the host repeats PCHR. Already in the boot window,
             * enter the loader directly rather than resetting again. */
            if(matched==3&&b=='R')return 1;
            matched=b==magic[matched]?matched+1u:(b=='P'?1u:0u);
            if(matched==4)return 1;
        }else if(status!=HAL_TIMEOUT){
            app_debug.loader.uart_error=huart1.ErrorCode;matched=0;
            __HAL_UART_CLEAR_OREFLAG(&huart1);
        }
    }
    app_debug.loader.stage=LOADER_NO_COMMAND;return 0;
}
void board_assets_boot(void){
    if(!display_module.assets_size)return;
    uint8_t length[4],page[256],command[4];uint32_t at,n,size;
    /* Send PCHW during the configured boot window to install resources. */
    board_boot_progress(1,0);
    if(!boot_request()){flash_probe();board_boot_progress(1,100);return;}
    app_debug.loader.stage=LOADER_LENGTH;
    ack('R');
    receive(length,4,2000);
    size=(uint32_t)length[0]|((uint32_t)length[1]<<8)|((uint32_t)length[2]<<16)|((uint32_t)length[3]<<24);
    if(size!=display_module.assets_size){app_debug.loader.stage=LOADER_BAD_LENGTH;ack('E');board_boot_error(3);}
    flash_probe();flash_wait();app_debug.loader.stage=LOADER_ERASE;
    for(at=0;at<size;at+=4096){
        app_debug.storage.offset=at;board_boot_progress(2,at*100/size);
        flash_command(6);command[0]=0x20;command[1]=at>>16;command[2]=at>>8;command[3]=at;
        pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,0);tx(&hspi1,command,4);pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,1);flash_wait();ack('e');
    }
    app_debug.loader.stage=LOADER_PROGRAM;board_boot_progress(2,100);board_boot_progress(3,0);ack('D');
    for(at=0;at<size;at+=n){
        n=size-at;if(n>256)n=256;
        app_debug.storage.offset=at;receive(page,(uint16_t)n,5000);
        flash_command(6);command[0]=2;command[1]=at>>16;command[2]=at>>8;command[3]=at;
        pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,0);tx(&hspi1,command,4);tx(&hspi1,page,(uint16_t)n);
        pin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,1);flash_wait();
        /* ACK grants the host permission to send immediately. Finish LCD work first. */
        board_boot_progress(3,(at+n)*100/size);ack('p');
    }
    /* The same CRC/header check used at startup verifies the installed image. */
    app_debug.loader.stage=LOADER_VERIFY;
    app_debug.storage.ok=(uint8_t)(!display_module.validate||display_module.validate(&board_platform));
    ack(app_debug.storage.ok?'K':'E');
    if(app_debug.storage.ok)app_debug.loader.stage=LOADER_DONE;
}
#else
void board_assets_boot(void){}
#endif
