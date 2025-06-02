#ifndef CONSOLE_H
#define CONSOLE_H

#include "driver/uart.h"
#include <string>

namespace console {
    extern const uart_port_t UART_PORT;
    extern const int UART_BAUD_RATE;

    void init();
    std::string processCommand(const char* command);
    void sendCommand(const char* command);
}

#endif