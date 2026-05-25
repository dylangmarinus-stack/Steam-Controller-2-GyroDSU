#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QVector>
#include <QTimer>
#include "config.h"

namespace sc2 {

class DsuClient;

// ─────────────────────────────────────────────────────────────────────────────
// AxisWizard
//
// Guides the user through four simple gestures while collecting live DSU data
// (with a temporary identity mapping so raw sensor axes map 1:1 to DSU axes).
// Detects which raw axis corresponds to roll, pitch, gravity, and yaw, and
// sets the correct src_*/inv_* values in Sc2Config.
//
// Usage:
//   AxisWizard wiz(currentCfg, this);
//   if (wiz.exec() == QDialog::Accepted)
//       applyConfig(wiz.detectedConfig());
// ─────────────────────────────────────────────────────────────────────────────
class AxisWizard : public QDialog {
    Q_OBJECT

public:
    struct Sample { float a[3]; float g[3]; };

    explicit AxisWizard(const Sc2Config& current, QWidget* parent = nullptr);
    ~AxisWizard() override;

    // Call after exec() == Accepted to get the detected mapping.
    Sc2Config detectedConfig() const { return result_; }

private slots:
    void onNext();
    void onCancel();
    void onCollectTick();
    void onSample(int slot, float ax, float ay, float az,
                  float gx, float gy, float gz, quint64 ts);

private:
    enum Stage {
        INTRO,
        WAIT_SERVICE,
        FLAT,
        TILT_RIGHT,
        TILT_TOP,
        SPIN,
        RESULT
    };

    void goToStage(Stage s);
    void startCollection();          // begin gesture data collection after confirm
    void checkStageComplete(Stage next, const char* label);
    bool buildMapping();   // fills result_; returns false on error

    // ── Config ───────────────────────────────────────────────────────────────
    Sc2Config savedCfg_;
    Sc2Config result_;

    // ── UI ───────────────────────────────────────────────────────────────────
    QLabel*       instrLbl_   = nullptr;
    QLabel*       statusLbl_  = nullptr;
    QProgressBar* progBar_    = nullptr;
    QPushButton*  nextBtn_    = nullptr;
    QPushButton*  cancelBtn_  = nullptr;

    // ── Data collection ───────────────────────────────────────────────────────
    Stage stage_             = INTRO;
    bool  waitingConfirm_    = false;  // true while awaiting Begin / tap to start
    DsuClient* client_       = nullptr;
    QTimer*    collectTimer_ = nullptr;

    int collectMs_  = 0;
    int elapsedMs_  = 0;

    QVector<Sample>  flatSamples_;
    QVector<Sample>  tiltRightSamples_;
    QVector<Sample>  tiltTopSamples_;
    QVector<Sample>  spinSamples_;
    QVector<Sample>* activeSamples_ = nullptr;

    // ── Detection results ─────────────────────────────────────────────────────
    QString detectError_;
    QString resultSummary_;
};

} // namespace sc2
