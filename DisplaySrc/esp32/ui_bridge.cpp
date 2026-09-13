#include "ui_bridge.h"
#include "music_box_control.h"
#include "controller_link.h"
#include <mutex>
#include <cstring>

namespace ui_bridge {
namespace {
enum : uint32_t { Source=1,Connections=2,Midi=4,Progress=8,Error=16,
                 Offset=32,Range=64,Playing=128,Loading=256,Ack=512,Resync=1024,Name=2048 };
struct Song {char title[32]{};uint32_t duration=0;unsigned present=0;};
struct Snapshot {
    uint32_t dirty=0,generation=0,offset=0;
    unsigned source=0,connections=0,midi=0,stage=0,percent=0,playing=0,loading=0;
    uint8_t low=255,high=255,ack[8]{};
    char error[128]{};
    char name[49]="USB MIDI";
    Song songs[10];
} state;
struct Frame {uint32_t generation;unsigned length;uint8_t bytes[247];};
constexpr unsigned depth=128;
Frame frames[depth];
unsigned head=0,used=0;
bool ready=false;
std::mutex mutex;
}
void set_screen_ready(bool value){std::lock_guard<std::mutex> lock(mutex);ready=value;}
unsigned music_box_screen_ready(){std::lock_guard<std::mutex> lock(mutex);return ready;}
void music_box_control_frame(const uint8_t *p,unsigned n) {
    if(!p||n<7||n>247)return;
    std::lock_guard<std::mutex> lock(mutex);
    // The UI has only one outstanding action. Its ACK must survive graph backlog.
    if(n==8&&p[4]==0x51){memcpy(state.ack,p,8);state.dirty|=Ack;return;}
    if(used==depth) {
        // Never block musical transport. Discard a whole stale visual history
        // and rebuild it from subsequent STATE/note frames, not partial packets.
        head=used=0;++state.generation;state.dirty|=Resync;
    }
    Frame &f=frames[(head+used++)%depth];
    f.generation=state.generation;f.length=n;memcpy(f.bytes,p,n);
}
void music_box_control_source(unsigned value) {
    std::lock_guard<std::mutex> lock(mutex);
    state.source=value;++state.generation;state.dirty|=Source|Name;head=used=0;
}
void music_box_midi_name(const char *value) {
    std::lock_guard<std::mutex> lock(mutex);
    if(strncmp(state.name,value,48)){strncpy(state.name,value,48);state.name[48]=0;state.dirty|=Name;}
}
void music_box_control_connections(unsigned value) {
    std::lock_guard<std::mutex> lock(mutex);
    if(state.connections!=value){state.connections=value;state.dirty|=Connections;}
}
void music_box_midi_input(unsigned value) {
    std::lock_guard<std::mutex> lock(mutex);
    if(state.midi!=value){state.midi=value;state.dirty|=Midi;}
}
void music_box_saved_song(unsigned slot,const char *title,unsigned present,uint32_t duration) {
    if(slot>=10)return;
    std::lock_guard<std::mutex> lock(mutex);
    Song &s=state.songs[slot];strncpy(s.title,title,31);s.title[31]=0;s.present=present;s.duration=duration;
    state.dirty|=1u<<(16+slot);
}
void music_box_save_progress(unsigned stage,unsigned percent) {
    std::lock_guard<std::mutex> lock(mutex);
    state.stage=stage;state.percent=percent;state.dirty|=Progress;
}
void music_box_playback_error(const char *reason) {
    std::lock_guard<std::mutex> lock(mutex);
    strncpy(state.error,reason,127);state.error[127]=0;state.dirty|=Error;
}
void music_box_saved_offset(uint32_t value){std::lock_guard<std::mutex> lock(mutex);state.offset=value;state.dirty|=Offset;}
void music_box_saved_range(uint8_t low,uint8_t high){std::lock_guard<std::mutex> lock(mutex);state.low=low;state.high=high;state.dirty|=Range;}
void music_box_saved_playing(unsigned value) {
    std::lock_guard<std::mutex> lock(mutex);state.playing=value;state.dirty|=Playing;
    if(!value){state.loading=0;state.dirty|=Loading;}
}
void music_box_saved_loading(unsigned value){std::lock_guard<std::mutex> lock(mutex);state.loading=value;state.dirty|=Loading;}
void drain() {
    Snapshot copy;
    {std::lock_guard<std::mutex> lock(mutex);copy=state;state.dirty=0;}
    // No lock is held while calling any UI code, including synchronous sends.
    if(copy.dirty&Resync)::music_box_control_source(0);
    if(copy.dirty&(Source|Resync))::music_box_control_source(copy.source);
    if(copy.dirty&(Name|Resync)) {
        uint8_t frame[55];unsigned n=strlen(copy.name);
        control::encode(frame,0x46,(const uint8_t*)copy.name,n);
        ::music_box_control_frame(frame,n+7);
    }
    for(unsigned i=0;i<10;++i)if(copy.dirty&(1u<<(16+i))) {
        const Song &s=copy.songs[i];::music_box_saved_song(i,s.title,s.present,s.duration);
    }
    if(copy.dirty&Connections)::music_box_control_connections(copy.connections);
    if(copy.dirty&Midi)::music_box_midi_input(copy.midi);
    if(copy.dirty&Range)::music_box_saved_range(copy.low,copy.high);
    if(copy.dirty&Offset)::music_box_saved_offset(copy.offset);
    if(copy.dirty&Playing)::music_box_saved_playing(copy.playing);
    if(copy.dirty&Loading)::music_box_saved_loading(copy.loading);
    if(copy.dirty&Progress)::music_box_save_progress(copy.stage,copy.percent);
    if(copy.dirty&Error)::music_box_playback_error(copy.error);
    if(copy.dirty&Ack)::music_box_control_frame(copy.ack,8);
    for(unsigned i=0;i<depth;++i) {
        Frame frame;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(!used||frames[head].generation!=copy.generation)break;
            frame=frames[head];head=(head+1)%depth;--used;
        }
        ::music_box_control_frame(frame.bytes,frame.length);
    }
}
}
