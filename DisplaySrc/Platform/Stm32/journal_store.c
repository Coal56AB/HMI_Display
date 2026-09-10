#include "journal_store.h"
#include <string.h>
typedef struct {uint32_t magic,generation,reserved;uint16_t crc,commit;} Header;
static JournalIo io;
static uint32_t base,generation,used,stage,erase_at,copy_at,copied,cutoff;
static int failed;
static unsigned counts[4],new_counts[4];
unsigned journal_level(unsigned code){
 if(code==114||(code>=150&&code<=154)||code>=200)return 3;
 return code==115?2:code==100||code==199?1:0;
}
static uint16_t crc(const void *data,unsigned n){
 const uint8_t *p=data;uint16_t c=0xffff;unsigned i;
 while(n--){c^=(uint16_t)*p++<<8;for(i=0;i<8;i++)c=(uint16_t)((c<<1)^((c&0x8000)?0x1021:0));}return c;
}
static uint32_t other(void){return base==JOURNAL_BASE?JOURNAL_BASE+JOURNAL_BANK:JOURNAL_BASE;}
static int valid_header(Header *h){return h->magic==0x314c4e4au&&h->commit==0xa55a&&h->crc==crc(h,12);}
static int write_header(uint32_t at,uint32_t gen){
 Header h={0x314c4e4au,gen,0,0,0xa55a},check;h.crc=crc(&h,12);
 return io.write(at,&h,14)&&io.write(at+14,&h.commit,2)&&io.read(at,&check,16)&&memcmp(&h,&check,16)==0;
}
__attribute__((weak)) void journal_init_progress(unsigned done,unsigned total){(void)done;(void)total;}
int journal_init(JournalIo access){
 Header a,b;JournalRecord r;io=access;failed=0;stage=used=0;memset(counts,0,sizeof(counts));
 if(!io.read(JOURNAL_BASE,&a,16)||!io.read(JOURNAL_BASE+JOURNAL_BANK,&b,16))return !(failed=1);
 if(!valid_header(&a)&&!valid_header(&b)){
  base=JOURNAL_BASE;generation=1;
  /* Fresh initialization is resumable: commit header only after all erases. */
  for(unsigned at=0;at<JOURNAL_BANK;at+=4096){journal_init_progress(at,JOURNAL_BANK*2);if(!io.erase(base+at))return !(failed=1);}
  if(!write_header(base,generation))return !(failed=1);
 }else{
  base=valid_header(&b)&&(!valid_header(&a)||(int32_t)(b.generation-a.generation)>0)?JOURNAL_BASE+JOURNAL_BANK:JOURNAL_BASE;
  generation=base==JOURNAL_BASE?a.generation:b.generation;
 }
 for(unsigned i=0;i<JOURNAL_CAPACITY;i++){
  if((i&255u)==0)journal_init_progress(JOURNAL_CAPACITY+i,JOURNAL_CAPACITY*2);
  if(!io.read(base+4096+i*16,&r,16))return !(failed=1);
  const uint8_t *p=(const uint8_t *)&r;unsigned j;for(j=0;j<16&&p[j]==255;j++){}
  if(j!=16)used=i+1; /* Torn records consume a slot; never overwrite NOR bits. */
  if(r.commit==0xa55a&&r.crc==crc(&r,12))counts[journal_level(r.code)]++;
 }
 return 1;
}
int journal_append(uint32_t stamp,uint16_t code,uint16_t detail,float value){
 JournalRecord r={stamp,code,detail,value,0,0xa55a},check;uint32_t at;
 if(failed||used>=JOURNAL_CAPACITY)return 0;
 r.crc=crc(&r,12);at=base+4096+used++*16;
 if(!io.write(at,&r,14)||!io.write(at+14,&r.commit,2)||!io.read(at,&check,16)||memcmp(&r,&check,16)){failed=1;return 0;}counts[journal_level(code)]++;return 1;
}
unsigned journal_used(void){return used;}
unsigned journal_generation(void){return generation;}
unsigned journal_count(unsigned mask){unsigned n=0;for(unsigned i=0;i<3;i++)if(mask&(1u<<i))n+=counts[i];return n;}
int journal_read(unsigned index,JournalRecord *r){
 if(index>=used)return 0;
 if(!io.read(base+4096+index*16,r,16)){failed=1;return 0;}
 return r->commit==0xa55a&&r->crc==crc(r,12);
}
int journal_pack(uint32_t date){
 if(stage||failed)return 0;
 cutoff=date;stage=1;erase_at=copy_at=copied=0;memset(new_counts,0,sizeof(new_counts));return 1;
}
int journal_step(void){
 if(!stage||failed)return 0;
 if(stage==1){
  if(!io.erase(other()+erase_at)){failed=1;return 0;}
  erase_at+=4096;if(erase_at==JOURNAL_BANK)stage=2;return 1;
 }
 for(unsigned n=0;n<16&&copy_at<used;n++,copy_at++){
  JournalRecord r;
  if(journal_read(copy_at,&r)&&(cutoff!=0xffffffffu)&&(!r.stamp||r.stamp>cutoff)){
   JournalRecord check;uint32_t at=other()+4096+copied*16;
   if(!io.write(at,&r,16)||!io.read(at,&check,16)||memcmp(&r,&check,16)){failed=1;return 0;}copied++;new_counts[journal_level(r.code)]++;
  }
 }
 if(copy_at==used){
  if(!write_header(other(),generation+1)){failed=1;return 0;}
  base=other();generation++;used=copied;stage=0;memcpy(counts,new_counts,sizeof(counts));
 }
 return 1;
}
int journal_busy(void){return stage!=0;}
int journal_error(void){return failed;}

unsigned journal_progress(void){return !stage?100:stage==1?erase_at*50/JOURNAL_BANK:50+(used?copy_at*50/used:50);}
