#include "journal_app.h"
#include "journal_store.h"
#include "telemetry.h"
#include "module_services.h"

#include <stdio.h>
#include <string.h>
static HmiUi *ui;
static HmiJournalItem rows[7];
static char dates[7][20],messages[7][56];
static struct {uint16_t code,detail;float value;} pending[16];
static unsigned queued,dropped,refresh;
static uint32_t wall,wall_tick,last_minute=0xffffffffu,pack_date;
static uint8_t pack_requested;
static int leap(unsigned y){return y%4==0&&(y%100!=0||y%400==0);}
static unsigned days(unsigned y,unsigned m){static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31};return d[m-1]+(m==2&&leap(y));}
static uint32_t cutoff(unsigned date){
 unsigned y=2000+date/10000,m=date/100%100,d=date%100;uint32_t n=0;
 if(!date)return 0xffffffffu;
 if(m<1||m>12||d<1||d>days(y,m))return 0;
 for(unsigned a=1970;a<y;a++)n+=365+leap(a);
 for(unsigned a=1;a<m;a++)n+=days(y,a);
 return (n+d)*86400u-1u;
}
static void stamp_text(uint32_t t,char *out){
 unsigned y=1970,m=1,d=t/86400u,secs=t%86400u;
 if(!t){strcpy(out,"--.--.---- --:--");return;}
 while(d>=365u+(unsigned)leap(y)){d-=365u+leap(y);y++;}
 while(d>=days(y,m)){d-=days(y,m);m++;}
 (void)snprintf(out,20,"%02u.%02u.%04u %02u:%02u",d+1,m,y,secs/3600,secs/60%60);
}
static const char *name(unsigned code){
 switch(code){
 case 100:return "Связь потеряна";case 101:return "Связь восстановлена";
 case 110:return "Стоп";case 111:return "Заряд";case 112:return "Готов";
 case 113:return "Пуск";case 114:return "Регулирование";case 115:return "Авария контроллера";
 case 120:return "Режим разряда";case 130:return "Включение панели";
 case 140:return "Упаковка завершена";case 199:return "Пропущены события";
 case 150:return "Модуляция";case 151:return "Частота";case 152:return "Лимит тока";
 case 153:return "Режим управления";case 154:return "Выбор энкодера";
 case 201:return "Уставка";case 202:return "Настройка";case 203:return "График";
 case 206:return "Время";case 207:return "Датчик ROM";case 210:return "Выбор / действие";
 default:return "Действие";
 }
}
static void enqueue(unsigned code,unsigned detail,float value){
 if(queued==16){dropped++;return;}
 pending[queued].code=(uint16_t)code;pending[queued].detail=(uint16_t)detail;pending[queued++].value=value;
}
void telemetry_notice(unsigned code,float value){
 /* Keep process transitions, faults and connectivity; selection and tuning are not process events. */
 if(code==114||(code>=150&&code<=154))return;
 enqueue(code,0,value);
}
void telemetry_clock(uint32_t stamp){if(stamp>=1577836800u){wall=stamp;wall_tick=module_platform->now_ms();}}
void journal_app_init(HmiUi *state){
 ui=state;queued=0;wall=0;refresh=1;
 (void)journal_init((JournalIo){module_platform->flash_read,module_platform->flash_write,module_platform->flash_erase});
 ui->state.journal=rows;ui->state.journal_paged=1;ui->state.journal_count=0;
 enqueue(130,0,0);
}
void journal_app_event(const HmiEvent *e){
 refresh=1;
 if(e->type==HMI_EVENT_JOURNAL_CLEAR){pack_date=cutoff((unsigned)e->value);pack_requested=1;return;}
 /* Navigation, encoder selection and display preferences do not belong in the drive journal. */
}
static void load_page(void){
 unsigned total=0,written=0,skip=(ui->state.journal_page-1u)*7u;
 JournalRecord r;
 for(unsigned n=journal_used();n>0&&written<7;n--)if(journal_read(n-1,&r)){
  unsigned level=journal_level(r.code);if(!(ui->settings[4]&(1u<<level)))continue;
  if(total>=skip&&written<7){
   stamp_text(r.stamp,dates[written]);
   if(r.code==120)strcpy(messages[written],r.value==16?"Разряд через инвертор":r.value==8?"Самостоятельный разряд":"Разряд завершён");
   else if(r.code>=110&&r.code<=115)(void)snprintf(messages[written],56,"%s: %.0f В",name(r.code),(double)r.value);
   else (void)snprintf(messages[written],56,"%s",name(r.code));
   rows[written]=(HmiJournalItem){dates[written],level==2?"fault":level==1?"warn":"info",messages[written]};written++;
  }total++;
 }
 total=journal_count(ui->settings[4]);ui->state.journal_count=(uint16_t)total;
 unsigned pages=(total+6)/7;if(!pages)pages=1;
 if(ui->state.journal_page>pages){ui->state.journal_page=(uint16_t)pages;load_page();return;}
 if(ui->state.page==HMI_PAGE_JOURNAL)hmi_invalidate((HmiRect){7,101,306,328});
}
void journal_app_tick(uint32_t now){
 uint32_t stamp=wall?wall+(now-wall_tick)/1000u:0;
 if(pack_requested){
  pack_requested=0;
  if(!pack_date){ui->state.dialog=HMI_DIALOG_KEYPAD;ui->edit_error=1;hmi_invalidate_all();}
  else (void)journal_pack(pack_date);
 }
 if(queued){
  if(journal_append(stamp,pending[0].code,pending[0].detail,pending[0].value))refresh=1;
  queued--;memmove(pending,pending+1,queued*sizeof(pending[0]));
 }
 if(dropped&&queued<16){enqueue(199,0,(float)dropped);dropped=0;}
 if(journal_busy()){
  journal_step();if(!journal_busy()){refresh=1;enqueue(140,0,(float)journal_used());}
 }
 uint8_t warning=journal_error()?3:journal_busy()?2:journal_used()*10u>=JOURNAL_CAPACITY*9u?1:0;
 if(warning!=ui->state.journal_warning){ui->state.journal_warning=warning;hmi_invalidate_all();}
 uint8_t progress=(uint8_t)journal_progress();
 if(progress!=ui->state.journal_progress){ui->state.journal_progress=progress;if(warning==2)hmi_invalidate((HmiRect){20,180,280,120});}
 if(wall&&stamp/60!=last_minute){
  last_minute=stamp/60;(void)snprintf(ui->clock,6,"%02u:%02u",(unsigned)(stamp/3600%24),(unsigned)(stamp/60%60));
  hmi_invalidate((HmiRect){258,3,54,29});
 }
 if(refresh){refresh=0;load_page();}
}
