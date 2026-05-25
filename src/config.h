#pragma once
#include "triton.h"
#include <cstdint>
#include <string>

namespace sc2 {

struct Sc2Config {
    uint16_t port       = 26761;
    bool     expose     = false;

    AxisMap  gyro       = DEFAULT_GYRO;
    AxisMap  accel      = DEFAULT_ACCEL;

    float    gyroBias[3] = {0.f, 0.f, 0.f};

    // Load from ~/.config/sc2gyrodsu/config.ini; returns defaults if missing.
    static Sc2Config load();

    // Write to configPath(). Returns false on I/O error.
    bool save() const;

    static std::string configDir();
    static std::string configPath();
};

} // namespace sc2
