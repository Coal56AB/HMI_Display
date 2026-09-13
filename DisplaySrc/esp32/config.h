#pragma once
// Native USB Serial/JTAG for the PC simulator; set to 0 for the external UART.
#define PCH_USE_USB 1
// Separate from UART0 used by the board console.
#define PCH_UART_PORT UART_NUM_1
#define PCH_UART_TX 1
#define PCH_UART_RX 2
#define PCH_UART_BAUD 115200
