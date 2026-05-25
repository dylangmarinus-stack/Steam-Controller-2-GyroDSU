#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <cstdint>

namespace sc2 {

// Lightweight Cemuhook/DSU UDP client.
// Subscribes to a running sc2gyrodsu server and emits motion samples.
// All operations run in the caller's thread (Qt event-loop driven, no threads).
class DsuClient : public QObject {
    Q_OBJECT
public:
    explicit DsuClient(QObject* parent = nullptr);

    // Connect to the server and start receiving. Port should match the daemon.
    void start(quint16 port = 26761);
    void stop();

    bool isConnected() const { return connected_; }

signals:
    // Emitted when the first packet arrives after disconnected state.
    void serverFound();
    // Emitted when no packet has been received for > 3 seconds.
    void serverLost();

    // Live motion sample. accel_g[] in g, gyro_dps[] in °/s.
    // slot: DSU slot index 0-3.
    void sampleReceived(int slot,
                        float accel_x_g,  float accel_y_g,  float accel_z_g,
                        float gyro_x_dps, float gyro_y_dps, float gyro_z_dps,
                        quint64 timestamp_us);

private slots:
    void onReadyRead();
    void onSubTimer();
    void onWatchdogTimer();

private:
    void sendSubscription();
    bool parseDataPacket(const QByteArray& data, int& slot_out,
                         float accel[3], float gyro[3], quint64& ts_out);

    QUdpSocket socket_;
    QTimer     subTimer_;      // re-subscription keepalive (every 2 s)
    QTimer     watchdogTimer_; // detect server loss (every 1 s)

    quint16    port_       = 26761;
    quint32    clientId_   = 0;
    bool       connected_  = false;
    qint64     lastPacketMs_ = 0;
};

} // namespace sc2
