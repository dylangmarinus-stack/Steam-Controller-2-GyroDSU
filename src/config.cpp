#include "config.h"
#include <fstream>
#include <string>
#include <cstdlib>
#include <sys/stat.h>

namespace sc2 {

// ---------- path helpers ----------

static std::string xdgConfigHome() {
    const char* xch = getenv("XDG_CONFIG_HOME");
    if (xch && xch[0]) return xch;
    const char* home = getenv("HOME");
    return std::string(home ? home : "/tmp") + "/.config";
}

std::string Sc2Config::configDir()  { return xdgConfigHome() + "/sc2gyrodsu"; }
std::string Sc2Config::configPath() { return configDir()      + "/config.ini"; }

// ---------- helpers ----------

static void trim(std::string& s) {
    const auto ws = " \t\r\n";
    size_t a = s.find_first_not_of(ws);
    size_t b = s.find_last_not_of(ws);
    s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static bool parseBool(const std::string& v) {
    return v == "true" || v == "1" || v == "yes";
}

// ---------- load ----------

Sc2Config Sc2Config::load() {
    Sc2Config cfg;
    std::ifstream f(configPath());
    if (!f) return cfg;   // file absent → return defaults

    std::string section, line;
    while (std::getline(f, line)) {
        trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            size_t e = line.find(']');
            section = (e != std::string::npos) ? line.substr(1, e - 1) : "";
            trim(section);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        trim(key); trim(val);
        if (val.empty()) continue;

        try {
            if (section == "server") {
                if      (key == "port")   cfg.port   = static_cast<uint16_t>(std::stoi(val));
                else if (key == "expose") cfg.expose  = parseBool(val);
            } else if (section == "gyro") {
                if      (key == "src_x") cfg.gyro.src_x = std::stoi(val);
                else if (key == "inv_x") cfg.gyro.inv_x = parseBool(val);
                else if (key == "src_y") cfg.gyro.src_y = std::stoi(val);
                else if (key == "inv_y") cfg.gyro.inv_y = parseBool(val);
                else if (key == "src_z") cfg.gyro.src_z = std::stoi(val);
                else if (key == "inv_z") cfg.gyro.inv_z = parseBool(val);
            } else if (section == "accel") {
                if      (key == "src_x") cfg.accel.src_x = std::stoi(val);
                else if (key == "inv_x") cfg.accel.inv_x = parseBool(val);
                else if (key == "src_y") cfg.accel.src_y = std::stoi(val);
                else if (key == "inv_y") cfg.accel.inv_y = parseBool(val);
                else if (key == "src_z") cfg.accel.src_z = std::stoi(val);
                else if (key == "inv_z") cfg.accel.inv_z = parseBool(val);
            } else if (section == "calibration") {
                if      (key == "gyro_bias_x") cfg.gyroBias[0] = std::stof(val);
                else if (key == "gyro_bias_y") cfg.gyroBias[1] = std::stof(val);
                else if (key == "gyro_bias_z") cfg.gyroBias[2] = std::stof(val);
            }
        } catch (...) { /* ignore malformed values */ }
    }
    return cfg;
}

// ---------- save ----------

bool Sc2Config::save() const {
    mkdir(configDir().c_str(), 0755);
    std::ofstream f(configPath());
    if (!f) return false;

    auto b = [](bool v) { return v ? "true" : "false"; };

    f << "[server]\n"
      << "port   = " << port   << "\n"
      << "expose = " << b(expose) << "\n"
      << "\n"
      << "[gyro]\n"
      << "src_x = " << gyro.src_x << "\n"
      << "inv_x = " << b(gyro.inv_x) << "\n"
      << "src_y = " << gyro.src_y << "\n"
      << "inv_y = " << b(gyro.inv_y) << "\n"
      << "src_z = " << gyro.src_z << "\n"
      << "inv_z = " << b(gyro.inv_z) << "\n"
      << "\n"
      << "[accel]\n"
      << "src_x = " << accel.src_x << "\n"
      << "inv_x = " << b(accel.inv_x) << "\n"
      << "src_y = " << accel.src_y << "\n"
      << "inv_y = " << b(accel.inv_y) << "\n"
      << "src_z = " << accel.src_z << "\n"
      << "inv_z = " << b(accel.inv_z) << "\n"
      << "\n"
      << "[calibration]\n"
      << "gyro_bias_x = " << gyroBias[0] << "\n"
      << "gyro_bias_y = " << gyroBias[1] << "\n"
      << "gyro_bias_z = " << gyroBias[2] << "\n";

    return f.good();
}

} // namespace sc2
