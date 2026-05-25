#include "dsu_client.h"
#include <QHostAddress>
#include <QDateTime>
#include <cstring>

namespace sc2 {

// ── Minimal DSU/Cemuhook protocol constants ───────────────────────────────────
static const quint8  MAGIC_CLIENT[4] = {'D','S','U','C'};
static const quint8  MAGIC_SERVER[4] = {'D','S','U','S'};
static const quint16 PROTO_VER       = 1001;
static const quint32 MSG_DATA        = 0x100002;

// Packet layout (all LE):
//   [0-3]   magic
//   [4-5]   version
//   [6-7]   payload_len  (total - 16)
//   [8-11]  CRC32        (with this field zeroed for computation)
//   [12-15] server/client id
//   [16-19] msg_type
//   [20+]   payload

static const int HDR_FULL     = 20;  // bytes before payload
static const int CRC_OFFSET   = 8;
static const int CRC_LEN      = 4;
static const int CTRL_HDR_LEN = 11;  // slot(1)+state(3)+mac(6)+battery(1)

// Data packet structure:
//   [20..30]  controller info header (CTRL_HDR_LEN)
//   [31]      connected flag
//   [32..35]  per-subscriber packet counter
//   [36..40]  buttons 1/2/home/touch (5 bytes)
//   [40..43]  sticks (4 bytes)
//   [44..56]  analog (13 bytes)
//   [57..68]  reserved (12 bytes)
//   [68..75]  timestamp_us (uint64)
//   [76..99]  motion: 6 × float32 (accel xyz, gyro xyz)
static const int MOTION_OFFSET    = HDR_FULL + CTRL_HDR_LEN + 45;  // = 76
static const int TIMESTAMP_OFFSET = HDR_FULL + CTRL_HDR_LEN + 37;  // = 68
static const int SLOT_OFFSET      = HDR_FULL;                       // = 20

// ── CRC32 ─────────────────────────────────────────────────────────────────────
static quint32 crc32Table[256];
static bool    crc32TableReady = false;

static void buildCrc32Table() {
    for (quint32 i = 0; i < 256; i++) {
        quint32 c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32Table[i] = c;
    }
    crc32TableReady = true;
}

static quint32 crc32(const quint8* data, int len) {
    if (!crc32TableReady) buildCrc32Table();
    quint32 c = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++)
        c = crc32Table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static quint32 crcZeroed(const QByteArray& msg) {
    QByteArray tmp = msg;
    tmp[CRC_OFFSET+0] = tmp[CRC_OFFSET+1] = tmp[CRC_OFFSET+2] = tmp[CRC_OFFSET+3] = 0;
    return crc32(reinterpret_cast<const quint8*>(tmp.constData()), tmp.size());
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static void w16(quint8* p, quint16 v) { p[0]=v&0xFF; p[1]=v>>8; }
static void w32(quint8* p, quint32 v) {
    p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}
static quint32 r32(const quint8* p) {
    return p[0]|((quint32)p[1]<<8)|((quint32)p[2]<<16)|((quint32)p[3]<<24);
}
static float r32f(const quint8* p) {
    quint32 v = r32(p);
    float f; memcpy(&f, &v, 4);
    return f;
}
static quint64 r64(const quint8* p) {
    quint64 v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

// ── DsuClient ─────────────────────────────────────────────────────────────────

DsuClient::DsuClient(QObject* parent)
    : QObject(parent)
{
    clientId_ = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);

    connect(&socket_,      &QUdpSocket::readyRead, this, &DsuClient::onReadyRead);
    connect(&subTimer_,    &QTimer::timeout,       this, &DsuClient::onSubTimer);
    connect(&watchdogTimer_,&QTimer::timeout,      this, &DsuClient::onWatchdogTimer);

    subTimer_.setInterval(2000);
    watchdogTimer_.setInterval(1000);
}

void DsuClient::start(quint16 port) {
    stop();
    port_ = port;
    socket_.bind(QHostAddress::LocalHost, 0);  // ephemeral local port
    subTimer_.start();
    watchdogTimer_.start();
    sendSubscription();  // subscribe immediately
}

void DsuClient::stop() {
    subTimer_.stop();
    watchdogTimer_.stop();
    socket_.close();
    if (connected_) { connected_ = false; emit serverLost(); }
}

// Build and send the subscription packet (reg=0 = subscribe to all slots).
void DsuClient::sendSubscription() {
    quint8 buf[28] = {};
    memcpy(buf,    MAGIC_CLIENT, 4);
    w16(buf+4,  PROTO_VER);
    w16(buf+6,  28 - 16);          // payload_len = 12
    // CRC placeholder stays zero until computed below
    w32(buf+12, clientId_);
    buf[16]=0x02; buf[17]=0x00; buf[18]=0x10; buf[19]=0x00;  // MSG_DATA LE
    buf[20]=0;    // reg = 0 (all slots)
    // bytes 21-27 = 0 (slot, MAC padding)

    quint32 crc = crc32(buf, 28);  // CRC with field still zeroed
    w32(buf+CRC_OFFSET, crc);

    socket_.writeDatagram(reinterpret_cast<const char*>(buf), 28,
                          QHostAddress::LocalHost, port_);
}

void DsuClient::onSubTimer()      { sendSubscription(); }

void DsuClient::onWatchdogTimer() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (connected_ && now - lastPacketMs_ > 3000) {
        connected_ = false;
        emit serverLost();
    }
}

void DsuClient::onReadyRead() {
    while (socket_.hasPendingDatagrams()) {
        QByteArray data(static_cast<int>(socket_.pendingDatagramSize()), 0);
        socket_.readDatagram(data.data(), data.size());

        int slot;
        float accel[3], gyro[3];
        quint64 ts;
        if (!parseDataPacket(data, slot, accel, gyro, ts)) continue;

        lastPacketMs_ = QDateTime::currentMSecsSinceEpoch();
        if (!connected_) {
            connected_ = true;
            emit serverFound();
        }

        emit sampleReceived(slot,
                            accel[0], accel[1], accel[2],
                            gyro[0],  gyro[1],  gyro[2],
                            ts);
    }
}

bool DsuClient::parseDataPacket(const QByteArray& data, int& slot_out,
                                float accel[3], float gyro[3], quint64& ts_out) {
    if (data.size() < 100) return false;

    const auto* b = reinterpret_cast<const quint8*>(data.constData());

    // Validate magic and version
    if (memcmp(b, MAGIC_SERVER, 4) != 0) return false;
    quint16 ver = b[4] | (quint16(b[5]) << 8);
    if (ver != PROTO_VER) return false;

    // Validate message type
    quint32 mtype = r32(b+16);
    if (mtype != MSG_DATA) return false;

    // Validate CRC
    if (r32(b+CRC_OFFSET) != crcZeroed(data)) return false;

    // Check controller is connected
    if (b[SLOT_OFFSET + 1] == 0) return false;   // slot_state == disconnected

    slot_out = b[SLOT_OFFSET];
    ts_out   = r64(b + TIMESTAMP_OFFSET);

    accel[0] = r32f(b + MOTION_OFFSET +  0);
    accel[1] = r32f(b + MOTION_OFFSET +  4);
    accel[2] = r32f(b + MOTION_OFFSET +  8);
    gyro[0]  = r32f(b + MOTION_OFFSET + 12);
    gyro[1]  = r32f(b + MOTION_OFFSET + 16);
    gyro[2]  = r32f(b + MOTION_OFFSET + 20);

    return true;
}

} // namespace sc2
