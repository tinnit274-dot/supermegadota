#include "Logger.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

std::mutex Logger::logMutex;

static std::string Timestamp() {
    auto t = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(t);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()) % 1000;
    std::tm tm;
    localtime_s(&tm, &tt);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();
    return os.str();
}

void Logger::Init() {
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream f("bot.log", std::ios::trunc);
    f << "[" << Timestamp() << "] === Bot started ===\n";
    f.flush();
}

void Logger::Log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream f("bot.log", std::ios::app);
    f << "[" << Timestamp() << "] " << msg << "\n";
    f.flush();
}
