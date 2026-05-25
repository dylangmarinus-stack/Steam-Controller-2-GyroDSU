#include "mainwindow.h"
#include "calibworker.h"
#include "axis_wizard.h"
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QFrame>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QThread>
#include <QFont>
#include <QStatusBar>

namespace sc2 {

// ──────────────────────────────────────────────────────────────────────────────
// Construction
// ──────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("sc2gyrodsu Configuration");
    setMinimumWidth(520);

    cfg_ = Sc2Config::load();
    setupUi();
    loadIntoUi(cfg_);

    svcTimer_ = new QTimer(this);
    svcTimer_->setInterval(2000);
    connect(svcTimer_, &QTimer::timeout, this, &MainWindow::onServiceRefreshTimer);
    svcTimer_->start();
    updateServiceStatus();
}

MainWindow::~MainWindow() {
    if (calibThread_ && calibThread_->isRunning()) {
        calibThread_->quit();
        calibThread_->wait(3000);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// UI construction
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setSpacing(8);
    root->setContentsMargins(10, 10, 10, 10);

    // ── service status bar ──
    auto* svcFrame = new QFrame;
    svcFrame->setFrameShape(QFrame::StyledPanel);
    auto* svcLay = new QHBoxLayout(svcFrame);
    svcLay->setContentsMargins(6, 4, 6, 4);

    serviceLabel_ = new QLabel("Service: checking…");
    serviceLabel_->setMinimumWidth(200);
    svcLay->addWidget(serviceLabel_);
    svcLay->addStretch();

    btnStop_ = new QPushButton("Stop");
    btnStop_->setFixedWidth(70);
    connect(btnStop_, &QPushButton::clicked, this, &MainWindow::onServiceStop);
    svcLay->addWidget(btnStop_);

    btnRestart_ = new QPushButton("Restart");
    btnRestart_->setFixedWidth(70);
    connect(btnRestart_, &QPushButton::clicked, this, &MainWindow::onServiceRestart);
    svcLay->addWidget(btnRestart_);

    root->addWidget(svcFrame);

    // ── tabs ──
    tabs_ = new QTabWidget;
    tabs_->addTab(buildServerTab(), "Server Settings");
    tabs_->addTab(buildAxisTab(),   "Axis Mapping");
    tabs_->addTab(buildCalibTab(),  "Calibration");
    testTab_ = new TestTab(cfg_.port);
    tabs_->addTab(testTab_, "Test / Verify");
    root->addWidget(tabs_, 1);

    // ── bottom buttons ──
    auto* btnRow = new QHBoxLayout;
    auto* resetBtn = new QPushButton("Reset to Defaults");
    connect(resetBtn, &QPushButton::clicked, this, &MainWindow::onResetDefaults);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();

    auto* saveBtn = new QPushButton("Save && Apply");
    saveBtn->setDefault(true);
    QFont f = saveBtn->font();
    f.setBold(true);
    saveBtn->setFont(f);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveApply);
    btnRow->addWidget(saveBtn);

    root->addLayout(btnRow);
}

QWidget* MainWindow::buildServerTab() {
    auto* w   = new QWidget;
    auto* lay = new QFormLayout(w);
    lay->setContentsMargins(12, 16, 12, 16);
    lay->setSpacing(10);

    portSpin_ = new QSpinBox;
    portSpin_->setRange(1024, 65535);
    portSpin_->setValue(26761);
    lay->addRow("DSU port:", portSpin_);

    exposeCb_ = new QCheckBox("Expose to network (bind 0.0.0.0 instead of 127.0.0.1)");
    lay->addRow(exposeCb_);

    auto* note = new QLabel(
        "<i>Restart the service after saving for changes to take effect.</i>");
    note->setWordWrap(true);
    lay->addRow(note);

    return w;
}

// Helper: build one row of axis mapping controls (source combo + invert checkbox).
static void addAxisRow(QFormLayout* lay, const QString& label,
                       QComboBox*& srcOut, QCheckBox*& invOut)
{
    auto* row = new QHBoxLayout;
    srcOut = new QComboBox;
    srcOut->addItem("Raw axis 0");
    srcOut->addItem("Raw axis 1");
    srcOut->addItem("Raw axis 2");
    srcOut->setFixedWidth(120);
    row->addWidget(srcOut);

    invOut = new QCheckBox("Invert");
    row->addWidget(invOut);
    row->addStretch();
    lay->addRow(label, row);
}

