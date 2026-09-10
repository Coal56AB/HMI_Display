#pragma once
#include "music.h"
#include <stdint.h>
namespace live {
struct Snapshot {
    uint32_t at = 0;
    uint8_t connected = 0, enabled = 1;
    uint8_t notes[6] = {255,255,255,255,255,255};
};
// One owner: the MIDI task. No GUI, USB, RTOS or allocation dependencies.
class Controller {
public:
    void connection(bool present);
    void event(music::Event event);
    void overflow();
    uint8_t action(uint8_t sequence, uint8_t command, uint8_t motor, uint32_t value, uint32_t now,
                   bool *reset_input = nullptr);
    music::NoteSet output() const;
    Snapshot transmitted(const music::NoteSet &set, uint32_t now);
private:
    music::Engine engine;
    bool connected = false, enabled = true;
    uint8_t voices[6] = {255,255,255,255,255,255};
    struct Action { uint8_t sequence, command, motor, result; uint32_t value, at; bool used = false; };
    Action recent[16]{};
    unsigned next_action = 0;
};
uint32_t frequency_mhz(uint8_t note);
}
