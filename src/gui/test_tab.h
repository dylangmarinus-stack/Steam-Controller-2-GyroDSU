#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include "dsu_client.h"
#include "controller_view.h"

namespace sc2 {

// Test tab: live DSU-stream visualization for axis mapping verification.
//
// Subscribes to the running sc2gyrodsu daemon as a Cemuhook client and
// shows real-time accel/gyro values and a 3D wireframe of the controller.
//
// To verify axis mapping:
//  1. Lay the controller flat face-up → wireframe should be face-up.
//  2. Tilt right (right side down) → wireframe rolls right.
//  3. Tilt nose up → wireframe pitches up.
//  4. Rotate clockwise (viewed from top) → wireframe yaws clockwise.
// If anything looks wrong, adjust the Axis Mapping tab and save.
class TestTab : public QWidget {
    Q_OBJECT
public:
    explicit TestTab(quint16 dsuPort = 26761, QWidget* parent = nullptr);

    // Call when the DSU port changes (e.g. user saves new settings).
    void setPort(quint16 port);

private slots:
    void onServerFound();
    void onServerLost();
    void onSample(int slot,
                  float ax, float ay, float az,
                  float gx, float gy, float gz,
                  quint64 ts);
    void onSlotChanged(int index);
    void onResetYaw();

private:
    void setupUi();
    void updateReadouts(float ax, float ay, float az,
                        float gx, float gy, float gz);

    DsuClient*     client_      = nullptr;
    ControllerView* view_       = nullptr;

    QLabel*     statusLabel_    = nullptr;
    QComboBox*  slotCombo_      = nullptr;
    QPushButton* resetYawBtn_   = nullptr;

    // Readout labels: [0]=accel X, [1]=accel Y, [2]=accel Z,
    //                 [3]=gyro  X, [4]=gyro  Y, [5]=gyro  Z
    QLabel* valLabel_[6]        = {};

    int      activeSlot_        = 0;
    quint64  lastTs_            = 0;
};

} // namespace sc2