QWidget* MainWindow::buildAxisTab() {
    auto* w    = new QWidget;
    auto* vlay = new QVBoxLayout(w);
    vlay->setContentsMargins(12, 12, 12, 12);
    vlay->setSpacing(12);

    // Auto-detect wizard button
    auto* wizRow = new QHBoxLayout;
    wizardBtn_ = new QPushButton("⬤  Auto-detect Axis Mapping…");
    wizardBtn_->setToolTip(
        "Launch the wizard to automatically detect the correct\n"
        "axis mapping by guiding you through four short gestures.");
    QFont wf = wizardBtn_->font();
    wf.setBold(true);
    wizardBtn_->setFont(wf);
    connect(wizardBtn_, &QPushButton::clicked, this, &MainWindow::onAxisWizard);
    wizRow->addWidget(wizardBtn_);
    wizRow->addStretch();
    vlay->addLayout(wizRow);

    // Gyro group
    auto* gyroBox = new QGroupBox("Gyroscope axis mapping");
    auto* gLay    = new QFormLayout(gyroBox);
    addAxisRow(gLay, "Output X:", gSrc_[0], gInv_[0]);
    addAxisRow(gLay, "Output Y:", gSrc_[1], gInv_[1]);
    addAxisRow(gLay, "Output Z:", gSrc_[2], gInv_[2]);
    vlay->addWidget(gyroBox);

    // Accel group
    auto* accelBox = new QGroupBox("Accelerometer axis mapping");
    auto* aLay     = new QFormLayout(accelBox);
    addAxisRow(aLay, "Output X:", aSrc_[0], aInv_[0]);
    addAxisRow(aLay, "Output Y:", aSrc_[1], aInv_[1]);
    addAxisRow(aLay, "Output Z:", aSrc_[2], aInv_[2]);
    vlay->addWidget(accelBox);

    auto* hint = new QLabel(
        "<i>Default mapping follows SteamDeckGyroDSU orientation.<br>"
        "Use <b>Auto-detect</b> above if your emulator receives incorrect "
        "gyro orientation.</i>");
    hint->setWordWrap(true);
    vlay->addWidget(hint);
    vlay->addStretch();

    return w;
}

QWidget* MainWindow::buildCalibTab() {
    auto* w    = new QWidget;
    auto* vlay = new QVBoxLayout(w);
    vlay->setContentsMargins(12, 12, 12, 12);
    vlay->setSpacing(10);

    // Current bias display
    auto* biasBox = new QGroupBox("Current gyro bias (°/s)");
    auto* bLay    = new QHBoxLayout(biasBox);
    const char* axLabels[3] = {"X:", "Y:", "Z:"};
    for (int i = 0; i < 3; ++i) {
        bLay->addWidget(new QLabel(axLabels[i]));
        biasLbl_[i] = new QLabel("0.000");
        biasLbl_[i]->setMinimumWidth(70);
        biasLbl_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QFont mf = biasLbl_[i]->font();
        mf.setFamily("monospace");
        biasLbl_[i]->setFont(mf);
        bLay->addWidget(biasLbl_[i]);
        if (i < 2) bLay->addSpacing(16);
    }
    bLay->addStretch();
    vlay->addWidget(biasBox);

    // Instructions
    auto* instrLbl = new QLabel(
        "Place the controller on a flat, stable surface and do not touch it.\n"
        "Calibration takes 5 seconds and briefly stops the service.");
    instrLbl->setWordWrap(true);
    vlay->addWidget(instrLbl);

    // Calibration controls
    auto* ctrlRow = new QHBoxLayout;
    calibBtn_ = new QPushButton("Start Calibration");
    calibBtn_->setFixedWidth(160);
    connect(calibBtn_, &QPushButton::clicked, this, &MainWindow::onStartCalibration);
    ctrlRow->addWidget(calibBtn_);

    calibBar_ = new QProgressBar;
    calibBar_->setRange(0, 100);
    calibBar_->setValue(0);
    calibBar_->setTextVisible(false);
    calibBar_->setVisible(false);
    ctrlRow->addWidget(calibBar_, 1);
    vlay->addLayout(ctrlRow);

    calibStatusLbl_ = new QLabel;
    calibStatusLbl_->setVisible(false);
    vlay->addWidget(calibStatusLbl_);

    calibResultLbl_ = new QLabel;
    calibResultLbl_->setWordWrap(true);
    calibResultLbl_->setVisible(false);
    vlay->addWidget(calibResultLbl_);

    vlay->addStretch();
    return w;
}

