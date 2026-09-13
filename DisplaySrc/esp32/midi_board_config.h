#pragma once
#define MIDI_UART_TX 5
#define MIDI_UART_RX 5
#define MIDI_DEBUG_PIN -1
#define MIDI_DEBUG_ENABLED 0
#if MIDI_DEBUG_ENABLED && (MIDI_DEBUG_PIN < 0 || MIDI_DEBUG_PIN == MIDI_UART_TX || MIDI_DEBUG_PIN == MIDI_UART_RX)
#error "Choose a separate valid GPIO for MIDI debugging"
#endif
#define USB_VBUS_ENABLE_PIN -1
#define USB_VBUS_ENABLE_LEVEL 1
#define MIDI_EVENT_QUEUE_SIZE 64
