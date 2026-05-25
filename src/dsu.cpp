#include "dsu.h"
#include "triton.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace std::chrono_literals;

namespace sc2 {

// Exact same layout as sc2dsu Rust version
// HEADER_LEN      = 16  (magic 4 + ver 2 + len 2 + crc 4 + ... wait)
// Actually:
//   0-3:   magic
//   4-5:   version
//   6-7:   payload length (= total - 16)
//   8-11:  CRC32
//   12-15: server_id
//   16-19: msg_type
//   20+:   payload
// HEADER_LEN      = 16 (up to and including CRC)
// HEADER_LEN_FULL = 20 (including server_id and msg_type)

static const uint8_t  MAGIC_CLIENT[4] = {'D','S','U','C'};
static const uint8_t  MAGIC_SERVER[4] = {'D','S','U','S'};
static const uint16_t PROTO_VER       = 1001;
static const uint32_t MSG_VERSION     = 0x100000;
static const uint32_t MSG_PORTS       = 0x100001;
static const uint32_t MSG_DATA        = 0x100002;

static const size_t HEADER_LEN        = 16;
static const size_t HEADER_LEN_FULL   = 20;
static const size_t CTRL_HDR_LEN      = 11;
static const size_t CRC_OFFSET        = 8;
static const size_t CRC_LEN           = 4;
static const size_t MAX_SLOTS         = 4;
static const size_t MAX_SUBS          = 32;
static const auto   SUB_TIMEOUT       = 5s;
static const auto   STAT_INTERVAL     = 5s;
static const uint8_t TRIG_THRESH      = 200;

static const uint8_t SLOT_MACS[4][6] = {
    {0,0,0,0,0,1},{0,0,0,0,0,2},{0,0,0,0,0,3},{0,0,0,0,0,4}
};

// CRC32
static uint32_t crc32tab[256];
static bool     crc32ready = false;

static void buildCrc32() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32tab[i] = c;
    }
    crc32ready = true;
}

static uint32_t crc32(const uint8_t* data, size_t len) {
    if (!crc32ready) buildCrc32();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = crc32tab[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static uint32_t crcZeroed(const uint8_t* msg, size_t len) {
    std::vector<uint8_t> tmp(msg, msg + len);
    memset(tmp.data() + CRC_OFFSET, 0, CRC_LEN);
    return crc32(tmp.data(), len);
}

static void w16(uint8_t* p, uint16_t v) { p[0]=v&0xFF; p[1]=v>>8; }
static void w32(uint8_t* p, uint32_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }
static uint16_t r16(const uint8_t* p) { return p[0]|((uint16_t)p[1]<<8); }
static uint32_t r32(const uint8_t* p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

// Exact same as Rust write_header
static void writeHeader(uint8_t* out, size_t totalLen, uint32_t sid, uint32_t mtype) {
    memcpy(out, MAGIC_SERVER, 4);
    w16(out+4, PROTO_VER);
    w16(out+6, (uint16_t)(totalLen - HEADER_LEN));
    memset(out+CRC_OFFSET, 0, CRC_LEN);
    w32(out+12, sid);
    w32(out+16, mtype);
}

static void finCrc(uint8_t* out, size_t len) {
    w32(out+CRC_OFFSET, crcZeroed(out, len));
}

static void writeCtrlHdr(uint8_t* buf, uint8_t slot, bool conn) {
    buf[0] = slot;
    if (conn && slot < MAX_SLOTS) {
        buf[1] = 1; buf[2] = 2; buf[3] = 1;
        memcpy(buf+4, SLOT_MACS[slot], 6);
        buf[10] = 0;
    } else {
        memset(buf+1, 0, 10);
    }
}

static uint8_t stk(int16_t v)  { return (uint8_t)((((int32_t)v+32768))>>8); }
static uint8_t trg(uint16_t v) { uint32_t r=v>>7; return r>255?255:(uint8_t)r; }

static void writeDataBody(uint8_t* body, const ControllerState& s) {
    // Exact same offsets as Rust sc2dsu write_data_body
    // B_CONNECTED = 0
    // B_BUTTONS   = 5
    // B_STICKS    = 9
    // B_ANALOG    = 13
    // B_TIMESTAMP = 37
    // B_MOTION    = 45

    body[0] = 1; // connected

    uint8_t l2=trg(s.trigger_left), r2=trg(s.trigger_right);
    bool l2d=l2>=TRIG_THRESH, r2d=r2>=TRIG_THRESH;

    using namespace button;
    auto dn=[&](uint32_t m){return (s.buttons&m)!=0;};

    // buttons1 at body[5]
    body[5]=(uint8_t)(
        (dn(MENU)?0x01:0)|(dn(L3)?0x02:0)|(dn(R3)?0x04:0)|(dn(VIEW)?0x08:0)|
        (dn(DPAD_UP)?0x10:0)|(dn(DPAD_RIGHT)?0x20:0)|(dn(DPAD_DOWN)?0x40:0)|(dn(DPAD_LEFT)?0x80:0));
    // buttons2 at body[6]
    body[6]=(uint8_t)(
        (l2d?0x01:0)|(r2d?0x02:0)|(dn(L)?0x04:0)|(dn(R)?0x08:0)|
        (dn(X)?0x10:0)|(dn(A)?0x20:0)|(dn(B)?0x40:0)|(dn(Y)?0x80:0));
    body[7]=dn(STEAM)?1:0;
    body[8]=dn(QAM)?1:0;

    // sticks at body[9..13]
    body[9] =stk(s.left_stick[0]);
    body[10]=stk(s.left_stick[1]);
    body[11]=stk(s.right_stick[0]);
    body[12]=stk(s.right_stick[1]);

    // analog at body[13..25]
    auto fl=[](bool on)->uint8_t{return on?255:0;};
    body[13]=fl(dn(DPAD_LEFT));
    body[14]=fl(dn(DPAD_DOWN));
    body[15]=fl(dn(DPAD_RIGHT));
    body[16]=fl(dn(DPAD_UP));
    body[17]=fl(dn(Y));
    body[18]=fl(dn(B));
    body[19]=fl(dn(A));
    body[20]=fl(dn(X));
    body[21]=fl(dn(R));
    body[22]=fl(dn(L));
    body[23]=r2;
    body[24]=l2;

    // reserved body[25..37]
    memset(body+25, 0, 12);

    // timestamp at body[37..45]
    uint64_t ts=s.imu.timestamp_us;
    memcpy(body+37, &ts, 8);

    // motion at body[45..69]
    float mo[6]={
        s.imu.accel_g[0], s.imu.accel_g[1], s.imu.accel_g[2],
        s.imu.gyro_dps[0], s.imu.gyro_dps[1], s.imu.gyro_dps[2]
    };
    memcpy(body+45, mo, 24);
}

static uint32_t randId() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint32_t)(ts.tv_nsec^((uint32_t)getpid()*2654435761u));
}

// DsuServer implementation

DsuServer::DsuServer(uint16_t port, bool expose)
    : port_(port), expose_(expose), serverId_(randId())
{
    memset(slotConnected_, 0, sizeof(slotConnected_));
    windowStart_ = std::chrono::steady_clock::now();
}

DsuServer::~DsuServer() { stop(); }

void DsuServer::start() {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) { perror("socket"); return; }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = expose_ ? INADDR_ANY : htonl(INADDR_LOOPBACK);
    if (bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock_); sock_=-1; return;
    }
    struct timeval tv{0,10000};
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    running_ = true;
    thread_ = std::thread(&DsuServer::serverLoop, this);
    fprintf(stderr, "sc2gyrodsu: DSU server on %s:%u  (id 0x%08X)\n",
        expose_?"0.0.0.0":"127.0.0.1", port_, serverId_);
}

