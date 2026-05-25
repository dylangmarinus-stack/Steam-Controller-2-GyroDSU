#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QGroupBox>
#include <QTabWidget>
#include <QTimer>
#include <QThread>
#include "config.h"
#include "test_tab.h"

namespace sc2 {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onServiceRefreshTimer();
    void onServiceStop();
    void onServiceRestart();
    void onSaveApply();
    void onResetDefaults();
    void onAxisWizard();
    void onStartCalibration();
    void onCalibProgress(int pct);
    void onCalibDone(float bx, float by, float bz, QString error);

private:
    // UI construction
    void setupUi();
    QWidget* buildServerTab();
    QWidget* buildAxisTab();
    QWidget* buildCalibTab();

    // Helpers
    void updateServiceStatus();
    bool isServiceRunning() const;
    void loadIntoUi(const Sc2Config& cfg);
    Sc2Config collectFromUi() const;
    void setCalibUiEnabled(bool enabled);
    void showCalibResult(const QString& msg, bool ok);

    // ── status bar area ──
    QLabel*      serviceLabel_ = nullptr;
    QPushButton* btnStop_      = nullptr;
    QPushButton* btnRestart_   = nullptr;

    // ── server tab ──
    QSpinBox* portSpin_  = nullptr;
    QCheckBox* exposeCb_ = nullptr;

    // ── axis tab ──
    // [0]=X [1]=Y [2]=Z output axis; gyro + accel each
    QComboBox*   gSrc_[3]    = {};
    QCheckBox*   gInv_[3]    = {};
    QComboBox*   aSrc_[3]    = {};
    QCheckBox*   aInv_[3]    = {};
    QPushButton* wizardBtn_  = nullptr;

    // ── calibration tab ──
    QLabel*       biasLbl_[3]    = {};   // current bias display
    QPushButton*  calibBtn_      = nullptr;
    QProgressBar* calibBar_      = nullptr;
    QLabel*       calibStatusLbl_= nullptr;
    QLabel*       calibResultLbl_= nullptr;

    // ── service refresh ──
    QTimer* svcTimer_ = nullptr;

    // ── tabs ──
    QTabWidget* tabs_    = nullptr;
    TestTab*    testTab_ = nullptr;

    // ── calibration worker ──
    QThread* calibThread_ = nullptr;

    // ── current config ──
    Sc2Config cfg_;
};

} // namespace sc2
