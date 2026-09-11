#pragma once
#include "controller_link.h"

// Uses exactly the same CRC and state validation as the running application,
// but never delivers startup packets to the not-yet-initialized GUI.
class StartupLink {
public:
    enum class State { Waiting, Ready, Failed };
    static constexpr uint32_t timeout_ms = 1000;
    static constexpr uint32_t retry_ms = 20;
    explicit StartupLink(uint32_t now): started(now), last_probe(now-retry_ms), probe(ignore_frame, ignore_source, ignore_send) {}
    bool probe_due(uint32_t now) {
        return state(now) == State::Waiting && uint32_t(now-last_probe) >= retry_ms;
    }
    void probe_sent(uint32_t now) { last_probe = now; }
    void feed(uint8_t byte, uint32_t now) {
        if (state(now) == State::Waiting) probe.feed(control::Uart, byte, now);
    }
    State state(uint32_t now) {
        if (result != State::Waiting) return result;
        probe.tick(now);
        if (uint32_t(now - started) >= timeout_ms) result = State::Failed;
        else if (probe.active() == control::Uart) result = State::Ready;
        return result;
    }
private:
    static void ignore_frame(const uint8_t *, unsigned) {}
    static void ignore_source(control::Source) {}
    static void ignore_send(control::Source, const uint8_t *, unsigned) {}
    uint32_t started, last_probe;
    control::Link probe;
    State result = State::Waiting;
};
