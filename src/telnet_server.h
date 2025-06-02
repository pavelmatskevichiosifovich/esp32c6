#ifndef TELNET_SERVER_H
#define TELNET_SERVER_H

#include <string>

namespace telnet_server {
    void init();
    std::string processCommand(const char* command);
}

#endif