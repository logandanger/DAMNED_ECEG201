// I2S output on its own task, pinned to core 1. The callback is BeatEngine::render.
//
// TODO(TLV320DAC3100): the DAC driver is out of scope for now, so this outputs valid I2S and the
// DAC stays silent until these exist. All of it goes over I2C at 0x18 before audio starts:
//   - No MCLK is routed (README risk R5). Clock the codec from BCLK through its PLL:
//     CODEC_CLKIN = PLL_CLK, PLL_CLKIN = BCLK.
//   - BCLK = 2 x 16 x 44100 = 1.4112 MHz. A PLL ratio of 64 gives 90.3168 MHz = 2048 fs.
//     Candidate: P=1, R=2, J=32, D=0, then NDAC=8, MDAC=2, DOSR=128. [verify] against the datasheet
//     limits for PLL input, PLL output and DAC_MOD_CLK before use.
//   - Word length 16 bit, I2S mode, codec as clock slave (ESP32 drives BCLK and WS).
//   - The PLL needs BCLK running before it locks, so start I2S first, then program the codec.
//   - Power up the DAC channels, route to headphone and class-D outputs, set volumes, unmute.
//   - Class-D output power depends on the supply; see README section 4 before driving 3 W.
#include <atomic>

#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/esp32/board.hpp"
#include "hal/hal.hpp"

namespace hal {
namespace {

constexpr int kBlock = 128;  // core/config/beat_config.hpp kBlockFrames
constexpr int kSampleRate = 44100;
constexpr int kAudioCore = 1;
constexpr uint32_t kCpuHz = 240000000;

i2s_chan_handle_t g_tx;
AudioCallback g_cb;
void* g_user;
std::atomic<float> g_load{0.0f};
int16_t g_block[kBlock * 2];

void audio_task(void*) {
    constexpr float kCyclesPerBlock = float(kCpuHz) * kBlock / kSampleRate;
    for (;;) {
        const uint32_t t0 = esp_cpu_get_cycle_count();
        g_cb(g_block, kBlock, g_user);
        const float load = float(esp_cpu_get_cycle_count() - t0) / kCyclesPerBlock;
        g_load.store(g_load.load(std::memory_order_relaxed) * 0.95f + load * 0.05f, std::memory_order_relaxed);
        size_t written = 0;
        i2s_channel_write(g_tx, g_block, sizeof(g_block), &written, portMAX_DELAY);  // paced by DMA
    }
}

}  // namespace

void audio_start(AudioCallback cb, void* user) {
    g_cb = cb;
    g_user = user;

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 3;  // 3 x 2.9 ms of buffering
    chan.dma_frame_num = kBlock;
    chan.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &g_tx, nullptr));

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(board::kI2sBclk);
    std_cfg.gpio_cfg.ws = static_cast<gpio_num_t>(board::kI2sWs);
    std_cfg.gpio_cfg.dout = static_cast<gpio_num_t>(board::kI2sDout);
    std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(g_tx));

    xTaskCreatePinnedToCore(audio_task, "audio", 6144, nullptr, configMAX_PRIORITIES - 2, nullptr, kAudioCore);
}

float audio_load() { return g_load.load(std::memory_order_relaxed); }

}  // namespace hal
