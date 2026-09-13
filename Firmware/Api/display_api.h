#ifndef DISPLAY_API_H
#define DISPLAY_API_H
#include <stdint.h>
#define DISPLAY_API_VERSION 2u

typedef void (*DisplayWriteRect)(uint16_t,uint16_t,uint16_t,uint16_t,const uint16_t *,uint16_t,void *);
typedef int (*DisplayReadAssets)(uint32_t,void *,uint32_t,void *);
typedef struct {
    uint32_t version;
    uint16_t width,height;
    DisplayWriteRect write_rect;
    DisplayReadAssets read_assets;
    /* Byte offsets in platform storage; writes/erases cannot overlap resources. */
    int (*flash_read)(uint32_t,void *,uint32_t);
    int (*flash_write)(uint32_t,const void *,uint32_t);
    int (*flash_erase)(uint32_t);
    uint32_t (*now_ms)(void);
    void (*send)(const uint8_t *,uint16_t);
    void (*reset)(void);
    void (*boot_progress)(unsigned,unsigned);
} DisplayPlatform;

typedef enum {DISPLAY_TOUCH,DISPLAY_RX_BYTE,DISPLAY_RX_ERROR,DISPLAY_TOUCH_CANCEL} DisplayEventType;
typedef struct {
    DisplayEventType type;
    uint32_t now_ms;
    int16_t x,y;
    uint8_t down,byte;
} DisplayEvent;
/* Resource callbacks are used before init, including after a UART upload.
 * validate returns nonzero on success. error: 0 OK, 1 read, 2 missing,
 * 3 incompatible, 4 CRC. No external assets: size=0, callbacks may be NULL.
 * Counters are optional diagnostics; they must not change module state. */
typedef struct {
    uint32_t api_version,assets_size;
    int (*validate)(const DisplayPlatform *);
    unsigned (*error)(void);
    unsigned (*asset_loads)(void);
} DisplayModule;
extern const DisplayModule display_module;
void display_init(const DisplayPlatform *platform);
/* RX events may arrive in IRQ context: enqueue only, never draw or use SPI.
 * TOUCH events and step run in the main loop, never during a display flush. */
void display_event(const DisplayEvent *event);
void display_step(uint32_t now_ms);
#endif
