#include "live_controller.h"
#include <cstring>
namespace live {
void Controller::connection(bool present) {
    connected = present;
    engine.reset();
}
void Controller::event(music::Event event) {
    if (event.type == music::Type::Reset) { engine.reset(); return; }
    if (connected && enabled) engine.process(event);
}
void Controller::overflow() { engine.reset(); enabled = false; }
uint8_t Controller::action(uint8_t sequence, uint8_t command, uint8_t motor, uint32_t value, uint32_t now,
                           bool *reset_input) {
    if (reset_input) *reset_input = false;
    for (const auto &old : recent)
        if (old.used && old.sequence == sequence && old.command == command && old.motor == motor &&
            old.value == value && now - old.at < 2000) return old.result;
    uint8_t result = 1;
    if (motor >= 6) result = 2;
    else if (command == 0 || command == 1) {
        enabled = command == 0 ? false : !enabled;
        // Resume requires new Note On: held/queued old notes cannot restart a stopped output.
        engine.reset(); result = 0;
        if (reset_input) *reset_input = true;
    }
    recent[next_action++ % 16] = {sequence,command,motor,result,value,now,true};
    return result;
}
music::NoteSet Controller::output() const { return connected && enabled ? engine.output() : music::NoteSet{}; }
Snapshot Controller::transmitted(const music::NoteSet &set, uint32_t now) {
    // Stable logical voice lanes for the sets actually submitted to UART.
    // These are not feedback from STM32 or guaranteed physical motor assignments.
    bool matched[6]{};
    for (auto &voice : voices) {
        bool keep = false;
        for (unsigned i = 0; i < set.count; ++i) if (voice == set.notes[i]) { matched[i] = keep = true; break; }
        if (!keep) voice = 255;
    }
    for (unsigned i = 0; i < set.count; ++i) if (!matched[i])
        for (auto &voice : voices) if (voice == 255) { voice = set.notes[i]; break; }
    Snapshot snapshot;
    snapshot.at = now; snapshot.connected = connected; snapshot.enabled = enabled;
    memcpy(snapshot.notes, voices, sizeof(voices));
    return snapshot;
}
uint32_t frequency_mhz(uint8_t note) {
    if (note > 127) return 0;
    static const uint32_t octave[] = {261626,277183,293665,311127,329628,349228,369994,391995,415305,440000,466164,493883};
    uint32_t f = octave[note % 12];
    const int shift = int(note / 12) - 5;
    f = shift >= 0 ? f << shift : f >> -shift;
    while (f < 20000) f *= 2;
    while (f > 4000000) f /= 2;
    return f;
}
}
