#include "midi_app.h"
#include "midi_board_config.h"
#include "usb_midi_host.h"
#include "controller_link.h"
#include "esp_link.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>
namespace {
struct Input { music::Event event; uint32_t generation; };
struct Ack { uint8_t sequence,result; };
QueueHandle_t events,acks;
std::atomic<bool> connected{false},overflow{false};
std::atomic<bool> enabled{false},quiet{true};
std::atomic<uint32_t> generation{0};
void incoming(const music::Event &e) {Input input{e,generation.load()};if(xQueueSend(events,&input,0)!=pdTRUE)overflow=true;}
void connection(bool present) {connected=present;generation.fetch_add(1);}
void task(void*) {
    uint8_t frame[208]{},seq=0;unsigned length=0;
    uint32_t sent=0,last=0,epoch=~0u,blocked_until=0;bool reset=true;
    for(;;) {
        if(!enabled) {
            xQueueReset(events);xQueueReset(acks);length=0;reset=true;epoch=~0u;
            quiet=true;vTaskDelay(pdMS_TO_TICKS(1));continue;
        }
        quiet=false;
        uint32_t now=uint32_t(esp_timer_get_time()/1000),current=generation.load();
        if(epoch!=current){epoch=current;length=0;reset=true;}
        if(overflow.exchange(false)){xQueueReset(events);length=0;reset=true;}
        Ack ack;
        while(xQueueReceive(acks,&ack,0)==pdTRUE)if(length&&ack.sequence==seq){
            length=0;last=now;
            if(ack.result){xQueueReset(events);reset=true;blocked_until=now+100;}
        }
        if(!length&&int32_t(now-blocked_until)>=0&&(reset||uxQueueMessagesWaiting(events)||now-last>=100)) {
            uint8_t p[201]{};p[0]=connected?1:0;unsigned n=1;
            if(reset){p[n+5]=0x84;n+=10;reset=false;}
            Input input;
            while(n+10<=sizeof(p)&&xQueueReceive(events,&input,0)==pdTRUE)if(input.generation==epoch){
                const auto &e=input.event;
                uint32_t at=uint32_t(e.timestamp/1000);for(unsigned j=0;j<4;++j)p[n+j]=uint8_t(at>>(8*j));
                p[n+4]=uint8_t((e.source<<4)|e.channel);
                p[n+5]=uint8_t(e.type==music::Type::Reset?4:unsigned(e.type))|(e.batch_end?128:0);
                p[n+6]=e.note;p[n+7]=e.velocity;n+=10;
            }
            length=control::encode(frame,0x56,p,n,++seq);sent=now-100;
        }
        // Stop-and-wait: an unacknowledged Note Off is retried, never overwritten.
        if(length&&now-sent>=100)if(esp_link_send(frame,length)==int(length))sent=now;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
}
void midi_app_start() {
    if(!events) {
        events=xQueueCreate(MIDI_EVENT_QUEUE_SIZE,sizeof(Input));acks=xQueueCreate(8,sizeof(Ack));
        configASSERT(events&&acks);
        configASSERT(xTaskCreatePinnedToCore(task,"midi-bridge",4096,nullptr,20,nullptr,1)==pdPASS);
    }
    enabled=true;
    usb_midi_start(incoming,connection);
}
void midi_app_stop() {
    enabled=false;
    usb_midi_stop();
}
bool midi_app_stopped() {return usb_midi_stopped()&&quiet.load();}
bool midi_app_connected(){return connected.load();}
void midi_app_ack(uint8_t seq,uint8_t result){if(acks){Ack a{seq,result};xQueueSend(acks,&a,0);}}
