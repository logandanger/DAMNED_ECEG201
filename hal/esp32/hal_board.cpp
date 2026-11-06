// Display, LEDs and time.
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/esp32/board.hpp"
#include "hal/hal.hpp"

#ifndef DANGER_MIRROR
#define DANGER_MIRROR 1
#endif

namespace hal {
namespace {

esp_lcd_panel_handle_t g_panel;
SemaphoreHandle_t g_flush_done;
spi_device_handle_t g_leds;

bool on_color_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(g_flush_done, &woken);
    return woken == pdTRUE;
}

void display_init() {
    g_flush_done = xSemaphoreCreateBinary();

    gpio_set_direction(static_cast<gpio_num_t>(board::kLcdBacklight), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(board::kLcdBacklight), 0);

    spi_bus_config_t bus = {};
    bus.mosi_io_num = board::kSpiMosi;
    bus.sclk_io_num = board::kSpiSck;
    bus.miso_io_num = board::kSpiMiso;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 320 * 40 * 2;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = static_cast<gpio_num_t>(board::kLcdCs);
    io.dc_gpio_num = static_cast<gpio_num_t>(board::kLcdDc);
    io.spi_mode = 0;
    io.pclk_hz = board::kLcdClockHz;
    io.trans_queue_depth = 4;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.on_color_trans_done = on_color_done;
    esp_lcd_panel_io_handle_t handle = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io, &handle));

    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = static_cast<gpio_num_t>(board::kLcdRst);
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(handle, &panel, &g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));
    // [verify] orientation and inversion on the real panel.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(g_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(g_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_panel, true));
    gpio_set_level(static_cast<gpio_num_t>(board::kLcdBacklight), 1);
}

void leds_init() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = board::kLedData;
    bus.sclk_io_num = board::kLedClk;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 128;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = 4000000;
    dev.mode = 0;
    dev.spics_io_num = -1;
    dev.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &g_leds));
}

}  // namespace

void board_init() {
    display_init();
    leds_init();
}

void display_flush(int x, int y, int w, int h, uint16_t* px) {
#if DANGER_MIRROR
    mirror_on_flush(x, y, w, h, px);
#endif
    const int count = w * h;
    for (int i = 0; i < count; ++i) px[i] = static_cast<uint16_t>((px[i] << 8) | (px[i] >> 8));  // panel is big-endian
    esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, px);
    xSemaphoreTake(g_flush_done, portMAX_DELAY);
}

void leds_write(const uint8_t* rgb, int count) {
    // SK9822 frame: 32-bit start, 32 bits per LED (brightness, B, G, R), 32-bit end, plus clock tail.
    static uint8_t frame[4 + board::kLedCount * 4 + 8];
    const int n = count < board::kLedCount ? count : board::kLedCount;
    std::memset(frame, 0, sizeof(frame));
    for (int i = 0; i < board::kLedCount; ++i) {
        uint8_t* p = frame + 4 + i * 4;
        p[0] = 0xE0 | board::kLedGlobalBrightness;
        if (i < n) {
            p[1] = rgb[i * 3 + 2];
            p[2] = rgb[i * 3 + 1];
            p[3] = rgb[i * 3 + 0];
        }
    }
    spi_transaction_t t = {};
    t.length = sizeof(frame) * 8;
    t.tx_buffer = frame;
    spi_device_polling_transmit(g_leds, &t);
}

uint32_t millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

}  // namespace hal
