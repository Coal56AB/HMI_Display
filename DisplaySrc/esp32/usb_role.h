#pragma once
#include <stdint.h>

// Only probe when idle. A connected peer pins the current USB role.
class UsbRole {
public:
    enum State { Serial, Host, Stopping, Gap };
    enum Action { None, StartHost, StopHost, StartSerial };
    static constexpr uint32_t serial_probe_ms=1500,host_probe_ms=3000;
    static constexpr uint32_t disconnect_ms=500,gap_ms=600;
    void begin(bool serial,uint32_t now) {state=serial?Serial:Host;since=last_seen=now;seen=false;}
    Action tick(uint32_t now,bool connected,bool stopped) {
        if(state==Stopping) {
            if(stopped){state=Gap;since=now;}
            return None;
        }
        if(state==Gap) {
            if(uint32_t(now-since)<gap_ms)return None;
            begin(true,now);return StartSerial;
        }
        // The ROM SOF monitor can retain the previous PHY state for a few ticks.
        if(state==Serial && uint32_t(now-since)<20)return None;
        if(connected){seen=true;last_seen=now;return None;}
        const uint32_t timeout=seen?disconnect_ms:state==Serial?serial_probe_ms:host_probe_ms;
        if(uint32_t(now-(seen?last_seen:since))<timeout)return None;
        if(state==Serial){begin(false,now);return StartHost;}
        state=Stopping;return StopHost;
    }
    State current() const {return state;}
private:
    State state=Serial;
    uint32_t since=0,last_seen=0;
    bool seen=false;
};
