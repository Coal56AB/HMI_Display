#include "controller_link.h"
#include "note_set_wire.h"
#ifdef MUSIC_BOX_CONTROLLER
#include "midi_app.h"
#endif
#include <cstring>
namespace control {
unsigned encode(uint8_t *out, uint8_t command, const uint8_t *payload, unsigned length, uint8_t sequence) {
    if (length > 240) return 0;
    out[0] = 0xa5; out[1] = 0x5a; out[2] = uint8_t(length); out[3] = sequence; out[4] = command;
    if (length) memcpy(out + 5, payload, length);
    const auto crc = ns_crc(out + 2, length + 3);
    out[length + 5] = uint8_t(crc); out[length + 6] = uint8_t(crc >> 8);
    return length + 7;
}
static uint32_t get32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static bool state_valid(const uint8_t *p, unsigned length) {
    if (!((p[0]==1&&length==50)||(p[0]==2&&length==86)||(p[0]==3&&length==94)) || p[1] > 127 || p[2] > 1 || p[3] > 1 || p[4] > 7 || p[5] > 63) return false;
    for (unsigned m = 0; m < 6; ++m) {
        if (p[14 + 6*m] > 31 || (p[15 + 6*m] > 127 && p[15 + 6*m] != 255) ||
            get32(p + 16 + 6*m) > 4000000) return false;
    }
    if(p[0]>=2)for(unsigned m=0;m<6;++m) {
        if(p[50+6*m]>7 || (p[51+6*m]>127&&p[51+6*m]!=255) || get32(p+52+6*m)>1200000)return false;
    }
    return true;
}
bool Link::select(uint32_t now) {
    const Source next = inputs[0].alive && now - inputs[0].last_state <= lease_ms ? Uart :
                        inputs[1].alive && now - inputs[1].last_state <= lease_ms ? Usb :
                        inputs[2].alive && now - inputs[2].last_state <= lease_ms ? Midi : None;
    if (selected == next) return false;
    selected = next; changed(next);
    if (next != None) {
        const auto &input = inputs[next - 1];
        uint8_t bytes[32], payload[7]{};
        for (unsigned i = 0; i < 4; ++i) payload[i] = uint8_t(input.clock >> (i * 8));
        ui(bytes, encode(bytes, 0x43, payload, 4));
        ui(bytes, encode(bytes, 0x44, input.range, 2));
        ui(input.state, unsigned(input.state[2])+7);
        if (input.title_length) ui(input.title, input.title_length);
        // Reconstruct held notes when taking over; inactive transport history is not replayed.
        for (unsigned m = 0; m < 6; ++m) {
            const uint8_t *motor = input.state + 5 + 14 + m*6;
            if ((motor[0] & 2) && motor[1] < 128) {
                payload[4] = uint8_t(m); payload[5] = motor[1]; payload[6] = 100;
                ui(bytes, encode(bytes, 0x42, payload, 7));
            }
        }
    }
    return true;
}
void Link::tick(uint32_t now) { select(now); }
void Link::disconnect(Source source,uint32_t now) {
    if(source<Uart||source>Midi)return;
    inputs[source-1]=Input{};
    select(now);
}
void Link::frame(Source source, const uint8_t *bytes, unsigned count, uint32_t now) {
    auto &input = inputs[source - 1];
    const unsigned command = bytes[4], length = bytes[2];
    // Live events are an input to STM32, independent of the selected UI source.
    // Keep the original sequence/CRC so the PC can retry until STM32 ACKs it.
    if(source==Usb && command==0x56 && length>=1 && length<=201 && (length-1)%10==0) {
        send(Uart,bytes,count);return;
    }
    if(source==Uart && command==0x57 && length==1) {
        send(Usb,bytes,count);
#ifdef MUSIC_BOX_CONTROLLER
        midi_app_ack(bytes[3],bytes[5]);
#endif
        return;
    }
    if (command == 0x40 && (length == 50 || length == 86 || length == 94) && state_valid(bytes + 5,length)) {
        memcpy(input.state, bytes, count); input.last_state = now; input.alive = true;
        if (!select(now) && selected == source) ui(bytes, count);
    } else if (command == 0x45 && length >= 5 && length <= 165) {
        if(selected==source)ui(bytes,count);
    } else if (command == 0x44 && length == 2) {
        memcpy(input.range, bytes + 5, 2);
        if (selected == source) ui(bytes, count);
    } else if (command == 0x41 && length <= 48) {
        memcpy(input.title, bytes, count); input.title_length = count;
        if (selected == source) ui(bytes, count);
    } else if (command == 0x43 && length == 4) {
        input.clock = get32(bytes + 5);
        if (selected == source) ui(bytes, count);
    } else if ((command == 0x42 && length == 7 && bytes[9] < 6 && bytes[10] < 128) ||
               (command == 0x51 && length == 1)) {
        if (selected == source) ui(bytes, count);
    }
}
void Link::feed(Source source, uint8_t byte, uint32_t now) {
    if (source != Uart && source != Usb && source != Midi) return;
    auto &input = inputs[source - 1];
    if (input.used && now - input.last_byte > 100) input.used = 0;
    input.last_byte = now;
    input.bytes[input.used++] = byte;
    while (input.used >= 2) {
        if (input.bytes[0] == 0xa5 && input.bytes[1] == 0x5a) {
            if (input.used < 3) break;
            const unsigned length = input.bytes[2], total = length + 7;
            if (length <= 240) {
                if (input.used < total) break;
                if (ns_crc(input.bytes + 2, length + 3) == uint16_t(input.bytes[total-2] | uint16_t(input.bytes[total-1]) << 8)) {
                    frame(source, input.bytes, total, now);
                    input.used -= total; memmove(input.bytes, input.bytes + total, input.used); continue;
                }
            }
        }
        --input.used; memmove(input.bytes, input.bytes + 1, input.used);
    }
}
void Link::action(const uint8_t *bytes, unsigned count, uint32_t now) {
    if (select(now)) return; // Never migrate an old GUI command to a new controller.
    if (selected != None && count == 13 && bytes[0] == 0xa5 && bytes[1] == 0x5a &&
        bytes[2] == 6 && bytes[4] == 0x50 &&
        ns_crc(bytes+2,9) == uint16_t(bytes[11] | uint16_t(bytes[12]) << 8)) send(selected, bytes, count);
}
}
