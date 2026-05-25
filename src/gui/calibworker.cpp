#include "calibworker.h"
#include "triton.h"
#include <hidapi/hidapi.h>
#include <cstring>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace sc2 {

CalibWorker::CalibWorker(int durationMs, QObject* parent)
    : QObject(parent), durationMs_(durationMs) {}

void CalibWorker::run() {
    hid_init();

    // Find the first usable SC2 HID interface.
    hid_device* dev = nullptr;
    {
        struct hid_device_info* devs = hid_enumerate(VID_VALVE, 0);
        for (struct hid_device_info* cur = devs; cur && !dev; cur = cur->next) {
            if (isTritonPid(cur->product_id) && cur->usage_page >= 0xFF00)
                dev = hid_open_path(cur->path);
        }
        hid_free_enumeration(devs);
    }

    if (!dev) {
        hid_exit();
        emit done(0.f, 0.f, 0.f, "No Steam Controller 2 found.\n"
                                   "Make sure the controller is connected and "
                                   "the service has fully stopped.");
        return;
    }

    // Enable IMU and disable lizard mode.
    auto sendFeature = [&](uint8_t setting, uint16_t value) {
        uint8_t buf[FEATURE_REPORT_SIZE] = {};
        buf[0] = FEATURE_REPORT_ID;
        buf[1] = ID_SET_SETTINGS;
        buf[2] = 3;
        buf[3] = setting;
        buf[4] = value & 0xFF;
        buf[5] = (value >> 8) & 0xFF;
        hid_send_feature_report(dev, buf, sizeof(buf));
    };
    sendFeature(SETTING_LIZARD_MODE, LIZARD_MODE_OFF);
    sendFeature(SETTING_IMU_MODE,    IMU_MODE_GYRO_ACCEL);
    hid_set_nonblocking(dev, 1);

    // Collect samples for durationMs_.
    double sumG[3] = {0.0, 0.0, 0.0};
    long   count   = 0;

    auto start = std::chrono::steady_clock::now();
    auto end   = start + std::chrono::milliseconds(durationMs_);
    int  lastPct = -1;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= end) break;

        int pct = static_cast<int>(
            100.0 * std::chrono::duration<double>(now - start).count()
                  / (durationMs_ / 1000.0));
        if (pct != lastPct) {
            emit progress(pct);
            lastPct = pct;
        }

        uint8_t buf[64] = {};
        int n = hid_read_timeout(dev, buf, sizeof(buf), 8);
        if (n <= 0) continue;

        uint8_t id = buf[0];
        if (id != REPORT_ID_STATE && id != REPORT_ID_STATE_BLE) continue;

        int plen = n - 1;
        const uint8_t* p = buf + 1;
        if (plen < static_cast<int>(IMU_OFFSET + 16)) continue;

        const uint8_t* imu = p + IMU_OFFSET;
        auto i16le = [&](int o) -> int16_t {
            return (int16_t)((uint16_t)imu[o] | ((uint16_t)imu[o+1] << 8));
        };

        // Raw gyro in °/s (before any axis map).
        sumG[0] += (double)i16le(10) / 16.384;
        sumG[1] += (double)i16le(12) / 16.384;
        sumG[2] += (double)i16le(14) / 16.384;
        ++count;
    }

    emit progress(100);
    hid_close(dev);
    hid_exit();

    if (count < 10) {
        emit done(0.f, 0.f, 0.f,
            QString("Too few samples (%1). "
                    "Is the controller sending IMU data?").arg(count));
        return;
    }

    float bx = static_cast<float>(sumG[0] / count);
    float by = static_cast<float>(sumG[1] / count);
    float bz = static_cast<float>(sumG[2] / count);
    emit done(bx, by, bz, {});
}

} // namespace sc2
