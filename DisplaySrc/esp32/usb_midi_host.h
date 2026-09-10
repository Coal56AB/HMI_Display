#pragma once
#include "music.h"
using MidiSink = void (*)(const music::Event &);
using MidiConnection = void (*)(bool);
void usb_midi_start(MidiSink sink, MidiConnection connection);
