#pragma once
#include "music.h"
using MidiSink = void (*)(const music::Event &);
using MidiConnection = void (*)(bool);
void usb_midi_start(MidiSink sink, MidiConnection connection);
void usb_midi_stop();
bool usb_midi_stopped();
