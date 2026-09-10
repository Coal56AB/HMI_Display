#ifndef HMI_STORAGE_H
#define HMI_STORAGE_H
#include "hmi_config.h"
#include <stdint.h>
#include <string.h>
#define HMI_ASSET_BASE 0x90000000u
typedef int (*HmiAssetReader)(uint32_t offset,void *destination,uint32_t length,void *user);
#if HMI_STORAGE_VIRTUAL
/* Cache misses: decompressions in Lite, SPI reads in ExternalFlash. */
extern uint32_t hmi_storage_loads;
uint8_t hmi_read_u8(const void *source);
uint16_t hmi_read_u16(const uint16_t *source);
void hmi_read_copy(void *destination,const void *source,uint32_t length);
int hmi_storage_init(HmiAssetReader reader,void *user);
/* 0 OK, 1 read failure, 2 missing/header, 3 incompatible image, 4 CRC. */
int hmi_storage_error(void);
#else
static inline uint8_t hmi_read_u8(const void *p){return *(const uint8_t *)p;}
static inline uint16_t hmi_read_u16(const uint16_t *p){return *p;}
static inline void hmi_read_copy(void *d,const void *s,uint32_t n){memcpy(d,s,n);}
static inline int hmi_storage_init(HmiAssetReader reader,void *user){(void)reader;(void)user;return 1;}
static inline int hmi_storage_error(void){return 0;}
#endif
#endif
