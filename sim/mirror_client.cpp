#include "sim/mirror_client.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace sim {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace mirror = danger::mirror;

}  // namespace

MirrorClient::MirrorClient() : decoder_(this) {}

MirrorClient::~MirrorClient() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

void MirrorClient::start(Source source, const std::string& name) {
    source_ = source;
    name_ = name;
    thread_ = std::thread([this] {
        if (source_ == Source::Serial) run_serial();
        else run_replay();
    });
}

void MirrorClient::on_hello(int width, int height) {
    if (width != 320 || height != 240) std::fprintf(stderr, "mirror: device reports %dx%d\n", width, height);
}

void MirrorClient::on_rect(int x, int y, int w, int h, const uint16_t* px) {
    if (x < 0 || y < 0 || x + w > 320 || y + h > 240) return;
    for (int r = 0; r < h; ++r) std::memcpy(&back_[(y + r) * 320 + x], px + r * w, size_t(w) * 2);
}

void MirrorClient::on_frame_end() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::memcpy(front_, back_, sizeof(front_));
    fresh_ = true;
    ++frames_;
}

void MirrorClient::on_error() { want_full_ = true; }

bool MirrorClient::copy_if_new(uint16_t* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fresh_) return false;
    std::memcpy(out, front_, sizeof(front_));
    fresh_ = false;
    return true;
}

void MirrorClient::run_serial() {
    static uint8_t buf[4096];
    while (!stop_) {
        SerialPort port;
        if (!port.open(name_.c_str())) {
            connected_ = false;
            std::this_thread::sleep_for(500ms);  // the port may appear later
            continue;
        }
        connected_ = true;
        want_full_ = true;
        auto last_send = Clock::now() - 1h;
        while (!stop_) {
            const auto now = Clock::now();
            if (want_full_.exchange(false)) {
                if (!port.write(reinterpret_cast<const uint8_t*>(mirror::kCmdFullFrame), 4)) break;
                last_send = now;
            } else if (now - last_send > 1s) {
                if (!port.write(reinterpret_cast<const uint8_t*>(mirror::kCmdKeepalive), 4)) break;
                last_send = now;
            }
            const int n = port.read(buf, sizeof(buf));
            if (n < 0) break;
            if (n > 0) decoder_.feed(buf, size_t(n));
        }
        connected_ = false;
    }
}

void MirrorClient::run_replay() {
    FILE* f = std::fopen(name_.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "mirror: cannot open %s\n", name_.c_str());
        return;
    }
    connected_ = true;
    static uint8_t buf[2048];
    size_t n;
    while (!stop_ && (n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        decoder_.feed(buf, n);
        std::this_thread::sleep_for(2ms);
    }
    std::fclose(f);
}

}  // namespace sim
