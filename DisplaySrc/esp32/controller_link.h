#pragma once
#include <stdint.h>
namespace control {
enum Source : uint8_t { None = 0, Uart = 1, Usb = 2, Midi = 3 };
inline bool routes_to_midi(Source source,unsigned action,bool usb_device,bool pc_active) {
    return !usb_device && (source==Midi || (source==Uart &&
        (action==0 || (action==1&&!pc_active))));
}
using FrameSink = void (*)(const uint8_t *, unsigned);
using SourceSink = void (*)(Source);
using SendSink = void (*)(Source, const uint8_t *, unsigned);
unsigned encode(uint8_t *out, uint8_t command, const uint8_t *payload, unsigned length, uint8_t sequence = 0);
class Link {
public:
    Link(FrameSink ui, SourceSink changed, SendSink send): ui(ui), changed(changed), send(send) {}
    void feed(Source source, uint8_t byte, uint32_t now);
    void tick(uint32_t now);
    void disconnect(Source source,uint32_t now);
    void action(const uint8_t *frame, unsigned length, uint32_t now);
    Source active() const { return selected; }
    uint8_t source_flags(Source source, uint32_t now) const {
        if(source < Uart || source > Midi)return 0;
        const auto &input = inputs[source - 1];
        return input.alive && now-input.last_state <= lease_ms ? input.state[6] : 0;
    }
    static constexpr uint32_t lease_ms = 500;
private:
    struct Input {
        uint8_t bytes[247]{}, state[101]{}, title[55]{}, range[2]{255,255};
        unsigned used = 0, title_length = 0;
        uint32_t last_byte = 0, last_state = 0, clock = 0;
        bool alive = false;
    } inputs[3];
    FrameSink ui;
    SourceSink changed;
    SendSink send;
    Source selected = None;
    void frame(Source source, const uint8_t *bytes, unsigned count, uint32_t now);
    bool select(uint32_t now);
};
}
