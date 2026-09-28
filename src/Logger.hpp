#pragma once
#include <string>
#include <mutex>

class Logger {
    static std::mutex logMutex;
public:
    static void Init();
    static void Log(const std::string& msg);
};
