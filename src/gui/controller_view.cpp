#include "controller_view.h"
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

namespace sc2 {

// ── Complementary filter tuning ───────────────────────────────────────────────
static constexpr float ALPHA  = 0.96f;
static constexpr float MAX_DT = 0.05f;

// ── Colours ───────────────────────────────────────────────────────────────────
static const QColor C_SKY   (0x1a, 0x3e, 0x74);   // ADI sky blue
static const QColor C_GROUND(0x50, 0x28, 0x08);   // ADI earth brown
static const QColor C_YAW   (0xff, 0x88, 0x00);   // yaw arc orange
static const QColor C_SYMBOL(0xff, 0xee, 0x00);   // aircraft symbol yellow
static const QColor C_BORDER(0x44, 0x44, 0x66);   // bezel ring
static const QColor C_LABEL (0xaa, 0xaa, 0xcc);   // numeric readout

// ── ControllerView ────────────────────────────────────────────────────────────

ControllerView::ControllerView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(160, 165);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x12, 0x12, 0x20));
    setPalette(pal);
    setAutoFillBackground(true);
}

void ControllerView::resetFilter() {
    roll_ = pitch_ = yaw_ = 0.f;
    filterInit_ = false;
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void ControllerView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // ── Readout font ──────────────────────────────────────────────────────────
    // Text is ~30 chars ("R  -45.1°  P  -12.3°  Y +180.0°").
    // A monospace char is ≈ 0.6 × pixelSize wide, so cap at width / (30 × 0.6)
    // = width / 18 to guarantee the whole string always fits.
    QFont readoutFont;
    readoutFont.setFamily("monospace");
    readoutFont.setPixelSize(std::max(7,
        std::min(std::min(width(), height()) / 14,
                 width() / 18)));
    QFontMetrics fm(readoutFont);
    const int readoutH = fm.height() + 4;

    // ── Working angles (shared by readout and ADI) ────────────────────────────
    const float rollDeg  = roll_  * 180.f / float(M_PI);
    const float pitchDeg = pitch_ * 180.f / float(M_PI);
    float yawDeg = yaw_ * 180.f / float(M_PI);
    while (yawDeg >  180.f) yawDeg -= 360.f;
    while (yawDeg < -180.f) yawDeg += 360.f;

    // ── Numeric readout — always drawn regardless of ADI size ─────────────────
    // Pinned to the bottom of the widget so it is never clipped by the ADI.
    {
        p.setFont(readoutFont);
        p.setPen(C_LABEL);
        const QString txt = QString("R %1°  P %2°  Y %3°")
            .arg(rollDeg,  6, 'f', 1)
            .arg(pitchDeg, 6, 'f', 1)
            .arg(yawDeg,   6, 'f', 1);
        p.drawText(QRect(0, height() - readoutH - 1, width(), readoutH + 1),
                   Qt::AlignHCenter | Qt::AlignVCenter, txt);
    }

    // ── ADI geometry — centred in the space above the readout strip ───────────
    const int margin = 14;   // space for yaw arc + bezel
    const int cx = width() / 2;
    const int cy = (height() - readoutH - 2) / 2;
    const int r  = std::min(width() / 2 - margin,
                            (height() - readoutH - 2) / 2 - margin);
    if (r < 20) return;   // widget too small — readout already painted above

    const float pitchScale = float(r) / (float(M_PI) / 2.f);  // px per radian
    const float pitchPx    = pitch_ * pitchScale;

    // ── 1. Yaw arc (outside the instrument) ──────────────────────────────────
    if (fabsf(yawDeg) > 1.5f) {
        const int yr = r + 8;
        QPen yp(C_YAW, 4.5f, Qt::SolidLine, Qt::RoundCap);
        p.setPen(yp);
        p.setBrush(Qt::NoBrush);
        // Qt: 0° = 3 o'clock, positive = CCW, angles in 1/16 °.
        // 12 o'clock = 90°.  Negate so CW spin (positive yaw_) draws a CW
        // arc that grows to the right, matching physical rotation.
        p.drawArc(cx - yr, cy - yr, yr * 2, yr * 2,
                  90 * 16, int(-yawDeg * 16.f));
    }

    // ── 2. ADI ball — clip, roll, pitch ──────────────────────────────────────
    {
        QPainterPath clip;
        clip.addEllipse(cx - r, cy - r, r * 2, r * 2);
        p.save();
        p.setClipPath(clip);

        p.save();
        p.translate(cx, cy);
        p.rotate(rollDeg);          // roll tilts the ball

        const int fill = r * 3;     // large enough to cover the clipped circle

        // Sky (above horizon line)
        p.fillRect(-fill, -fill, fill * 2,
                   fill + int(pitchPx), C_SKY);
        // Ground (below horizon line)
        p.fillRect(-fill, int(pitchPx),
                   fill * 2, fill, C_GROUND);

        // Horizon line
        p.setPen(QPen(Qt::white, 2.5f));
        p.drawLine(-fill, int(pitchPx), fill, int(pitchPx));

        // ── Pitch ladder ──────────────────────────────────────────────────
        {
            const float deg2px = pitchScale * float(M_PI) / 180.f;
            QFont lf;
            lf.setPixelSize(std::max(8, r / 8));
            p.setFont(lf);
            QFontMetrics lfm(lf);

            for (int deg = -80; deg <= 80; deg += 10) {
                if (deg == 0) continue;
                const float markY = float(deg) * deg2px;
                // Skip if this mark is far off the visible ball area
                if (fabsf(markY) > float(r) * 1.8f) continue;

                const bool major = (abs(deg) % 30 == 0);
                const int  hw    = major ? r / 3 : r / 5;
                p.setPen(QPen(Qt::white, major ? 1.5f : 1.0f));
                p.drawLine(-hw, int(markY), hw, int(markY));

                if (major) {
                    QString label = QString::number(abs(deg));
                    int lh = lfm.ascent();
                    p.setPen(Qt::white);
                    p.drawText( hw + 4,               int(markY) + lh / 2, label);
                    p.drawText(-hw - 4 - lfm.horizontalAdvance(label),
                                                       int(markY) + lh / 2, label);
                }
            }
        }

        // ── Bank indicator triangle (on the ball, rotates with it) ────────
        {
            const int ts = std::max(4, r / 13);
            p.setPen(Qt::NoPen);
            p.setBrush(C_SYMBOL);
            QPolygonF tri;
            tri << QPointF(  0,  -(r - ts * 2 - 2))   // tip (inward)
                << QPointF(-ts,  -(r - 2))              // base left
                << QPointF( ts,  -(r - 2));             // base right
            p.drawPolygon(tri);
        }

        p.restore();  // un-rotate
        p.restore();  // un-clip
    }

    // ── 3. Bank-angle scale (fixed on bezel) ──────────────────────────────────
    {
        p.save();
        p.translate(cx, cy);
        p.setPen(QPen(Qt::white, 1.5f));

        static const int TICKS[] = {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60};
        for (int deg : TICKS) {
            p.save();
            p.rotate(deg);
            const int tlen = (abs(deg) % 30 == 0 || deg == 0)
                             ? r / 8 : r / 14;
            p.drawLine(0, -(r + 1), 0, -(r + 1 + tlen));
            p.restore();
        }

        p.restore();
    }

    // ── 4. Aircraft symbol (fixed, centre) ────────────────────────────────────
    {
        p.save();
        p.translate(cx, cy);

        const int wingR = r / 3;
        const int wingH = std::max(2, r / 18);
        QPen wingPen(C_SYMBOL, 3.0f, Qt::SolidLine, Qt::RoundCap);
        p.setPen(wingPen);

        // Left wing
        p.drawLine(-wingR, 0, -wingR / 4, 0);
        p.drawLine(-wingR, -wingH, -wingR, wingH);
        // Right wing
        p.drawLine(wingR / 4, 0, wingR, 0);
        p.drawLine( wingR, -wingH,  wingR, wingH);
        // Centre dot
        p.setPen(Qt::NoPen);
        p.setBrush(C_SYMBOL);
        const int dr = std::max(3, r / 16);
        p.drawEllipse(-dr, -dr, dr * 2, dr * 2);

        p.restore();
    }

    // ── 5. Bezel ring ─────────────────────────────────────────────────────────
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(C_BORDER, 3.0f));
    p.drawEllipse(cx - r, cy - r, r * 2, r * 2);
}

