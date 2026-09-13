#ifndef JOURNAL_STORE_H
#define JOURNAL_STORE_H
#include <stdint.h>
#define JOURNAL_BASE 0x128000u
#define JOURNAL_BANK 0x6c000u
#define JOURNAL_CAPACITY ((JOURNAL_BANK-4096u)/16u)
typedef struct {uint32_t stamp;uint16_t code,detail;float value;uint16_t crc,commit;} JournalRecord;
typedef struct {
 int (*read)(uint32_t,void *,uint32_t);
 int (*write)(uint32_t,const void *,uint32_t);
 int (*erase)(uint32_t);
} JournalIo;
int journal_init(JournalIo io);
int journal_append(uint32_t stamp,uint16_t code,uint16_t detail,float value);
unsigned journal_used(void);
unsigned journal_generation(void);
unsigned journal_count(unsigned mask);
unsigned journal_level(unsigned code); /* 0 info, 1 warning, 2 fault, 3 hidden legacy UI event */
int journal_read(unsigned index,JournalRecord *record);
int journal_pack(uint32_t cutoff);
int journal_step(void); /* one erase sector or up to 16 copied records */
int journal_busy(void);
unsigned journal_progress(void);
int journal_error(void);
#endif