void DsuServer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (sock_ >= 0) { close(sock_); sock_=-1; }
}

bool DsuServer::hasSubscribers() const {
    std::lock_guard<std::mutex> lk(subMutex_);
    return !subscribers_.empty();
}

void DsuServer::setDisconnected(int slot) {
    std::lock_guard<std::mutex> lk(stateMutex_);
    if (slot>=0&&slot<4) slotConnected_[slot]=false;
}

void DsuServer::pushState(const ControllerState& state) {
    { std::lock_guard<std::mutex> lk(stateMutex_);
      int s=state.slot; if(s>=0&&s<4) slotConnected_[s]=true; }
    samplesInWindow_.fetch_add(1, std::memory_order_relaxed);
    broadcastData(state);
}

void DsuServer::serverLoop() {
    uint8_t buf[1024];
    while (running_) {
        struct sockaddr_in src{};
        socklen_t sl=sizeof(src);
        int n=recvfrom(sock_,buf,sizeof(buf),0,(struct sockaddr*)&src,&sl);
        if (n > 0) { requestsInWindow_.fetch_add(1, std::memory_order_relaxed); handleMessage(buf,n,src); }
        cleanupSubscribers();
        auto now=std::chrono::steady_clock::now();
        if (now-windowStart_>=STAT_INTERVAL) {
            float secs=std::chrono::duration<float>(now-windowStart_).count();
            size_t ns; { std::lock_guard<std::mutex> lk(subMutex_); ns=subscribers_.size(); }
            uint32_t smpl = samplesInWindow_.exchange(0, std::memory_order_relaxed);
            uint32_t pkts = packetsInWindow_.exchange(0, std::memory_order_relaxed);
            uint32_t reqs = requestsInWindow_.exchange(0, std::memory_order_relaxed);
            fprintf(stderr,"dsu: %zu sub(s) | %.1f samples/s | %.1f packets/s | %.1f reqs/s\n",
                ns, smpl/secs, pkts/secs, reqs/secs);
            windowStart_=now;
        }
    }
}

