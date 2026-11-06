// Input task: GPIO buttons, AW9523 keys, seesaw encoders, polled at 1 kHz. Runs on core 0.
// A missing breakout is skipped, so the board boots with any subset connected.
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/esp32/board.hpp"
#include "hal/hal.hpp"

namespace hal {
namespace {

constexpr int kInputCore = 0;
constexpr int kI2cTimeoutMs = 5;

// AW9523 registers.
constexpr uint8_t kAwInput0 = 0x00, kAwConfig0 = 0x04, kAwIntEnable0 = 0x06, kAwChipId = 0x10, kAwLedMode0 = 0x12,
                  kAwReset = 0x7F, kAwIdValue = 0x23;
// Mechanical keys bounce and the AW9523 has no debouncer. A change fires at once, then the key
// ignores further changes for this long. The six on-board buttons need none: the MC14490 handles them.
constexpr uint32_t kKeyLockoutMs = 10;

// Seesaw registers.
constexpr uint8_t kSsGpioBase = 0x01, kSsEncoderBase = 0x11, kSsDirClr = 0x03, kSsBulk = 0x04, kSsBulkSet = 0x05,
                  kSsPullEnSet = 0x0B, kSsEncDelta = 0x40;
constexpr uint32_t kSsSwitchMask = 1u << 24;
constexpr uint32_t kSsDelayUs = 250;  // the seesaw needs this between register select and read

struct ButtonPin { int gpio; Control control; bool down; };
ButtonPin g_buttons[] = {
    {board::kBtnUp, Control::Up, false},     {board::kBtnDown, Control::Down, false},
    {board::kBtnLeft, Control::Left, false}, {board::kBtnRight, Control::Right, false},
    {board::kBtnA, Control::A, false},       {board::kBtnB, Control::B, false},
};

ControlSink g_sink;
void* g_user;
i2c_master_bus_handle_t g_bus;
i2c_master_dev_handle_t g_keys;
i2c_master_dev_handle_t g_enc[2];
bool g_enc_down[2];
uint16_t g_key_state = 0;
uint32_t g_key_lockout[kKeyCount];

void emit(Control c, EventType t, int16_t delta = 0, uint8_t velocity = 0) {
    g_sink({c, t, delta, velocity}, g_user);
}

i2c_master_dev_handle_t add_device(uint8_t addr) {
    if (i2c_master_probe(g_bus, addr, kI2cTimeoutMs) != ESP_OK) return nullptr;
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = board::kI2cHz;
    i2c_master_dev_handle_t dev = nullptr;
    return i2c_master_bus_add_device(g_bus, &cfg, &dev) == ESP_OK ? dev : nullptr;
}

bool aw_write(uint8_t reg, uint8_t a, uint8_t b) {
    const uint8_t out[3] = {reg, a, b};
    return i2c_master_transmit(g_keys, out, 3, kI2cTimeoutMs) == ESP_OK;
}

void keys_init() {
    g_keys = add_device(board::kAw9523Addr);
    if (!g_keys) return;
    const uint8_t reset[2] = {kAwReset, 0};
    i2c_master_transmit(g_keys, reset, 2, kI2cTimeoutMs);
    uint8_t reg = kAwChipId, id = 0;
    if (i2c_master_transmit_receive(g_keys, &reg, 1, &id, 1, kI2cTimeoutMs) != ESP_OK || id != kAwIdValue) {
        g_keys = nullptr;
        return;
    }
    aw_write(kAwLedMode0, 0xFF, 0xFF);    // GPIO mode
    aw_write(kAwConfig0, 0xFF, 0xFF);     // 1 = input
    aw_write(kAwIntEnable0, 0xFF, 0xFF);  // 1 = disabled
}

void keys_poll(uint32_t now) {
    if (!g_keys) return;
    uint8_t reg = kAwInput0, in[2];
    if (i2c_master_transmit_receive(g_keys, &reg, 1, in, 2, kI2cTimeoutMs) != ESP_OK) return;
    // Active-low with external pull-ups: the AW9523 has no internal ones.
    const uint16_t pressed = static_cast<uint16_t>(~(in[0] | (in[1] << 8)));
    for (int k = 0; k < kKeyCount; ++k) {
        const bool now_down = pressed & (1u << board::kKeyPin[k]);
        const bool was_down = g_key_state & (1u << k);
        if (now_down == was_down || now < g_key_lockout[k]) continue;
        g_key_lockout[k] = now + kKeyLockoutMs;
        g_key_state ^= static_cast<uint16_t>(1u << k);
        emit(key_control(k), now_down ? EventType::Press : EventType::Release, 0, now_down ? kFixedKeyVelocity : 0);
    }
}

bool ss_write32(i2c_master_dev_handle_t dev, uint8_t base, uint8_t reg, uint32_t v) {
    const uint8_t out[6] = {base, reg, uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    return i2c_master_transmit(dev, out, 6, kI2cTimeoutMs) == ESP_OK;
}

bool ss_read32(i2c_master_dev_handle_t dev, uint8_t base, uint8_t reg, uint32_t& v) {
    const uint8_t sel[2] = {base, reg};
    if (i2c_master_transmit(dev, sel, 2, kI2cTimeoutMs) != ESP_OK) return false;
    esp_rom_delay_us(kSsDelayUs);
    uint8_t in[4];
    if (i2c_master_receive(dev, in, 4, kI2cTimeoutMs) != ESP_OK) return false;
    v = (uint32_t(in[0]) << 24) | (uint32_t(in[1]) << 16) | (uint32_t(in[2]) << 8) | in[3];
    return true;
}

void encoders_init() {
    for (int i = 0; i < 2; ++i) {
        g_enc[i] = add_device(board::kEncoderAddr[i]);
        if (!g_enc[i]) continue;
        ss_write32(g_enc[i], kSsGpioBase, kSsDirClr, kSsSwitchMask);
        ss_write32(g_enc[i], kSsGpioBase, kSsPullEnSet, kSsSwitchMask);
        ss_write32(g_enc[i], kSsGpioBase, kSsBulkSet, kSsSwitchMask);
    }
}

// One seesaw read per tick, in rotation, so a tick stays well under 1 ms: each register every 4 ms.
void encoders_poll(uint32_t tick) {
    const int i = tick & 1;
    if (!g_enc[i]) return;
    uint32_t v = 0;
    if (tick & 2) {
        if (!ss_read32(g_enc[i], kSsGpioBase, kSsBulk, v)) return;
        const bool down = !(v & kSsSwitchMask);
        if (down != g_enc_down[i]) {
            g_enc_down[i] = down;
            emit(i ? Control::Enc2 : Control::Enc1, down ? EventType::Press : EventType::Release);
        }
    } else {
        if (!ss_read32(g_enc[i], kSsEncoderBase, kSsEncDelta, v)) return;
        const int delta = static_cast<int32_t>(v) * board::kEncoderSign;
        if (delta != 0) emit(i ? Control::Enc2 : Control::Enc1, EventType::Turn, static_cast<int16_t>(delta));
    }
}

void buttons_poll() {
    for (ButtonPin& b : g_buttons) {
        const bool down = gpio_get_level(static_cast<gpio_num_t>(b.gpio)) == 0;  // active-low, no debounce
        if (down != b.down) {
            b.down = down;
            emit(b.control, down ? EventType::Press : EventType::Release);
        }
    }
}

void input_task(void*) {
    keys_init();
    encoders_init();
    TickType_t wake = xTaskGetTickCount();
    for (uint32_t tick = 0;; ++tick) {
        const uint32_t now = millis();
        buttons_poll();
        keys_poll(now);
        encoders_poll(tick);
        vTaskDelayUntil(&wake, 1);
    }
}

}  // namespace

void input_start(ControlSink sink, void* user) {
    g_sink = sink;
    g_user = user;

    gpio_config_t io = {};
    io.mode = GPIO_MODE_INPUT;
    for (const ButtonPin& b : g_buttons) io.pin_bit_mask |= 1ULL << b.gpio;
    ESP_ERROR_CHECK(gpio_config(&io));

    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = static_cast<gpio_num_t>(board::kI2cSda);
    bus.scl_io_num = static_cast<gpio_num_t>(board::kI2cScl);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &g_bus));

    xTaskCreatePinnedToCore(input_task, "input", 4096, nullptr, 10, nullptr, kInputCore);
}

}  // namespace hal
