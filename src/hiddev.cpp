#include "hiddev.h"
#include <cstring>
#include <cstdio>
#include <cwchar>       // wcstombs
#include <algorithm>
#include <chrono>

using namespace std::chrono_literals;

namespace sc2 {

// ---------- helpers ----------

// Convert a hidapi wide-character serial number to a narrow std::string.
// hidapi serial_number is wchar_t* (4-byte wchar_t on Linux).
static std::string wideToNarrow(const wchar_t* ws) {
    if (!ws) return {};
    size_t len = wcstombs(nullptr, ws, 0);
    if (len == static_cast<size_t>(-1)) return {};
    std::string s(len, '\0');
    wcstombs(s.data(), ws, len + 1);
    return s;
}

// Return a unique identifier for a HID device: prefer the serial number
// string, fall back to the device path (unique per session).
static std::string deviceId(const struct hid_device_info* info) {
    if (info->serial_number && info->serial_number[0])
        return wideToNarrow(info->serial_number);
    return info->path ? info->path : "";
}

// ---------- HidSlot ----------

HidSlot::HidSlot(hid_device* dev, int dsuSlot, uint16_t pid, std::string serial)
    : dsuSlot(dsuSlot), pid(pid), serial(std::move(serial)), dev_(dev)
{
    auto now = std::chrono::steady_clock::now();
    lastLizard_ = now - 10s;
    lastImu_    = now - 10s;
    sendFeatureReport(SETTING_LIZARD_MODE, LIZARD_MODE_OFF);
    sendFeatureReport(SETTING_IMU_MODE,    IMU_MODE_GYRO_ACCEL);
    hid_set_nonblocking(dev_, 1);
}

HidSlot::~HidSlot() {
    if (dev_) hid_close(dev_);
}

void HidSlot::configure(const AxisMap& gyro, const AxisMap& accel, const float bias[3]) {
    gyroMap_  = gyro;
    accelMap_ = accel;
    memcpy(gyroBias_, bias, sizeof(gyroBias_));
}

void HidSlot::sendFeatureReport(uint8_t setting, uint16_t value) {
    uint8_t buf[FEATURE_REPORT_SIZE] = {};
    buf[0] = FEATURE_REPORT_ID;
    buf[1] = ID_SET_SETTINGS;
    buf[2] = 3;
    buf[3] = setting;
    buf[4] = value & 0xFF;
    buf[5] = (value >> 8) & 0xFF;
    hid_send_feature_report(dev_, buf, sizeof(buf));
}

void HidSlot::refreshLizard() {
    auto now = std::chrono::steady_clock::now();
    if (now - lastLizard_ >= 3s) {
        sendFeatureReport(SETTING_LIZARD_MODE, LIZARD_MODE_OFF);
        lastLizard_ = now;
    }
}

void HidSlot::refreshImu() {
    auto now = std::chrono::steady_clock::now();
    if (now - lastImu_ >= std::chrono::milliseconds(100)) {
        sendFeatureReport(SETTING_IMU_MODE, IMU_MODE_GYRO_ACCEL);
        lastImu_ = now;
    }
}

bool HidSlot::readOne(ControllerState& out) {
    refreshLizard();
    refreshImu();

    uint8_t buf[INPUT_REPORT_SIZE] = {};
    int n = hid_read_timeout(dev_, buf, sizeof(buf), 8);
    if (n <= 0) return false;

    uint8_t id = buf[0];
    if (id != REPORT_ID_STATE && id != REPORT_ID_STATE_BLE) return false;

    const uint8_t* p    = buf + 1;
    int            plen = n - 1;
    if (plen < static_cast<int>(IMU_OFFSET + 16)) return false;

    out.buttons       = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                        ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
    out.trigger_left  = (uint16_t)p[5]  | ((uint16_t)p[6]  << 8);
    out.trigger_right = (uint16_t)p[7]  | ((uint16_t)p[8]  << 8);
    out.left_stick[0] = (int16_t)((uint16_t)p[9]  | ((uint16_t)p[10] << 8));
    out.left_stick[1] = (int16_t)((uint16_t)p[11] | ((uint16_t)p[12] << 8));
    out.right_stick[0]= (int16_t)((uint16_t)p[13] | ((uint16_t)p[14] << 8));
    out.right_stick[1]= (int16_t)((uint16_t)p[15] | ((uint16_t)p[16] << 8));

    const uint8_t* imu = p + IMU_OFFSET;
    uint32_t ts = (uint32_t)imu[0] | ((uint32_t)imu[1] << 8) |
                  ((uint32_t)imu[2] << 16) | ((uint32_t)imu[3] << 24);

    auto i16le = [&](int o) -> int16_t {
        return (int16_t)((uint16_t)imu[o] | ((uint16_t)imu[o+1] << 8));
    };

    float rawA[3] = {
        (float)i16le(4)  / 16384.0f,
        (float)i16le(6)  / 16384.0f,
        (float)i16le(8)  / 16384.0f,
    };
    float rawG[3] = {
        (float)i16le(10) / 16.384f,
        (float)i16le(12) / 16.384f,
        (float)i16le(14) / 16.384f,
    };

    // Subtract calibrated gyro bias (sensor frame, before axis remap).
    rawG[0] -= gyroBias_[0];
    rawG[1] -= gyroBias_[1];
    rawG[2] -= gyroBias_[2];

    out.imu.timestamp_us = ts;
    out.imu.accel_g[0]   = accelMap_.applyX(rawA);
    out.imu.accel_g[1]   = accelMap_.applyY(rawA);
    out.imu.accel_g[2]   = accelMap_.applyZ(rawA);
    out.imu.gyro_dps[0]  = gyroMap_.applyX(rawG);
    out.imu.gyro_dps[1]  = gyroMap_.applyY(rawG);
    out.imu.gyro_dps[2]  = gyroMap_.applyZ(rawG);
    out.slot             = dsuSlot;

    return true;
}

// ---------- HidManager ----------

HidManager::HidManager() { hid_init(); }
HidManager::~HidManager() { hid_exit(); }

bool HidManager::isActive(const std::vector<std::unique_ptr<HidSlot>>& active,
                           const std::string& id) {
    for (auto& s : active)
        if (s->serial == id) return true;
    return false;
}

void HidManager::scan(std::vector<std::unique_ptr<HidSlot>>& active,
                      bool slotUsed[4],
                      const AxisMap& gyro,
                      const AxisMap& accel,
                      const float bias[3]) {
    struct hid_device_info* devs = hid_enumerate(VID_VALVE, 0);
    for (struct hid_device_info* cur = devs; cur; cur = cur->next) {
        if (!isTritonPid(cur->product_id) || cur->usage_page < 0xFF00) continue;

        std::string id = deviceId(cur);
        if (id.empty() || isActive(active, id)) continue;

        int slot = -1;
        for (int i = 0; i < 4; ++i)
            if (!slotUsed[i]) { slot = i; break; }
        if (slot < 0) break;

        hid_device* dev = hid_open_path(cur->path);
        if (!dev) continue;

        fprintf(stderr, "scanner: PID %04X (%s) id=%s -> slot %d\n",
            cur->product_id, pidLabel(cur->product_id), id.c_str(), slot);

        slotUsed[slot] = true;
        auto s = std::make_unique<HidSlot>(dev, slot, cur->product_id, id);
        s->configure(gyro, accel, bias);
        active.push_back(std::move(s));
    }
    hid_free_enumeration(devs);
}

void HidManager::probe() {
    fprintf(stderr, "=== All Valve (VID 0x28DE) HID interfaces ===\n");
    struct hid_device_info* devs = hid_enumerate(VID_VALVE, 0);
    for (struct hid_device_info* cur = devs; cur; cur = cur->next) {
        bool cand = isTritonPid(cur->product_id) && cur->usage_page >= 0xFF00;
        fprintf(stderr, "  PID %04X usage_page=0x%04X path=%s%s\n",
            cur->product_id, cur->usage_page, cur->path,
            cand ? "  <-- candidate" : "");
    }
    hid_free_enumeration(devs);
}

} // namespace sc2