// ──────────────────────────────────────────────────────────────────────────────
// Config ↔ UI sync
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::loadIntoUi(const Sc2Config& cfg) {
    portSpin_->setValue(cfg.port);
    exposeCb_->setChecked(cfg.expose);

    // Gyro map: src_x/y/z → [0][1][2] combos; inv_x/y/z → checkboxes
    const int gSrcArr[3] = { cfg.gyro.src_x, cfg.gyro.src_y, cfg.gyro.src_z };
    const bool gInvArr[3] = { cfg.gyro.inv_x, cfg.gyro.inv_y, cfg.gyro.inv_z };
    for (int i = 0; i < 3; ++i) {
        gSrc_[i]->setCurrentIndex(std::clamp(gSrcArr[i], 0, 2));
        gInv_[i]->setChecked(gInvArr[i]);
    }

    // Accel map
    const int aSrcArr[3] = { cfg.accel.src_x, cfg.accel.src_y, cfg.accel.src_z };
    const bool aInvArr[3] = { cfg.accel.inv_x, cfg.accel.inv_y, cfg.accel.inv_z };
    for (int i = 0; i < 3; ++i) {
        aSrc_[i]->setCurrentIndex(std::clamp(aSrcArr[i], 0, 2));
        aInv_[i]->setChecked(aInvArr[i]);
    }

    // Bias labels
    for (int i = 0; i < 3; ++i)
        biasLbl_[i]->setText(QString::number(static_cast<double>(cfg.gyroBias[i]), 'f', 3));
}

Sc2Config MainWindow::collectFromUi() const {
    Sc2Config cfg = cfg_;   // start from loaded config (preserves bias etc.)

    cfg.port   = static_cast<uint16_t>(portSpin_->value());
    cfg.expose = exposeCb_->isChecked();

    cfg.gyro.src_x = gSrc_[0]->currentIndex();
    cfg.gyro.inv_x = gInv_[0]->isChecked();
    cfg.gyro.src_y = gSrc_[1]->currentIndex();
    cfg.gyro.inv_y = gInv_[1]->isChecked();
    cfg.gyro.src_z = gSrc_[2]->currentIndex();
    cfg.gyro.inv_z = gInv_[2]->isChecked();

    cfg.accel.src_x = aSrc_[0]->currentIndex();
    cfg.accel.inv_x = aInv_[0]->isChecked();
    cfg.accel.src_y = aSrc_[1]->currentIndex();
    cfg.accel.inv_y = aInv_[1]->isChecked();
    cfg.accel.src_z = aSrc_[2]->currentIndex();
    cfg.accel.inv_z = aInv_[2]->isChecked();

    return cfg;
}

// ──────────────────────────────────────────────────────────────────────────────
// Service management
// ──────────────────────────────────────────────────────────────────────────────

bool MainWindow::isServiceRunning() const {
    QProcess p;
    p.start("systemctl", {"--user", "is-active", "--quiet", "SteamControllerGyroDSU.service"});
    p.waitForFinished(2000);
    return p.exitCode() == 0;
}

void MainWindow::updateServiceStatus() {
    bool running = isServiceRunning();
    serviceLabel_->setText(running
        ? "Service: <b style='color:green'>Running</b>"
        : "Service: <b style='color:red'>Stopped</b>");
    serviceLabel_->setTextFormat(Qt::RichText);
    btnStop_->setEnabled(running);
    btnRestart_->setEnabled(true);
}

void MainWindow::onServiceRefreshTimer() { updateServiceStatus(); }

void MainWindow::onServiceStop() {
    QProcess::execute("systemctl", {"--user", "stop", "SteamControllerGyroDSU.service"});
    QTimer::singleShot(600, this, &MainWindow::updateServiceStatus);
}

void MainWindow::onServiceRestart() {
    QProcess::execute("systemctl", {"--user", "restart", "SteamControllerGyroDSU.service"});
    QTimer::singleShot(800, this, &MainWindow::updateServiceStatus);
}

// ──────────────────────────────────────────────────────────────────────────────
// Save / Reset
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::onSaveApply() {
    cfg_ = collectFromUi();
    if (!cfg_.save()) {
        QMessageBox::warning(this, "Save failed",
            "Could not write to " + QString::fromStdString(Sc2Config::configPath()));
        return;
    }
    // Restart the service so it picks up the new config,
    // and reconnect the test tab's DSU client to the new port.
    onServiceRestart();
    if (testTab_) testTab_->setPort(cfg_.port);
    statusBar()->showMessage("Saved — service restarting…", 3000);
}

void MainWindow::onResetDefaults() {
    auto ans = QMessageBox::question(this, "Reset to defaults",
        "Reset all settings to compiled-in defaults?",
        QMessageBox::Yes | QMessageBox::Cancel);
    if (ans != QMessageBox::Yes) return;
    loadIntoUi(Sc2Config{});
}

