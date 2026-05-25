#include "hiddev.h"
#include "dsu.h"
#include "triton.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <chrono>
#include <string>

using namespace std::chrono_literals;
using namespace sc2;

static std::atomic<bool> gShutdown{false};

static void signalHandler(int sig) {
    fprintf(stderr, "\nsc2gyrodsu: signal %d — shutting down\n", sig);
    gShutdown = true;
}

struct SlotReader {
    std::unique_ptr<HidSlot> slot;
    std::thread              thread;
    std::atomic<bool>        done{false};
};

static void slotReaderThread(HidSlot* slot, DsuServer* server, std::atomic<bool>& done) {
    const auto SILENCE_MAX = 2000ms;
    int      staleCnt  = 0;
    auto     lastSample = std::chrono::steady_clock::now();
    uint32_t lastTs    = 0;

    fprintf(stderr, "triton-%d: reader started (id=%s)\n",
            slot->dsuSlot, slot->serial.c_str());

    while (!gShutdown && !done) {
        ControllerState state;
        bool ok = slot->readOne(state);

        if (ok) {
            lastSample = std::chrono::steady_clock::now();
            if (state.imu.timestamp_us == lastTs) {
                staleCnt++;
            } else {
                staleCnt = 0;
                lastTs   = state.imu.timestamp_us;
                server->pushState(state);
            }
        } else {
            if (std::chrono::steady_clock::now() - lastSample >= SILENCE_MAX) {
                fprintf(stderr, "triton-%d: no data for 2s — disconnecting\n", slot->dsuSlot);
                break;
            }
        }
    }

    fprintf(stderr, "triton-%d: reader exiting\n", slot->dsuSlot);
    server->setDisconnected(slot->dsuSlot);
    done = true;
}

static void printHelp(const char* name) {
    fprintf(stderr,
        "sc2gyrodsu — Cemuhook DSU server for Steam Controller 2\n"
        "Usage: %s [--port PORT] [--expose] [--probe] [--help]\n"
        "\n"
        "  --port PORT   UDP port to listen on (default: 26761)\n"
        "  --expose      Bind to 0.0.0.0 instead of 127.0.0.1\n"
        "  --probe       List Valve HID interfaces and exit\n"
        "  --no-config   Ignore ~/.config/sc2gyrodsu/config.ini\n",
        name);
}

int main(int argc, char* argv[]) {
    bool probe    = false;
    bool noConfig = false;
    // Port and expose start unset; config fills them, CLI overrides after.
    int  portOverride   = -1;
    int  exposeOverride = -1;

    for (int i = 1; i < argc; ++i) {
        if      (strcmp(argv[i], "--probe")     == 0) probe          = true;
        else if (strcmp(argv[i], "--expose")    == 0) exposeOverride = 1;
        else if (strcmp(argv[i], "--no-config") == 0) noConfig       = true;
        else if (strcmp(argv[i], "--help")      == 0) { printHelp(argv[0]); return 0; }
        else if (strcmp(argv[i], "--port")      == 0 && i + 1 < argc)
            portOverride = atoi(argv[++i]);
    }

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    HidManager manager;
    if (probe) { manager.probe(); return 0; }

    // Load config (or use compiled-in defaults).
    Sc2Config cfg = noConfig ? Sc2Config{} : Sc2Config::load();

    // CLI overrides beat config file.
    if (portOverride   > 0) cfg.port   = static_cast<uint16_t>(portOverride);
    if (exposeOverride >= 0) cfg.expose = (exposeOverride == 1);

    DsuServer server(cfg.port, cfg.expose);
    server.start();

    fprintf(stderr, "sc2gyrodsu: running. Waiting for emulator on 127.0.0.1:%u\n", cfg.port);
    fprintf(stderr, "sc2gyrodsu: config = %s\n", Sc2Config::configPath().c_str());

    bool slotUsed[4] = {};
    std::vector<std::unique_ptr<SlotReader>> readers;

    while (!gShutdown) {
        // Reap finished readers and free their slots.
        for (auto it = readers.begin(); it != readers.end(); ) {
            auto& r = **it;
            if (r.done) {
                slotUsed[r.slot->dsuSlot] = false;
                if (r.thread.joinable()) r.thread.join();
                it = readers.erase(it);
            } else ++it;
        }

        // Scan for newly connected SC2 devices.
        std::vector<std::unique_ptr<HidSlot>> newSlots;
        manager.scan(newSlots, slotUsed, cfg.gyro, cfg.accel, cfg.gyroBias);

        for (auto& slot : newSlots) {
            HidSlot* slotPtr = slot.get();
            auto reader = std::make_unique<SlotReader>();
            reader->slot = std::move(slot);
            std::atomic<bool>& doneRef = reader->done;
            reader->thread = std::thread([slotPtr, &server, &doneRef]() {
                slotReaderThread(slotPtr, &server,
                    const_cast<std::atomic<bool>&>(doneRef));
            });
            readers.push_back(std::move(reader));
        }

        std::this_thread::sleep_for(500ms);
    }

    server.stop();
    for (auto& r : readers) {
        r->done = true;
        if (r->thread.joinable()) r->thread.join();
    }
    return 0;
}