void DsuServer::handleMessage(const uint8_t* buf, int n, struct sockaddr_in& src) {
    if (n < (int)HEADER_LEN_FULL) return;
    if (memcmp(buf, MAGIC_CLIENT, 4) != 0) return;
    if (r16(buf+4) != PROTO_VER) return;
    uint16_t payLen = r16(buf+6);
    if (n < (int)(HEADER_LEN + payLen)) return;
    if (r32(buf+CRC_OFFSET) != crcZeroed(buf, HEADER_LEN + payLen)) return;
    uint32_t clientId = r32(buf+12);
    uint32_t mtype    = r32(buf+16);
    const uint8_t* pay = buf + HEADER_LEN_FULL;
    int plen = n - (int)HEADER_LEN_FULL;

    switch (mtype) {
        case MSG_VERSION:
            sendVersion(src);
            break;
        case MSG_PORTS:
            if (plen >= 4) {
                uint32_t amt = r32(pay);
                if (amt > 4) amt = 4;
                for (uint32_t i = 0; i < amt && (int)(4+i) <= plen; i++)
                    sendSlotInfo(src, pay[4+i]);
            }
            break;
        case MSG_DATA: {
            if (plen < 8) break;
            uint8_t reg=pay[0], slot=pay[1];
            bool want=(reg==0)||(reg&1&&slot<MAX_SLOTS)||(reg&2);
            if (!want) break;
            std::lock_guard<std::mutex> lk(subMutex_);
            if (subscribers_.size()>=MAX_SUBS&&subscribers_.find(clientId)==subscribers_.end()) break;
            bool wasEmpty=subscribers_.empty();
            auto& sub=subscribers_[clientId];
            sub.addr=src;
            sub.lastRequest=std::chrono::steady_clock::now();
            if (wasEmpty) fprintf(stderr,"dsu: first subscriber %08X — waking controllers\n",clientId);
            break;
        }
    }
}

void DsuServer::sendVersion(struct sockaddr_in& src) {
    // Exact same as Rust: HEADER_LEN_FULL + 2 = 22 bytes
    uint8_t out[HEADER_LEN_FULL + 2] = {};
    writeHeader(out, sizeof(out), serverId_, MSG_VERSION);
    w16(out + HEADER_LEN_FULL, PROTO_VER);
    finCrc(out, sizeof(out));
    sendto(sock_, out, sizeof(out), 0, (struct sockaddr*)&src, sizeof(src));
}

void DsuServer::sendSlotInfo(struct sockaddr_in& src, uint8_t slot) {
    // Exact same as Rust: HEADER_LEN_FULL + 12 = 32 bytes
    uint8_t out[HEADER_LEN_FULL + 12] = {};
    writeHeader(out, sizeof(out), serverId_, MSG_PORTS);
    bool conn; { std::lock_guard<std::mutex> lk(stateMutex_); conn=slot<4&&slotConnected_[slot]; }
    writeCtrlHdr(out + HEADER_LEN_FULL, slot, conn);
    finCrc(out, sizeof(out));
    sendto(sock_, out, sizeof(out), 0, (struct sockaddr*)&src, sizeof(src));
}

void DsuServer::broadcastData(const ControllerState& state) {
    std::lock_guard<std::mutex> lk(subMutex_);
    if (subscribers_.empty()) return;
    uint8_t slot=(uint8_t)(state.slot<4?state.slot:0);

    // Exact same as Rust: HEADER_LEN_FULL + 80 = 100 bytes
    uint8_t out[HEADER_LEN_FULL + 80] = {};
    writeHeader(out, sizeof(out), serverId_, MSG_DATA);
    writeCtrlHdr(out + HEADER_LEN_FULL, slot, true);

    // packet number offset: HEADER_LEN_FULL + CTRL_HDR_LEN + 1
    const size_t PCNT_OFF = HEADER_LEN_FULL + CTRL_HDR_LEN + 1;

    writeDataBody(out + HEADER_LEN_FULL + CTRL_HDR_LEN, state);

    for (auto& [id,sub] : subscribers_) {
        sub.packetCounters[slot]++;
        w32(out + PCNT_OFF, sub.packetCounters[slot]);
        finCrc(out, sizeof(out));
        sendto(sock_, out, sizeof(out), 0, (struct sockaddr*)&sub.addr, sizeof(sub.addr));
        packetsInWindow_.fetch_add(1, std::memory_order_relaxed);
    }
}

void DsuServer::cleanupSubscribers() {
    auto now=std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(subMutex_);
    bool wasEmpty=subscribers_.empty();
    for (auto it=subscribers_.begin();it!=subscribers_.end();) {
        if (now-it->second.lastRequest>=SUB_TIMEOUT) it=subscribers_.erase(it);
        else ++it;
    }
    if (!wasEmpty&&subscribers_.empty())
        fprintf(stderr,"dsu: last subscriber timed out\n");
}

} // namespace sc2
