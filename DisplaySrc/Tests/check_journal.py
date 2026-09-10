import sys,os
from pathlib import Path
MODULE=Path(__file__).resolve().parents[1]
os.chdir(MODULE)
(MODULE/".build").mkdir(exist_ok=True)
sys.path.insert(0,str(MODULE/"Simulator"))
"""NOR model integration: persistent log, filters, date packing, power loss."""
import os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 os.chdir(ROOT);Path('.build').mkdir(exist_ok=True)
 src=Path('.build/check_journal.c');src.write_text(r'''#include "journal_store.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char flash[0x200000],saved[0x200000];
static int rd(uint32_t at,void *p,uint32_t n){assert(at>=JOURNAL_BASE&&at+n<=sizeof(flash));memcpy(p,flash+at,n);return 1;}
static int wr(uint32_t at,const void *p,uint32_t n){const unsigned char *b=p;assert((at&255)+n<=256);for(unsigned i=0;i<n;i++){assert((flash[at+i]&b[i])==b[i]);flash[at+i]&=b[i];}return 1;}
static int er(uint32_t at){assert(!(at&4095)&&at>=JOURNAL_BASE);memset(flash+at,255,4096);return 1;}
int main(void){
 JournalIo io={rd,wr,er};JournalRecord r;memset(flash,255,sizeof(flash));assert(sizeof(r)==16);assert(journal_init(io));
 for(unsigned i=0;i<100;i++)assert(journal_append(1000+i,i%3==0?115:i%3==1?100:113,i,(float)i));
 assert(journal_count(7)==100&&journal_count(4)==34&&journal_count(2)==33&&journal_count(1)==33);
 assert(journal_init(io)&&journal_used()==100);assert(journal_read(99,&r)&&r.value==99);
 memcpy(saved,flash,sizeof(flash));
 for(unsigned stop=0;stop<120;stop++){
  memcpy(flash,saved,sizeof(flash));assert(journal_init(io));assert(journal_pack(1049));
  for(unsigned j=0;j<stop&&journal_busy();j++)journal_step();
  assert(journal_init(io));assert(journal_used()==100||journal_used()==50);
  assert(journal_read(0,&r));assert(r.stamp==(journal_used()==100?1000:1050));
 }
 memcpy(flash,saved,sizeof(flash));assert(journal_init(io));assert(journal_pack(1049));while(journal_busy())assert(journal_step());
 assert(journal_used()==50&&journal_count(7)==50);assert(journal_init(io)&&journal_used()==50);
 assert(journal_pack(0xffffffffu));while(journal_busy())assert(journal_step());assert(journal_used()==0);
 for(unsigned i=0;i<JOURNAL_CAPACITY;i++)assert(journal_append(i+1,113,0,0));
 assert(!journal_append(9,113,0,0));assert(journal_init(io)&&journal_used()==JOURNAL_CAPACITY);
 assert(journal_pack(JOURNAL_CAPACITY-10));while(journal_busy())assert(journal_step());assert(journal_used()==10);
 assert(journal_append(90000,113,1,7));assert(journal_init(io)&&journal_used()==11);
 unsigned visible=journal_count(7);assert(journal_append(90001,210,0,0));assert(journal_count(7)==visible);
 assert(journal_init(io)&&journal_count(7)==visible&&journal_level(210)==3);
 puts("Journal PASS: NOR programming, 27392 records, restart, type filters, date packing, clear, 120 power-loss points, append after packing");return 0;
}
''',encoding='utf-8')
 env=os.environ.copy();env['PATH']='C:/mingw64/bin;'+env['PATH']
 subprocess.run(['C:/mingw64/bin/gcc.exe','-std=c99','-Os','-Wall','-Wextra','-Werror','-IPlatform/Stm32','Platform/Stm32/journal_store.c',str(src),'-o','.build/check_journal.exe'],check=True,env=env)
 subprocess.run(['.build/check_journal.exe'],check=True,env=env)
if __name__=='__main__':main()
