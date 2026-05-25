#pragma once
#include "triton.h"
#include <cstdint>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <netinet/in.h>

namespace sc2 {

class DsuServer {
public:
    DsuServer(uint16_t port, bool expose);
    ~DsuServer();

    DsuServer(const DsuServer&) = delete;
    DsuServer& operator=(const DsuServer&) = delete;

    void pushState(const ControllerState& state);
    void setDisconnected(int slot);
    bool hasSubscribers() const;
    void start();
    void stop();

private:
    void serverLoop();
    void handleMessage(const uint8_t* buf, int len, struct sockaddr_in& src);
    void sendVersion(struct sockaddr_in& src);
    void sendSlotInfo(struct sockaddr_in& src, uint8_t slot);
    void broadcastData(const ControllerState& state);
    void cleanupSubscribers();

    int      sock_  = -1;
    uint16_t port_;
    bool     expose_;
    uint32_t serverId_;

    struct Subscriber {
        struct sockaddr_in addr;
        std::chrono::steady_clock::time_point lastRequest;
        uint32_t packetCounters[4] = {};
    };

    mutable std::mutex subMutex_;
    std::unordered_map<uint32_t, Subscriber> subscribers_;

    std::mutex stateMutex_;
    bool slotConnected_[4] = {};

    std::atomic<bool>     running_{false};
    std::thread           thread_;

    // Accessed from multiple threads — use atomics to avoid data races.
    std::atomic<uint32_t> samplesInWindow_{0};
    std::atomic<uint32_t> packetsInWindow_{0};
    std::atomic<uint32_t> requestsInWindow_{0};
    std::chrono::steady_clock::time_point windowStart_;
};

} // namespace sc2
