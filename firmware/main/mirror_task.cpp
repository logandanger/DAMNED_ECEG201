// Streams the screen over the USB-Serial-JTAG CDC port while a host keeps a session open.
// Lowest-priority task, core 0: it cannot delay audio (core 1) or input, and a slow or absent host
// only coalesces dirty tiles. Without a host the only cost is the memcpy into the shadow buffer.
#include "mirror_task.hpp"

#ifndef DANGER_MIRROR
#define DANGER_MIRROR 1
#endif

#if DANGER_MIRROR

#include <cstring>
#include <new>

#include "core/mirror/mirror_sender.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/esp32/board.hpp"

namespace {

namespace mirror = danger::mirror;

constexpr int kMirrorCore = 0;
constexpr uint32_t kFramePeriodMs = 50;  // 20 frames/s at most

mirror::Sender* g_sender = nullptr;
alignas(mirror::Sender) uint8_t g_sender_mem[sizeof(mirror::Sender)];

size_t usb_write(const uint8_t* data, size_t size, void*) {
    size_t done = 0;
    while (done < size) {
        const int n = usb_serial_jtag_write_bytes(data + done, size - done, pdMS_TO_TICKS(20));
        if (n <= 0) break;
        done += static_cast<size_t>(n);
    }
    return done;
}

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Host commands are four bytes. A sliding window survives any misalignment.
void poll_commands(uint32_t& session_end) {
    static char window[4];
    uint8_t in[32];
    const int n = usb_serial_jtag_read_bytes(in, sizeof(in), 0);
    for (int i = 0; i < n; ++i) {
        std::memmove(window, window + 1, 3);
        window[3] = static_cast<char>(in[i]);
        const bool full = std::memcmp(window, mirror::kCmdFullFrame, 4) == 0;
        if (full || std::memcmp(window, mirror::kCmdKeepalive, 4) == 0) {
            if (now_ms() >= session_end) g_sender->mark_all();  // session was closed: resend everything
            session_end = now_ms() + mirror::kSessionTimeoutMs;
        }
        if (full) g_sender->mark_all();
    }
}

void mirror_task(void*) {
    uint32_t session_end = 0;
    for (;;) {
        poll_commands(session_end);
        if (now_ms() < session_end) g_sender->pump(usb_write, nullptr);
        vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
    }
}

}  // namespace

void mirror_on_flush(int x, int y, int w, int h, const uint16_t* px) {
    if (g_sender) g_sender->on_flush(x, y, w, h, px);
}

void mirror_start() {
    uint16_t* bands[mirror::kTilesY] = {};
    bool ok = true;
    for (auto& band : bands) {
        band = static_cast<uint16_t*>(heap_caps_calloc(mirror::kBandPixels, 2, MALLOC_CAP_8BIT));
        ok = ok && band;
    }
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 4096;
    if (!ok || usb_serial_jtag_driver_install(&cfg) != ESP_OK) {  // everything else keeps running
        for (auto* band : bands) heap_caps_free(band);
        ESP_LOGW("mirror", "disabled: %u B free, largest block %u B", unsigned(esp_get_free_heap_size()),
                 unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        return;
    }
    esp_log_level_set("*", ESP_LOG_WARN);  // logs share this port; the host skips them but they cost frames
    g_sender = new (g_sender_mem) mirror::Sender(bands);
    g_sender->mark_all();
    xTaskCreatePinnedToCore(mirror_task, "mirror", 14336, nullptr, 2, nullptr, kMirrorCore);
}

#else

void mirror_start() {}

#endif
