#include "test_tab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFont>
#include <QFrame>

namespace sc2 {

TestTab::TestTab(quint16 dsuPort, QWidget* parent)
    : QWidget(parent)
{
    client_ = new DsuClient(this);

    connect(client_, &DsuClient::serverFound, this, &TestTab::onServerFound);
    connect(client_, &DsuClient::serverLost,  this, &TestTab::onServerLost);
    connect(client_, &DsuClient::sampleReceived, this, &TestTab::onSample);

    setupUi();
    client_->start(dsuPort);
}

void TestTab::setPort(quint16 port) {
    client_->stop();
    client_->start(port);
    view_->resetFilter();
}

// ── UI construction ───────────────────────────────────────────────────────────

void TestTab::setupUi() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // ── Left: 3D view ─────────────────────────────────────────────────────────
    auto* viewBox = new QGroupBox("Controller orientation");
    auto* viewLay = new QVBoxLayout(viewBox);
    viewLay->setContentsMargins(4, 8, 4, 4);

    view_ = new ControllerView;
    viewLay->addWidget(view_, 1);

    // ADI legend
    auto* legend = new QLabel(
        "<span style='color:#4a80dd'>■</span> Sky  "
        "<span style='color:#8a4820'>■</span> Ground  "
        "<span style='color:#ff8800'>━</span> Yaw  "
        "<span style='color:#ffee00'>✛</span> Aircraft");
    legend->setTextFormat(Qt::RichText);
    legend->setAlignment(Qt::AlignCenter);
    legend->setStyleSheet("font-size: 9pt;");
    viewLay->addWidget(legend);

    root->addWidget(viewBox, 3);

    // ── Right: controls + readouts ────────────────────────────────────────────
    auto* rightCol = new QVBoxLayout;
    rightCol->setSpacing(8);

    // Status
    auto* statusFrame = new QFrame;
    statusFrame->setFrameShape(QFrame::StyledPanel);
    auto* stLay = new QHBoxLayout(statusFrame);
    stLay->setContentsMargins(6, 4, 6, 4);
    statusLabel_ = new QLabel("Status: connecting…");
    stLay->addWidget(statusLabel_);
    rightCol->addWidget(statusFrame);

    // Slot selector
    auto* slotRow = new QHBoxLayout;
    slotRow->addWidget(new QLabel("Slot:"));
    slotCombo_ = new QComboBox;
    for (int i = 0; i < 4; i++)
        slotCombo_->addItem(QString("Controller %1").arg(i));
    slotCombo_->setCurrentIndex(0);
    connect(slotCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TestTab::onSlotChanged);
    slotRow->addWidget(slotCombo_, 1);
    rightCol->addLayout(slotRow);

    // Reset yaw
    resetYawBtn_ = new QPushButton("Reset Yaw");
    resetYawBtn_->setToolTip("Resets the integrated yaw angle to 0°.\n"
                              "Yaw drifts over time (no magnetometer).");
    connect(resetYawBtn_, &QPushButton::clicked, this, &TestTab::onResetYaw);
    rightCol->addWidget(resetYawBtn_);

    // ── Sensor readouts ───────────────────────────────────────────────────────
    auto* readBox = new QGroupBox("Live sensor data (DSU-space)");
    auto* grid    = new QGridLayout(readBox);
    grid->setSpacing(4);

    QFont mono;
    mono.setFamily("monospace");

    struct { const char* name; const char* unit; int row; } fields[6] = {
        {"Accel X", "g",   0}, {"Accel Y", "g",   1}, {"Accel Z", "g",   2},
        {"Gyro  X", "°/s", 3}, {"Gyro  Y", "°/s", 4}, {"Gyro  Z", "°/s", 5},
    };

    // Separator line between accel and gyro
    for (int i = 0; i < 6; i++) {
        if (i == 3) {
            auto* sep = new QFrame;
            sep->setFrameShape(QFrame::HLine);
            grid->addWidget(sep, 3, 0, 1, 3);
        }
        int row = i + (i >= 3 ? 1 : 0);   // shift gyro rows past separator

        auto* nameLbl = new QLabel(fields[i].name);
        nameLbl->setStyleSheet(i < 3
            ? "color: #66aaff;"   // accel = blue
            : "color: #ffaa44;"); // gyro  = orange
        grid->addWidget(nameLbl, row, 0);

        valLabel_[i] = new QLabel("  0.000");
        valLabel_[i]->setFont(mono);
        valLabel_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valLabel_[i]->setMinimumWidth(70);
        grid->addWidget(valLabel_[i], row, 1);

        grid->addWidget(new QLabel(fields[i].unit), row, 2);
    }

    rightCol->addWidget(readBox);

    // ── Verification guide ────────────────────────────────────────────────────
    auto* guideBox = new QGroupBox("Axis verification guide");
    auto* guideLay = new QVBoxLayout(guideBox);
    auto* guide = new QLabel(
        "<small>"
        "<b>To verify axis mapping:</b><br>"
        "① Lay flat face-up → ADI level (50/50 sky/ground)<br>"
        "② Tilt right → horizon tilts, sky shifts right<br>"
        "③ Tilt nose up → horizon drops, more sky shows<br>"
        "④ Spin clockwise → orange yaw arc grows right<br><br>"
        "<b>Accel Z ≈ +1.0 g when face-up</b><br>"
        "If wrong, re-run the <i>Axis Wizard</i> or fix<br>"
        "manually in the <i>Axis Mapping</i> tab."
        "</small>");
    guide->setWordWrap(true);
    guide->setTextFormat(Qt::RichText);
    guideLay->addWidget(guide);
    rightCol->addWidget(guideBox);

    rightCol->addStretch();
    root->addLayout(rightCol, 2);

    // Initial state
    onServerLost();
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void TestTab::onServerFound() {
    statusLabel_->setText(
        "Status: <b style='color:green'>Connected</b> — receiving data");
    statusLabel_->setTextFormat(Qt::RichText);
}

void TestTab::onServerLost() {
    statusLabel_->setText(
        "Status: <b style='color:orange'>Waiting</b> — is the service running?");
    statusLabel_->setTextFormat(Qt::RichText);
    view_->resetFilter();
    for (auto* lbl : valLabel_) lbl->setText("  —");
}

void TestTab::onSlotChanged(int index) {
    activeSlot_ = index;
    view_->resetFilter();
}

void TestTab::onResetYaw() {
    view_->resetFilter();
}

void TestTab::onSample(int slot,
                        float ax, float ay, float az,
                        float gx, float gy, float gz,
                        quint64 ts) {
    if (slot != activeSlot_) return;

    // Compute elapsed time from hardware timestamps
    quint64 elapsed_us = (lastTs_ > 0 && ts > lastTs_)
        ? (ts - lastTs_)
        : 8000;   // default 8 ms
    lastTs_ = ts;

    float accel[3] = {ax, ay, az};
    float gyro[3]  = {gx, gy, gz};
    view_->pushSample(accel, gyro, elapsed_us);
    updateReadouts(ax, ay, az, gx, gy, gz);
}

void TestTab::updateReadouts(float ax, float ay, float az,
                              float gx, float gy, float gz) {
    const float vals[6] = {ax, ay, az, gx, gy, gz};
    for (int i = 0; i < 6; i++)
        valLabel_[i]->setText(QString("  %1").arg(
            static_cast<double>(vals[i]), 7, 'f', 3));
}

} // namespace sc2