// ── Complementary filter ──────────────────────────────────────────────────────

void ControllerView::pushSample(float accel[3], float gyro[3], quint64 elapsed_us) {
    std::copy(accel, accel + 3, lastAccel_);
    std::copy(gyro,  gyro  + 3, lastGyro_);

    float dt = float(elapsed_us) / 1e6f;
    if (dt <= 0.f || dt > MAX_DT) dt = 0.008f;

    float norm = sqrtf(accel[0]*accel[0] + accel[1]*accel[1] + accel[2]*accel[2]);
    if (norm < 0.3f || norm > 3.0f) {
        // High motion — integrate gyro only
        roll_  += gyro[2] * float(M_PI) / 180.f * dt;
        pitch_ += gyro[0] * float(M_PI) / 180.f * dt;
        yaw_   -= gyro[1] * float(M_PI) / 180.f * dt;
        update();
        return;
    }

    float ax = accel[0] / norm;
    float ay = accel[1] / norm;
    float az = accel[2] / norm;

    // Accel-derived roll/pitch (DSU convention: accel Z+ = gravity when flat)
    float roll_acc  = atan2f( ax,  az);   // right-side-down = positive roll
    float pitch_acc = atan2f(-ay,  az);   // nose-up = positive pitch

    // DSU gyro: [0]=pitch rate, [1]=-(yaw rate), [2]=roll rate
    float pitch_rate =  gyro[0] * float(M_PI) / 180.f;
    float roll_rate  =  gyro[2] * float(M_PI) / 180.f;
    float yaw_rate   = -gyro[1] * float(M_PI) / 180.f;

    if (!filterInit_) {
        roll_  = roll_acc;
        pitch_ = pitch_acc;
        yaw_   = 0.f;
        filterInit_ = true;
    } else {
        roll_  = ALPHA * (roll_  + roll_rate  * dt) + (1.f - ALPHA) * roll_acc;
        pitch_ = ALPHA * (pitch_ + pitch_rate * dt) + (1.f - ALPHA) * pitch_acc;
        yaw_  += yaw_rate * dt;
    }

    update();
}

} // namespace sc2
