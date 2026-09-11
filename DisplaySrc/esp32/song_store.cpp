#include "song_store.h"
#include "song_wire.h"
#include "half_duplex_wire.h"
#include "controller_link.h"
#include "music_box_control.h"
#include "esp_link.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>
#include <cmath>

namespace {
constexpr uint32_t magic=0x31474e53;
struct Header { uint32_t magic,generation,slot,count,duration,crc;uint8_t mask,raw,dirs,reserved;char title[32];uint32_t check; };
static_assert(sizeof(Header)==64,"Disk header");
const esp_partition_t *partition;
Header catalog[SONG_SLOT_COUNT]{};int physical[SONG_SLOT_COUNT];
struct Request { uint8_t seq,len,data[192]; };
struct Reply { uint8_t seq,data[8]; };
QueueHandle_t requests,replies;
Request last_request{};Reply last_reply{};
bool request_busy=false,request_valid=false,upload_open=false;
uint8_t parser[247];unsigned parser_n;
uint32_t progress_at;
bool progress_active;
bool playing=false,stopping=false;
bool boot_done=false,boot_testing=false;uint32_t boot_started=0;
unsigned selected=0,setup_step=0,event_index=0,free_events=256;
uint32_t poll_at=0,rpc_at=0,rpc_wait_at=0,last_tick=0;
bool rpc_waiting=false;
uint8_t rpc[208],rpc_length=0,rpc_seq=0,rpc_command=0;unsigned rpc_count=0,rpc_tries=0;
bool started=false,seek_ready=true,rpc_resume=false;
uint32_t play_offset=0,seek_notes[6]{};
uint8_t resume_events[60];unsigned resume_count=0,resume_index=0;
unsigned range_index=0;
uint8_t range_low=255,range_high=0;
bool range_ready=false;

uint32_t read32(const uint8_t*p){return song_u32(p);}
uint32_t checksum(const void*p,unsigned n){return ~song_crc(0xffffffffu,(const uint8_t*)p,n);}
bool valid(const Header &h) {return h.magic==magic&&h.slot<SONG_SLOT_COUNT&&h.count>0&&h.count<=SONG_MAX_EVENTS&&h.check==checksum(&h,60);}
void refresh() {
    memset(catalog,0,sizeof(catalog));for(auto &p:physical)p=-1;
    if(!partition)return;
    for(unsigned i=0;i<SONG_SLOT_COUNT+1;i++) {Header h{};
        if(esp_partition_read(partition,i*SONG_SLOT_BYTES,&h,sizeof(h))==ESP_OK&&valid(h)&&
            (physical[h.slot]<0||h.generation>catalog[h.slot].generation)) {catalog[h.slot]=h;physical[h.slot]=i;}
    }
}
void show_catalog() {for(unsigned i=0;i<SONG_SLOT_COUNT;i++)music_box_saved_song(i,catalog[i].title,physical[i]>=0,catalog[i].duration);}
void report(const Request &r,uint8_t error,uint8_t stage,uint8_t percent,uint32_t offset) {
    Reply reply{};reply.seq=r.seq;reply.data[0]=r.data[0];reply.data[1]=error;
    reply.data[2]=stage;reply.data[3]=percent;song_put32(reply.data+4,offset);
    xQueueSend(replies,&reply,portMAX_DELAY);
}
void worker(void*) {
    Header incoming{};int target=-1;uint32_t offset=0,total=0,last_at=0;bool ended=false;
    Request r{};
    for(;;) {
        xQueueReceive(requests,&r,portMAX_DELAY);
        uint8_t error=0,stage=2,percent=10;const uint8_t *p=r.data+1;unsigned n=r.len-1;
        if(!partition)error=4;
        else if(r.data[0]==SONG_BEGIN) {
            target=-1;offset=0;ended=false;last_at=0;
            if(n!=48||p[0]>=SONG_SLOT_COUNT||!read32(p+1)||read32(p+1)>SONG_MAX_EVENTS||!p[13]||p[13]>63||p[14]>7||p[15]>63)error=3;
            else {
                refresh();incoming={};incoming.magic=magic;incoming.slot=p[0];incoming.count=read32(p+1);
                incoming.duration=read32(p+5);incoming.crc=read32(p+9);incoming.mask=p[13];incoming.raw=p[14];incoming.dirs=p[15];
                memcpy(incoming.title,p+16,32);incoming.title[31]=0;incoming.generation=1;
                for(const auto &h:catalog)if(h.generation>=incoming.generation)incoming.generation=h.generation+1;
                for(unsigned i=0;i<SONG_SLOT_COUNT+1;i++){bool used=false;for(int j:physical)if((int)i==j)used=true;if(!used){target=(int)i;break;}}
                total=incoming.count*10;
                report(r,255,1,1,0);
                if(target<0)error=5;
                else for(unsigned a=0;a<SONG_SLOT_BYTES;a+=4096) {
                    if(esp_partition_erase_range(partition,target*SONG_SLOT_BYTES+a,4096)!=ESP_OK){error=9;break;}
                    if((a&16383)==0)report(r,255,1,1+9*a/SONG_SLOT_BYTES,0);
                }
            }
        } else if(r.data[0]==SONG_DATA) {
            stage=2;
            if(target<0||n<14||(n-4)%10||read32(p)!=offset||offset+n-4>total)error=6;
            else {
                for(unsigned i=4;i<n;i+=10) {
                    uint32_t at=read32(p+i),value=read32(p+i+6);uint8_t m=p[i+4],op=p[i+5];
                    if(ended||at<last_at||at>86400000u||op>2||
                       (op==2?(m!=255||value!=0||offset+i-4+10!=total):
                       (m>=6||!(incoming.mask&(1u<<m))||(op==0?value!=0:value<20000||value>4000000)))){error=3;break;}
                    last_at=at;ended=op==2;
                }
                if(!error && esp_partition_write(partition,target*SONG_SLOT_BYTES+SONG_DATA_OFFSET+offset,p+4,n-4)!=ESP_OK)error=9;
                if(!error)offset+=n-4;
            }
            percent=10+75*(uint64_t)offset/(total?total:1);
        } else if(r.data[0]==SONG_COMMIT) {
            stage=3;percent=85;
            if(n||target<0||offset!=total||!ended)error=6;
            else {
                uint8_t bytes[1024];uint32_t crc=0xffffffffu;
                for(uint32_t a=0;a<total;a+=sizeof(bytes)) {
                    unsigned count=total-a;if(count>sizeof(bytes))count=sizeof(bytes);
                    if(esp_partition_read(partition,target*SONG_SLOT_BYTES+SONG_DATA_OFFSET+a,bytes,count)!=ESP_OK){error=9;break;}
                    crc=song_crc(crc,bytes,count);
                    if((a&8191)==0)report(r,255,3,85+14*(uint64_t)a/total,offset);
                }
                if(!error && ~crc!=incoming.crc)error=7;
                if(!error) {
                    incoming.check=checksum(&incoming,60);
                    if(esp_partition_write(partition,target*SONG_SLOT_BYTES,&incoming,sizeof(incoming))!=ESP_OK)error=9;
                    Header verify{};
                    if(!error&&(esp_partition_read(partition,target*SONG_SLOT_BYTES,&verify,sizeof(verify))!=ESP_OK||memcmp(&verify,&incoming,sizeof(verify))))error=9;
                    if(!error){stage=4;percent=100;}
                }
            }
            target=-1;
        } else error=2;
        if(error){target=-1;stage=6;}
        report(r,error,stage,percent,offset);
    }
}
void send_reply(const Reply &r) {uint8_t bytes[16];auto n=control::encode(bytes,SONG_REPLY,r.data,8,r.seq);esp_link_send(bytes,n);}
void command(uint8_t cmd,const uint8_t *p=nullptr,unsigned n=0) {
    uint8_t payload[200];payload[0]=cmd;if(n)memcpy(payload+1,p,n);
    rpc_command=cmd;rpc_length=control::encode(rpc,SONG_ENGINE,payload,n+1,++rpc_seq);
    rpc_at=rpc_wait_at=last_tick;rpc_waiting=true;
    rpc_tries=esp_link_send(rpc,rpc_length)?1:0;
}
void stop() {playing=false;stopping=true;rpc_length=0;command(4);music_box_saved_playing(false);}
void playback_failed(unsigned error) {
    playing=stopping=boot_testing=false;rpc_length=0;music_box_saved_playing(false);
    const char *reason=error==12?"Управление занято программой ПК":
        error==2?"Обновите прошивку STM32":
        error==8?"STM32: тайм-аут управления":
        error==4?"STM32 отклонила настройку моторов":
        error==5?"Переполнена очередь нот":
        error==6?"Ноты поступили слишком поздно":
        error==10?"Не хватило данных мелодии":
        error==9?"Ошибка линии UART":
        error==11?"Пропущен импульс мотора":
        "Повреждены данные мелодии";
    music_box_playback_error(reason);
}
void on_frame(const uint8_t*b,unsigned n) {
    if(b[4]==SONG_RELAY&&b[2]>=1&&b[2]<=192) {
        Request r{};r.seq=b[3];r.len=b[2];memcpy(r.data,b+5,r.len);
        if(request_valid&&r.seq==last_request.seq&&r.len==last_request.len&&!memcmp(r.data,last_request.data,r.len)) {
            if(!request_busy)send_reply(last_reply);
            return;
        }
        if(request_busy)return;
        if(playing||stopping){Reply reject{};reject.seq=r.seq;reject.data[0]=r.data[0];reject.data[1]=4;reject.data[2]=6;send_reply(reject);return;}
        if(xQueueSend(requests,&r,0)==pdTRUE) {
            last_request=r;request_valid=request_busy=true;progress_active=true;upload_open=true;
            if(r.data[0]==SONG_BEGIN)music_box_save_progress(1,0);
        }
    } else if(b[4]==SONG_ENGINE_REPLY&&rpc_length&&b[3]==rpc_seq&&b[2]>=2&&b[5]==rpc_command) {
        rpc_length=0;
        if(b[6]) {playback_failed(b[6]);return;}
        if(stopping){stopping=false;boot_testing=false;return;}
        if(!playing)return;
        if(rpc_command==18&&b[2]==4){if(rpc_resume)resume_index+=rpc_count;else event_index+=rpc_count;free_events=b[7]|unsigned(b[8])<<8;}
        else if(rpc_command==3&&b[2]==54) {
            free_events=256-(b[13]|unsigned(b[14])<<8);
            if(started&&!b[11]){if(b[12])playback_failed(b[12]);else stop();}
        } else if(rpc_command==16)started=true;
    }
    (void)n;
}
}
void song_store_start() {
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"songs");
    if(partition&&partition->size<(SONG_SLOT_COUNT+1)*SONG_SLOT_BYTES)partition=nullptr;
    refresh();requests=xQueueCreate(1,sizeof(Request));replies=xQueueCreate(8,sizeof(Reply));
    configASSERT(requests&&replies);configASSERT(xTaskCreate(worker,"song-store",4096,nullptr,2,nullptr)==pdPASS);
}
bool song_store_busy(){return playing||stopping||upload_open||boot_testing;}
void song_store_boot(bool ready,bool pc_connected) {
    if(boot_done||!ready||!music_box_screen_ready())return;
    boot_done=true;
    if(pc_connected||request_busy)return;
    boot_testing=true;boot_started=0;music_box_save_progress(5,0);command(21);
}
bool song_store_action(unsigned action,unsigned slot,uint32_t position) {
    if(action==0&&(playing||boot_testing)){boot_testing=false;stop();return true;}
    const bool seeking=action==2&&playing;
    if(action!=20&&!seeking)return false;
    if(playing&&!seeking){stop();return true;}
    if(seeking)slot=selected;
    if(slot>=SONG_SLOT_COUNT||physical[slot]<0||upload_open||boot_testing)return true;
    selected=slot;playing=true;started=false;event_index=setup_step=0;free_events=256;rpc_length=0;poll_at=0;
    if(!seeking){range_index=0;range_low=255;range_high=0;range_ready=false;music_box_saved_range(255,255);}
    play_offset=seeking?position:0;
    if(catalog[slot].duration&&play_offset>=catalog[slot].duration)play_offset=catalog[slot].duration-1;
    seek_ready=play_offset==0;resume_count=resume_index=0;memset(seek_notes,0,sizeof(seek_notes));
    music_box_saved_offset(play_offset);music_box_saved_playing(true);return true;
}
void song_store_feed(uint8_t byte) {
    if(parser_n==sizeof(parser)){memmove(parser,parser+1,--parser_n);}
    parser[parser_n++]=byte;
    while(parser_n>=7) {
        unsigned n=parser[2]+7u;
        if(parser[0]==0xa5&&parser[1]==0x5a&&n<=sizeof(parser)) {
            if(parser_n<n)return;
            if(hd_crc(parser+2,n-4)==(parser[n-2]|unsigned(parser[n-1])<<8)) {
                on_frame(parser,n);parser_n-=n;memmove(parser,parser+n,parser_n);continue;
            }
        }
        memmove(parser,parser+1,--parser_n);
    }
}
void song_store_tick(uint32_t now) {
    last_tick=now;
    static bool shown=false;if(!shown){show_catalog();shown=true;}
    Reply r{};while(xQueueReceive(replies,&r,0)==pdTRUE) {
        last_reply=r;music_box_save_progress(r.data[2],r.data[3]);progress_at=now;send_reply(r);
        if(r.data[1]!=255){request_busy=false;if(r.data[1]||r.data[0]==SONG_COMMIT)upload_open=false;if(r.data[2]==4){refresh();show_catalog();}}
    }
    if(upload_open&&!request_busy&&now-progress_at>10000){upload_open=false;music_box_save_progress(6,last_reply.data[3]);}
    if(progress_active&&!upload_open&&!request_busy&&now-progress_at>15000){progress_active=false;music_box_save_progress(0,0);}
    if(rpc_length) {
        if(!rpc_waiting){rpc_waiting=true;rpc_wait_at=now;rpc_at=now-100;}
        if(now-rpc_wait_at>=2000) {
            char reason[128];
            snprintf(reason,sizeof(reason),rpc_tries?"Нет ACK команды %u (пакетов %u)":"Очередь UART занята: команда %u (%u)",rpc_command,rpc_tries);
            playing=stopping=boot_testing=false;rpc_length=0;
            music_box_saved_playing(false);music_box_playback_error(reason);return;
        }
        if(now-rpc_at>=100) {
            rpc_at=now;
            if(esp_link_send(rpc,rpc_length))++rpc_tries;
        }
        return;
    }
    if(boot_testing) {
        if(!boot_started)boot_started=now;
        if(now-boot_started>=2200){boot_testing=false;stop();music_box_save_progress(0,0);}
        else if(now-poll_at>=50){poll_at=now;music_box_save_progress(5,(now-boot_started)*100/2200);command(3);}
        return;
    }
    if(!playing)return;
    const auto &h=catalog[selected];uint8_t p[180]{};
    if(!range_ready && setup_step>0) {
        // Inspect the entire stored song before playback, in bounded UI-friendly blocks.
        uint8_t scan[640];unsigned count=h.count-range_index;if(count>64)count=64;
        if(esp_partition_read(partition,physical[selected]*SONG_SLOT_BYTES+SONG_DATA_OFFSET+range_index*10,scan,count*10)!=ESP_OK){stop();music_box_playback_error("Ошибка чтения памяти");return;}
        for(unsigned i=0;i<count;++i)if(scan[i*10+5]==1) {
            uint32_t frequency=read32(scan+i*10+6);
            if(frequency) {
                int note=(int)lroundf(69.0f+12.0f*log2f(frequency/440000.0f));
                if(note>=0&&note<128){if(note<range_low)range_low=note;if(note>range_high)range_high=note;}
            }
        }
        range_index+=count;
        if(range_index==h.count){range_ready=true;music_box_saved_range(range_low,range_high);}
        if(now-poll_at>=100){poll_at=now;command(3);}
        return;
    }
    if(setup_step<18) {
        unsigned s=setup_step++;
        if(s==0)command(4);
        else if(s==1){p[0]=h.mask;command(5,p,1);}
        else if(s==2){p[0]=h.raw;command(12,p,1);}
        else if(s==3)command(14,p,1);
        else if(s==4)command(13,p,1);
        else if(s<17){unsigned m=(s-5)/2;if(!(h.mask&(1u<<m)))return;p[0]=m;p[1]=(s&1)?((h.dirs>>m)&1):((h.mask>>m)&1);command((s&1)?11:6,p,2);}
        else command(19);
    } else if(!seek_ready) {
        // Scan bounded blocks while stopped, keeping the UI responsive.
        uint8_t scan[640];unsigned count=h.count-event_index;if(count>64)count=64;
        if(!count || esp_partition_read(partition,physical[selected]*SONG_SLOT_BYTES+SONG_DATA_OFFSET+event_index*10,scan,count*10)!=ESP_OK){stop();music_box_playback_error("Ошибка чтения памяти");return;}
        unsigned i=0;
        for(;i<count;++i) {
            const uint8_t *e=scan+i*10;uint32_t at=read32(e);
            if(at>=play_offset||e[5]==2) {
                if(e[5]==2&&at<play_offset){play_offset=at;music_box_saved_offset(at);}
                seek_ready=true;break;
            }
            if(e[4]<6)seek_notes[e[4]]=e[5]==1?read32(e+6):0;
        }
        event_index+=i;
        if(seek_ready)for(unsigned m=0;m<6;++m)if(seek_notes[m]) {
            uint8_t *e=resume_events+10*resume_count++;song_put32(e,0);e[4]=m;e[5]=1;song_put32(e+6,seek_notes[m]);
        }
        if(now-poll_at>=100){poll_at=now;command(3);}
    } else if(resume_index<resume_count) {
        rpc_resume=true;rpc_count=resume_count-resume_index;if(rpc_count>4)rpc_count=4;
        command(18,resume_events+resume_index*10,rpc_count*10);
    } else if(event_index<h.count && free_events>=4 && (!started||free_events>64)) {
        // Keep requests within 48 bytes; enqueue immediately on each preceding ACK.
        rpc_count=h.count-event_index;if(rpc_count>4)rpc_count=4;
        if(esp_partition_read(partition,physical[selected]*SONG_SLOT_BYTES+SONG_DATA_OFFSET+event_index*10,p,rpc_count*10)!=ESP_OK){stop();music_box_playback_error("Ошибка чтения памяти");return;}
        for(unsigned i=0;i<rpc_count;++i)song_put32(p+i*10,read32(p+i*10)-play_offset);
        rpc_resume=false;command(18,p,rpc_count*10);
    } else if(!started)command(16);
    else if(now-poll_at>=50){poll_at=now;command(3);}
}
