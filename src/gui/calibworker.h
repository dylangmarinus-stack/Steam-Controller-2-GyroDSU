#pragma once
#include <QObject>
#include <QString>

namespace sc2 {

// Runs gyro bias calibration in a worker thread.
//
// The caller is responsible for stopping the sc2gyrodsu service before
// starting the worker, and restarting it after the done() signal fires.
//
// Usage:
//   auto* worker = new CalibWorker;
//   worker->moveToThread(thread);
//   connect(thread, &QThread::started, worker, &CalibWorker::run);
//   connect(worker, &CalibWorker::progress, ...);
//   connect(worker, &CalibWorker::done, ...);
//   thread->start();
class CalibWorker : public QObject {
    Q_OBJECT
public:
    explicit CalibWorker(int durationMs = 5000, QObject* parent = nullptr);

public slots:
    void run();

signals:
    // Emitted periodically during calibration; pct in [0, 100].
    void progress(int pct);

    // Emitted when calibration finishes.
    // On success, error is empty and bx/by/bz hold the computed bias (°/s).
    // On failure, bx/by/bz are 0 and error describes what went wrong.
    void done(float bx, float by, float bz, QString error);

private:
    int durationMs_;
};

} // namespace sc2
