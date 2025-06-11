#ifndef CONSOLE_H
#define CONSOLE_H

#include <string>
#include <functional>

namespace console {
    void init();
    void processCommand(const char* command, std::function<void(const char*)> sendResponse);
}

#endif