#pragma once
#include <QWidget>

namespace sc2 {

// Artificial horizon / Attitude Direction Indicator (ADI) widget.
//
// Displays controller orientation derived from live DSU-space accel/gyro via
// a complementary filter.  Renders like an aircraft ADI:
//
//   • Sky (blue) / ground (brown) ball rolls with roll, shifts with pitch.
//   • Fixed aircraft symbol at centre.
//   • Bank-angle scale on the bezel; rotating triangle on the ball.
//   • Orange yaw arc around the outer ring (fills from 12 o'clock).
//   • Numeric roll / pitch / yaw readout below the instrument.
//
// DSU-space conventions (see triton.h):
//   Accel X+ = AccelRightToLeft  |  Gyro X+ = pitch rate
//   Accel Y+ = AccelFrontToBack  |  Gyro Y+ = -(yaw rate)
//   Accel Z+ = AccelTopToBottom  |  Gyro Z+ = roll rate
class ControllerView : public QWidget {
    Q_OBJECT
public:
    explicit ControllerView(QWidget* parent = nullptr);

    void pushSample(float accel[3], float gyro[3], quint64 elapsed_us);
    void resetFilter();

    QSize sizeHint()        const override { return {300, 310}; }
    QSize minimumSizeHint() const override { return {160, 165}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    // Complementary filter state
    float roll_       = 0.f;
    float pitch_      = 0.f;
    float yaw_        = 0.f;
    bool  filterInit_ = false;
    float lastAccel_[3] = {};
    float lastGyro_[3]  = {};
};

} // namespace sc2