// ──────────────────────────────────────────────────────────────────────────────
// Axis wizard
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::onAxisWizard() {
    // Save current UI state into cfg_ so the wizard can save/restore it.
    cfg_ = collectFromUi();

    AxisWizard wiz(cfg_, this);
    if (wiz.exec() != QDialog::Accepted)
        return;   // wizard cancelled — it already restored original config & service

    // Apply detected mapping
    cfg_ = wiz.detectedConfig();
    cfg_.save();
    loadIntoUi(cfg_);

    if (testTab_) testTab_->setPort(cfg_.port);

    // Offer to calibrate immediately — axis remapping invalidates old bias values.
    auto ans = QMessageBox::question(this, "Calibrate gyro?",
        "Axis mapping updated.\n\n"
        "The previous gyro bias calibration was measured for the old axis mapping "
        "and should be re-done to fix drift.\n\n"
        "Run calibration now? (Place the controller flat and still first.)",
        QMessageBox::Yes | QMessageBox::No);

    if (ans == QMessageBox::Yes) {
        tabs_->setCurrentIndex(2);   // switch to Calibration tab
        onStartCalibration();        // calibration restarts the service when done
        statusBar()->showMessage("Calibrating gyro with new axis mapping…", 5000);
    } else {
        onServiceRestart();
        statusBar()->showMessage("Axis mapping updated from wizard — service restarting…", 4000);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Calibration
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::setCalibUiEnabled(bool enabled) {
    calibBtn_->setEnabled(enabled);
    calibBar_->setVisible(!enabled);
    calibStatusLbl_->setVisible(!enabled);
}

void MainWindow::showCalibResult(const QString& msg, bool ok) {
    calibResultLbl_->setText(msg);
    calibResultLbl_->setStyleSheet(ok ? "color: green;" : "color: red;");
    calibResultLbl_->setVisible(true);
}

void MainWindow::onStartCalibration() {
    // Prevent double-start.
    if (calibThread_ && calibThread_->isRunning()) return;

    calibResultLbl_->setVisible(false);

    // Stop the service so the worker can open the HID device.
    bool wasSvcRunning = isServiceRunning();
    if (wasSvcRunning) {
        calibStatusLbl_->setText("Stopping service…");
        calibStatusLbl_->setVisible(true);
        QApplication::processEvents();
        QProcess::execute("systemctl", {"--user", "stop", "SteamControllerGyroDSU.service"});
        QThread::msleep(800);   // let hidraw release
    }

    setCalibUiEnabled(false);
    calibBar_->setValue(0);
    calibStatusLbl_->setText("Collecting samples… (hold the controller still)");

    auto* worker = new CalibWorker(5000);
    calibThread_ = new QThread(this);
    worker->moveToThread(calibThread_);

    connect(calibThread_, &QThread::started,  worker, &CalibWorker::run);
    connect(calibThread_, &QThread::finished, worker, &QObject::deleteLater);

    connect(worker, &CalibWorker::progress, this, &MainWindow::onCalibProgress,
            Qt::QueuedConnection);
    connect(worker, &CalibWorker::done, this, [this, wasSvcRunning]
            (float bx, float by, float bz, QString error) {
        onCalibDone(bx, by, bz, error);
        // Always restart the service if it was running before.
        if (wasSvcRunning)
            QProcess::execute("systemctl", {"--user", "start", "SteamControllerGyroDSU.service"});
        QTimer::singleShot(800, this, &MainWindow::updateServiceStatus);
    }, Qt::QueuedConnection);

    calibThread_->start();
}

void MainWindow::onCalibProgress(int pct) {
    calibBar_->setValue(pct);
}

void MainWindow::onCalibDone(float bx, float by, float bz, QString error) {
    if (calibThread_) {
        calibThread_->quit();
        calibThread_->wait();
        calibThread_ = nullptr;
    }

    setCalibUiEnabled(true);
    calibStatusLbl_->setVisible(false);

    if (!error.isEmpty()) {
        showCalibResult("Calibration failed: " + error, false);
        return;
    }

    // Save bias into config and update UI.
    cfg_.gyroBias[0] = bx;
    cfg_.gyroBias[1] = by;
    cfg_.gyroBias[2] = bz;
    cfg_.save();

    for (int i = 0; i < 3; ++i)
        biasLbl_[i]->setText(QString::number(static_cast<double>(cfg_.gyroBias[i]), 'f', 3));

    showCalibResult(
        QString("Calibration complete — bias saved.\n"
                "X: %1  Y: %2  Z: %3  (°/s)")
            .arg(bx, 0, 'f', 3)
            .arg(by, 0, 'f', 3)
            .arg(bz, 0, 'f', 3),
        true);
}

} // namespace sc2
