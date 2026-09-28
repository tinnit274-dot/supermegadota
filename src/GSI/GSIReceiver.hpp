#pragma once
#include "GSITypes.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

namespace httplib { class Server; }

namespace dota_bot {

class GSIReceiver {
public:
    using Callback = std::function<void(const GSIState&)>;
    using RawCallback = std::function<void(const std::string&)>;
    
    bool Start(int port = 3000);
    void Stop();
    bool IsRunning() const;
    GSIState GetLatestState() const;
    void SetCallback(Callback cb);
    void SetRawCallback(RawCallback cb);
    
private:
    void RunServer(int port);
    mutable std::mutex stateMutex;
    GSIState latestState;
    std::thread serverThread;
    std::atomic<bool> running{false};
    httplib::Server* server = nullptr;
    mutable std::mutex callbackMutex;
    Callback callback;
    RawCallback rawCallback;
};

} // namespace dota_bot
