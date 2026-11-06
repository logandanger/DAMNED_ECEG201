// Host side of the mirror: owns a reader thread and the latest screen contents.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "core/mirror/mirror_proto.hpp"
#include "sim/serial_port.hpp"

namespace sim {

class MirrorClient : danger::mirror::Decoder::Sink {
public:
    enum class Source { Serial, Replay };

    MirrorClient();
    ~MirrorClient();

    void start(Source source, const std::string& name);
    bool copy_if_new(uint16_t* out);  // true if a complete new frame was copied
    bool connected() const { return connected_.load(); }
    uint32_t frames() const { return frames_.load(); }
    uint32_t errors() const { return decoder_.errors(); }

private:
    void on_hello(int width, int height) override;
    void on_rect(int x, int y, int w, int h, const uint16_t* px) override;
    void on_frame_end() override;
    void on_error() override;

    void run_serial();
    void run_replay();

    Source source_ = Source::Serial;
    std::string name_;
    std::thread thread_;
    std::atomic<bool> stop_{false}, connected_{false}, want_full_{true};
    std::atomic<uint32_t> frames_{0};

    std::mutex mutex_;
    uint16_t back_[320 * 240] = {};   // written by the reader
    uint16_t front_[320 * 240] = {};  // last complete frame
    bool fresh_ = false;
    danger::mirror::Decoder decoder_;
};

}  // namespace sim
