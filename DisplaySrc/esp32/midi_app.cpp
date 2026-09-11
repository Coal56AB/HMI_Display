#include "hmi_board.h"
#include "midi_board_config.h"
#include "live_controller.h"
#include "midi_app.h"
#include "usb_midi_host.h"
#include "note_set_wire.h"
#include "esp_link.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>
#include <cstring>

namespace {
struct Input { music::Event event{}; uint32_t generation; };
struct Action { uint8_t sequence, command, motor; uint32_t value; };
struct Reply { uint8_t sequence, result; };
QueueHandle_t events, actions, replies, history, latest;
live::Controller controller;
std::atomic<bool> usb_connected{false}, input_overflow{false}, history_overflow{false};
std::atomic<uint32_t> connection_generation{0};
struct Counters { uint32_t input_overflows, history_overflows, action_overflows, max_process_us; };
Counters counters{};
uint32_t last_ui_at;
bool have_ui_snapshot;
void incoming(const music::Event &event) {
    const Input input{event, connection_generation.load()};
    if (xQueueSend(events, &input, 0) != pdTRUE) input_overflow.store(true);
}
void connection(bool present) {
    usb_connected.store(present);
    connection_generation.fetch_add(1);
}
void publish(const music::NoteSet &sent, uint32_t now) {
    const live::Snapshot snapshot = controller.transmitted(sent, now);
    xQueueOverwrite(latest, &snapshot);
    if (xQueueSend(history, &snapshot, 0) != pdTRUE) {
        // UI may be slow, but it must never backpressure Note Off or STOP.
        history_overflow.store(true); counters.history_overflows++;
    }
}
void musical_task(void *) {
    uint8_t frame[NS_SIZE]{}, sequence = 0;
    unsigned offset = NS_SIZE;
    music::NoteSet current, in_flight;
    uint32_t generation = 0;
    int64_t last_sent = 0;
    bool pending = true;
    bool batch_open = false;
    for (;;) {
        const uint32_t now = uint32_t(esp_timer_get_time() / 1000);
        const uint32_t new_generation = connection_generation.load();
        if (generation != new_generation) {
            generation = new_generation; controller.connection(usb_connected.load());
            batch_open = false;
            pending = true;
        }
        if (input_overflow.exchange(false)) {
            xQueueReset(events); controller.overflow(); counters.input_overflows++; pending = true; batch_open = false;
        }
        Action action;
        while (xQueueReceive(actions, &action, 0) == pdTRUE) {
            bool reset_input = false;
            const Reply reply{action.sequence, controller.action(action.sequence, action.command, action.motor, action.value, now, &reset_input)};
            // A retry must not discard a fresh Note Off and leave the note sounding.
            if (reset_input) {xQueueReset(events); batch_open = false;}
            xQueueSend(replies, &reply, 0); pending = true;
        }
        Input input;
        if (xQueueReceive(events, &input, 0) == pdTRUE) {
            const int64_t begin = esp_timer_get_time();
            if (input.generation == generation) {
                controller.event(input.event);
                batch_open = !input.event.batch_end;
            }
            const uint32_t elapsed = uint32_t(esp_timer_get_time() - begin);
            if (elapsed > counters.max_process_us) counters.max_process_us = elapsed;
        }
        const auto next = batch_open ? current : controller.output();
        if (!(next == current)) { current = next; pending = true; }
        const int64_t us = esp_timer_get_time();
        if (usb_connected.load() && us - last_sent >= NS_HEARTBEAT_MS * 1000) pending = true;
        if (offset == NS_SIZE && pending) {
            in_flight = current; ns_encode(frame, sequence++, current.notes, current.count);
            offset = 0; pending = false;
        }
        if (offset < NS_SIZE) {
            // Single-wire worker owns TX and reply windows; MIDI never waits for UI.
            const int count = esp_link_send(frame + offset, NS_SIZE - offset);
            if (count > 0) offset += unsigned(count);
            if (offset == NS_SIZE) { last_sent = us; publish(in_flight, now); }
        }
        if (!uxQueueMessagesWaiting(events)) vTaskDelay(1);
    }
}
void show(const live::Snapshot &snapshot, bool reset = false) {
    if (have_ui_snapshot && int32_t(snapshot.at - last_ui_at) < 0) return;
    have_ui_snapshot = true; last_ui_at = snapshot.at;
    app_midi_snapshot(snapshot, reset);
}
}
void midi_app_start() {
    events = xQueueCreate(MIDI_EVENT_QUEUE_SIZE, sizeof(Input));
    actions = xQueueCreate(8, sizeof(Action)); replies = xQueueCreate(8, sizeof(Reply));
    history = xQueueCreate(64, sizeof(live::Snapshot)); latest = xQueueCreate(1, sizeof(live::Snapshot));
    configASSERT(events && actions && replies && history && latest);
    configASSERT(xTaskCreatePinnedToCore(musical_task, "music", 16384, nullptr, 20, nullptr, 1) == pdPASS);
    usb_midi_start(incoming, connection);
}
void midi_app_tick(uint32_t now) {
    (void)now;
    Reply reply;
    while (xQueueReceive(replies, &reply, 0) == pdTRUE) app_midi_ack(reply.sequence, reply.result);
    live::Snapshot snapshot;
    if (history_overflow.exchange(false)) {
        xQueueReset(history);
        if (xQueuePeek(latest, &snapshot, 0) == pdTRUE) {
            show(snapshot, true);
        }
    } else {
        // Bounded UI work; pending data will be consumed on subsequent steps.
        for (unsigned n = 0; n < 8 && xQueueReceive(history, &snapshot, 0) == pdTRUE; ++n) show(snapshot);
    }
}
void midi_app_send(const uint8_t *bytes, uint16_t count) {
    if (count != 13 || bytes[0] != 0xa5 || bytes[1] != 0x5a || bytes[2] != 6 || bytes[4] != 0x50 ||
        ns_crc(bytes + 2, 9) != uint16_t(bytes[11] | uint16_t(bytes[12]) << 8)) return;
    Action action{bytes[3], bytes[5], bytes[6], 0};
    for (unsigned i = 0; i < 4; ++i) action.value |= uint32_t(bytes[7 + i]) << (8 * i);
    if (xQueueSend(actions, &action, 0) != pdTRUE) {
        counters.action_overflows++;
        // UI retries the pending command; never execute UI logic in the MIDI task.
    }
}
