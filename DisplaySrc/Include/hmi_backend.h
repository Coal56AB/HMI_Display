#ifndef HMI_BACKEND_H
#define HMI_BACKEND_H

#include <stdint.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} HmiRect;

typedef void (*HmiFlushRectFn)(uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height,
                               const uint16_t *rgb565, uint16_t stride,
                               void *user);

#endif
