#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace sc2 {

constexpr uint16_t VID_VALVE = 0x28DE;

// ── Steam Controller 2 ("Triton") USB product IDs ────────────────────────────
constexpr uint16_t PID_TRITON_WIRED   = 0x1302;  // USB-C wired
constexpr uint16_t PID_TRITON_BLE     = 0x1303;  // Bluetooth LE
constexpr uint16_t PID_PROTEUS_PUCK   = 0x1304;  // Proteus Puck dongle
constexpr uint16_t PID_NEREID_PUCK    = 0x1305;  // Nereid Puck dongle

constexpr std::array<uint16_t, 4> TRITON_PIDS = {
    PID_TRITON_WIRED, PID_TRITON_BLE, PID_PROTEUS_PUCK, PID_NEREID_PUCK
};

// ── HID report constants ──────────────────────────────────────────────────────
constexpr uint8_t REPORT_ID_STATE     = 0x42;
constexpr uint8_t REPORT_ID_STATE_BLE = 0x45;
constexpr uint8_t FEATURE_REPORT_ID   = 0x01;

constexpr uint8_t  ID_SET_SETTINGS     = 0x87;
constexpr uint8_t  SETTING_LIZARD_MODE = 9;
constexpr uint8_t  SETTING_IMU_MODE    = 48;
constexpr uint16_t LIZARD_MODE_OFF     = 0;
constexpr uint16_t IMU_MODE_GYRO_ACCEL = 0x0008 | 0x0010;

constexpr size_t FEATURE_REPORT_SIZE  = 64;
constexpr size_t INPUT_REPORT_SIZE    = 64;

// Byte offset within the HID report payload (after the report ID byte)
// where the IMU data block begins.
constexpr size_t IMU_OFFSET           = 29;

// ── Button bitmask definitions ────────────────────────────────────────────────
namespace button {
    constexpr uint32_t A          = 0x0000'0001;
    constexpr uint32_t B          = 0x0000'0002;
    constexpr uint32_t X          = 0x0000'0004;
    constexpr uint32_t Y          = 0x0000'0008;
    constexpr uint32_t QAM        = 0x0000'0010;
    constexpr uint32_t R3         = 0x0000'0020;
    constexpr uint32_t VIEW       = 0x0000'0040;
    constexpr uint32_t R4         = 0x0000'0080;
    constexpr uint32_t R5         = 0x0000'0100;
    constexpr uint32_t R          = 0x0000'0200;
    constexpr uint32_t DPAD_DOWN  = 0x0000'0400;
    constexpr uint32_t DPAD_RIGHT = 0x0000'0800;
    constexpr uint32_t DPAD_LEFT  = 0x0000'1000;
    constexpr uint32_t DPAD_UP    = 0x0000'2000;
    constexpr uint32_t MENU       = 0x0000'4000;
    constexpr uint32_t L3         = 0x0000'8000;
    constexpr uint32_t STEAM      = 0x0001'0000;
    constexpr uint32_t L4         = 0x0002'0000;
    constexpr uint32_t L5         = 0x0004'0000;
    constexpr uint32_t L          = 0x0008'0000;
}

// ── IMU sample (in physical units, before axis remapping) ─────────────────────
struct ImuSample {
    uint32_t timestamp_us = 0;
    float    accel_g[3]   = {};   // raw[0..2] in g
    float    gyro_dps[3]  = {};   // raw[0..2] in degrees/second
};

// ── Full controller state ─────────────────────────────────────────────────────
struct ControllerState {
    uint32_t buttons       = 0;
    uint16_t trigger_left  = 0;
    uint16_t trigger_right = 0;
    int16_t  left_stick[2] = {};
    int16_t  right_stick[2]= {};
    ImuSample imu;
    int      slot          = 0;
};

// ── Axis remapping ────────────────────────────────────────────────────────────
//
// The SC2 IMU delivers three-axis accel and gyro in its own sensor frame
// (raw[0], raw[1], raw[2]).  AxisMap reorders and optionally inverts these
// to produce the DSU protocol output axes.
//
// src_x/y/z  : which raw sensor index to use for this output axis (0, 1, or 2)
// inv_x/y/z  : whether to negate that raw value
//
// DEFAULT axis mapping (verified against SteamDeckGyroDSU cemuhookadapter.cpp):
//
//   raw[0] = sensor X  (side-to-side;  positive = toward right side of controller)
//   raw[1] = sensor Y  (front-to-back; positive = toward face of controller)
//   raw[2] = sensor Z  (top-to-bottom; positive = toward top/shoulder of controller)
//
// DSU protocol output conventions (what emulators expect):
//
//   Accel X  =  AccelRightToLeft  → negative raw[0]   inv=true,  src=0
//   Accel Y  =  AccelFrontToBack  → negative raw[2]   inv=true,  src=2
//   Accel Z  =  AccelTopToBottom  → positive raw[1]   inv=false, src=1
//
//   Gyro X   =  GyroRightToLeft   → positive raw[0]   inv=false, src=0
//   Gyro Y   =  GyroFrontToBack   → negative raw[2]   inv=true,  src=2
//   Gyro Z   =  GyroTopToBottom   → positive raw[1]   inv=false, src=1
//
// Expected sensor readings in known orientations (flat face-up on a table):
//   accel raw ≈ (  0,  +1,   0 )   → accel DSU ≈ (0, 0, +1)  [gravity = TopToBottom = +1g]
//
// If the physical SC2 sensor is mounted differently, adjust these defaults
// via the GUI Axis Mapping tab and save to ~/.config/sc2gyrodsu/config.ini.
//
struct AxisMap {
    int  src_x = 0; bool inv_x = false;
    int  src_y = 2; bool inv_y = true;
    int  src_z = 1; bool inv_z = false;

    float applyX(const float raw[3]) const { return inv_x ? -raw[src_x] : raw[src_x]; }
    float applyY(const float raw[3]) const { return inv_y ? -raw[src_y] : raw[src_y]; }
    float applyZ(const float raw[3]) const { return inv_z ? -raw[src_z] : raw[src_z]; }
};

constexpr AxisMap DEFAULT_GYRO  = { 0, false, 2, true,  1, false };
constexpr AxisMap DEFAULT_ACCEL = { 0, true,  2, true,  1, false };

bool        isTritonPid(uint16_t pid);
const char* pidLabel(uint16_t pid);

} // namespace sc2
