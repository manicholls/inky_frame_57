#pragma once

#include "esphome.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/log.h"
#include <Arduino.h>

namespace esphome {
namespace inky_frame_57 {

static const char *const TAG = "inky_frame_57";

class InkyFrame57 : public display::DisplayBuffer,
                     public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                            spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_4MHZ> {
 protected:
  uint8_t *buffer_;
  bool initialised_ = false;

  const int CS_PIN = 17;
  const int DC_PIN = 28;
  const int RST_PIN = 27;
  const int HOLD_VSYS_EN_PIN = 2;

  // FIXED: command + all of its data bytes now share a single CS-low window.
  // Toggling CS between each byte (the previous behaviour) breaks the panel's
  // controller state machine, which expects one continuous transaction per
  // command+parameters write.
  void command(uint8_t cmd, std::initializer_list<uint8_t> data_bytes = {}) {
    this->enable();
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW);
    this->write_byte(cmd);
    if (data_bytes.size() > 0) {
      digitalWrite(DC_PIN, HIGH);
      for (uint8_t b : data_bytes) {
        this->write_byte(b);
      }
    }
    digitalWrite(CS_PIN, HIGH);
    this->disable();
  }

  void wait_busy(const char* step, uint32_t delay_ms) {
    ESP_LOGI(TAG, "Waiting %d ms for %s...", delay_ms, step);
    uint32_t start = millis();
    while (millis() - start < delay_ms) {
      delay(10);
      App.feed_wdt();
      yield();
    }
    ESP_LOGI(TAG, "%s complete!", step);
  }

  void init_display() {
    ESP_LOGI(TAG, "Starting Hardware Reset...");
    digitalWrite(RST_PIN, LOW);
    wait_busy("Reset LOW pulse", 20);
    digitalWrite(RST_PIN, HIGH);
    wait_busy("Reset HIGH stabilization", 200);

    ESP_LOGI(TAG, "Sending initialization sequence...");
    command(0x00, {0xEF, 0x08});
    command(0x01, {0x37, 0x00, 0x23, 0x23});
    command(0x03, {0x00});
    command(0x06, {0xC7, 0xC7, 0x1D});
    command(0x30, {0x3C});
    command(0x40, {0x00});
    command(0x50, {0x37});
    command(0x60, {0x22});
    command(0x61, {0x02, 0x58, 0x01, 0xC0});
    command(0xE3, {0xAA});
    ESP_LOGI(TAG, "Display Initialization Complete!");
  }

  // Maps an 8-bit RGB Color to the panel's 3-bit palette index.
  uint8_t color_to_index_(Color color) {
    uint8_t c = 1; // default WHITE
    if (color.r < 50 && color.g < 50 && color.b < 50) c = 0;        // BLACK
    else if (color.r > 200 && color.g > 200 && color.b > 200) c = 1; // WHITE
    else if (color.r < 100 && color.g > 150 && color.b < 100) c = 2; // GREEN
    else if (color.r < 100 && color.g < 100 && color.b > 150) c = 3; // BLUE
    else if (color.r > 150 && color.g < 100 && color.b < 100) c = 4; // RED
    else if (color.r > 200 && color.g > 200 && color.b < 100) c = 5; // YELLOW
    else if (color.r > 200 && color.g > 100 && color.b < 50) c = 6;  // ORANGE
    return c;
  }

 public:
  void setup() override {
    pinMode(HOLD_VSYS_EN_PIN, OUTPUT);
    digitalWrite(HOLD_VSYS_EN_PIN, HIGH);

    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    pinMode(DC_PIN, OUTPUT);
    pinMode(RST_PIN, OUTPUT);

    this->spi_setup();

    this->set_timeout(5000, [this]() {
      ESP_LOGI(TAG, "Attempting to allocate 134KB frame buffer...");
      buffer_ = new (std::nothrow) uint8_t[600 * 448 / 2];
      if (buffer_ == nullptr) {
        ESP_LOGE(TAG, "FATAL: OUT OF MEMORY!");
        return;
      }
      memset(buffer_, 0x11, 600 * 448 / 2);
      ESP_LOGI(TAG, "Buffer allocated successfully.");
      initialised_ = true;
    });
  }

  void update() override {
    if (!initialised_ || buffer_ == nullptr) return;

    ESP_LOGI(TAG, "Rendering ESPHome graphics...");
    this->do_update_();

    // Debug heartbeat block: alternating red/white square so it's obvious
    // a fresh refresh actually happened. Comment out once you've confirmed
    // real content is drawing correctly.
    static bool heartbeat_toggle = false;
    heartbeat_toggle = !heartbeat_toggle;
    uint8_t hb_color = heartbeat_toggle ? 4 : 1; // 4 = Red, 1 = White
    for (int y = 0; y < 50; y++) {
      for (int x = 0; x < 50; x++) {
        int idx = (y * 600 + x) / 2;
        if (x % 2 == 0) buffer_[idx] = (buffer_[idx] & 0x0F) | (hb_color << 4);
        else buffer_[idx] = (buffer_[idx] & 0xF0) | hb_color;
      }
    }

    init_display();

    ESP_LOGI(TAG, "Powering ON E-Ink Panel...");
    command(0x04); // PON
    wait_busy("Power ON", 2000);

    ESP_LOGI(TAG, "Transmitting buffer to display...");
    this->enable();
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW);
    this->write_byte(0x10); // DTM1
    digitalWrite(DC_PIN, HIGH);
    size_t remaining = 600 * 448 / 2;
    uint8_t *ptr = buffer_;
    while (remaining > 0) {
      size_t chunk = remaining > 4096 ? 4096 : remaining;
      this->write_array(ptr, chunk);
      ptr += chunk;
      remaining -= chunk;
      App.feed_wdt();
    }
    digitalWrite(CS_PIN, HIGH);
    this->disable();

    ESP_LOGI(TAG, "Data Stop command...");
    command(0x11); // DSP

    ESP_LOGI(TAG, "Commanding screen refresh...");
    command(0x12); // DRF
    wait_busy("Screen Refresh", 35000);

    ESP_LOGI(TAG, "Powering OFF E-Ink Panel...");
    command(0x02); // POF
    wait_busy("Power OFF", 1000);

    ESP_LOGI(TAG, "Update sequence complete.");
  }

  // FIXED: now honors the actual requested fill color instead of always
  // forcing white. ESPHome/your lambda's intent (e.g. it.fill(Color::BLACK))
  // is respected.
  void fill(Color color) override {
    if (buffer_ == nullptr) return;
    uint8_t c = color_to_index_(color);
    uint8_t packed = (c << 4) | c;
    memset(buffer_, packed, 600 * 448 / 2);
  }

  void draw_absolute_pixel_internal(int x, int y, Color color) override {
    if (buffer_ == nullptr) return;
    if (x < 0 || x >= 600 || y < 0 || y >= 448) return;

    uint8_t c = color_to_index_(color);

    int idx = (y * 600 + x) / 2;
    if (x % 2 == 0) {
      buffer_[idx] = (buffer_[idx] & 0x0F) | (c << 4);
    } else {
      buffer_[idx] = (buffer_[idx] & 0xF0) | c;
    }
  }

  int get_width_internal() override { return 600; }
  int get_height_internal() override { return 448; }

  display::DisplayType get_display_type() override {
    return display::DisplayType::DISPLAY_TYPE_COLOR;
  }
};

}  // namespace inky_frame_57
}  // namespace esphome
