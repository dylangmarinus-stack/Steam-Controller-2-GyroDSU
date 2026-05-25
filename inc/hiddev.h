#pragma once
#include "triton.h"
#include <hidapi/hidapi.h>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <cstring>

namespace sc2 {

class HidSlot {
public:
    HidSlot(hid_device* dev, int dsuSlot, uint16_t pid, std::string serial);
    ~HidSlot();
    HidSlot(const HidSlot&) = delete;
    HidSlot& operator=(const HidSlot&) = delete;

    bool readOne(ControllerState& out);

    // Apply axis maps and gyro bias from config.
    // Call once after construction before the reader thread starts.
    void configure(const AxisMap& gyro, const AxisMap& accel, const float bias[3]);

    int         dsuSlot;
    uint16_t    pid;
    std::string serial;

private:
    void sendFeatureReport(uint8_t setting, uint16_t value);
    void refreshLizard();
    void refreshImu();

    hid_device*  dev_;
    std::chrono::steady_clock::time_point lastLizard_;
    std::chrono::steady_clock::time_point lastImu_;

    AxisMap gyroMap_   = DEFAULT_GYRO;
    AxisMap accelMap_  = DEFAULT_ACCEL;
    float   gyroBias_[3] = {0.f, 0.f, 0.f};
};

class HidManager {
public:
    HidManager();
    ~HidManager();

    // Enumerate connected SC2 devices, open new ones, and push into active.
    // Applies gyro/accel axis maps and bias to each new slot.
    void scan(std::vector<std::unique_ptr<HidSlot>>& active,
              bool slotUsed[4],
              const AxisMap& gyro,
              const AxisMap& accel,
              const float bias[3]);

    // Print all Valve HID interfaces to stderr for debugging.
    void probe();

private:
    static bool isActive(const std::vector<std::unique_ptr<HidSlot>>& active,
                         const std::string& id);
};

} // namespace sc2
