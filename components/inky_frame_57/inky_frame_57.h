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
  bool initialised_ = false;

  const int CS_PIN = 17; 
  const int DC_PIN = 28; 
  const int RST_PIN = 27;
  const int HOLD_VSYS_EN_PIN = 2; 

  const int SR_CLK_PIN = 8;
  const int SR_LATCH_PIN = 9;
  const int SR_DATA_PIN = 10;

  void send_cmd(uint8_t command, std::initializer_list<uint8_t> args = {}) {
    this->enable(); 
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW); 
    this->write_byte(command);
    
    if (args.size() > 0) {
      digitalWrite(DC_PIN, HIGH);
      for (uint8_t arg : args) {
        this->write_byte(arg);
      }
    }
    digitalWrite(CS_PIN, HIGH);
    this->disable(); 
  }

  bool is_busy() {
    digitalWrite(SR_LATCH_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(SR_LATCH_PIN, HIGH);
    delayMicroseconds(5);

    uint8_t val = 0;
    for (int i = 0; i < 8; ++i) {
      if (digitalRead(SR_DATA_PIN) == HIGH) {
        val |= (1 << (7 - i));
      }
      digitalWrite(SR_CLK_PIN, HIGH);
      delayMicroseconds(1);
      digitalWrite(SR_CLK_PIN, LOW);
      delayMicroseconds(1);
    }
    return (val & 0x80) == 0; 
  }

  void wait_until_idle(const char* step) {
    ESP_LOGI(TAG, "Waiting for hardware: %s...", step);
    
    uint32_t start_grace = millis();
    while (millis() - start_grace < 50) {
      App.feed_wdt();
      yield();
    }
    
    while (is_busy()) {
      delay(10);
      App.feed_wdt();
      yield();
    }
    ESP_LOGI(TAG, "%s complete!", step);
  }

  void init_display() {
    ESP_LOGI(TAG, "Starting Hardware Reset...");
    digitalWrite(RST_PIN, LOW);
    uint32_t t1 = millis(); while(millis() - t1 < 20) { App.feed_wdt(); yield(); }
    digitalWrite(RST_PIN, HIGH);
    uint32_t t2 = millis(); while(millis() - t2 < 200) { App.feed_wdt(); yield(); }

    wait_until_idle("Reset Stabilization");

    ESP_LOGI(TAG, "Sending initialization sequence...");
    send_cmd(0x00, {0xEF, 0x08}); // 0xEF confirmed to power the matrix correctly
    send_cmd(0x01, {0x37, 0x00, 0x23, 0x23}); 
    send_cmd(0x03, {0x00}); 
    send_cmd(0x06, {0xC7, 0xC7, 0x1D}); 
    send_cmd(0x30, {0x3C}); 
    send_cmd(0x40, {0x00}); 
    send_cmd(0x50, {0x37}); 
    send_cmd(0x60, {0x22}); 
    send_cmd(0x61, {0x02, 0x58, 0x01, 0xC0}); // 600x448 Resolution
    send_cmd(0xE3, {0xAA}); 
    
    uint32_t t3 = millis(); while(millis() - t3 < 100) { App.feed_wdt(); yield(); }
    send_cmd(0x50, {0x37});
    
    ESP_LOGI(TAG, "Display Initialization Complete!");
  }

 public:
  void clear() override {
    if (this->buffer_ != nullptr) {
      memset(this->buffer_, 0x11, 600 * 448 / 2);
    }
  }

  void setup() override {
    pinMode(HOLD_VSYS_EN_PIN, OUTPUT);
    digitalWrite(HOLD_VSYS_EN_PIN, HIGH);
      
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);

    pinMode(DC_PIN, OUTPUT);
    pinMode(RST_PIN, OUTPUT);
    
    pinMode(SR_CLK_PIN, OUTPUT);
    pinMode(SR_LATCH_PIN, OUTPUT);
    pinMode(SR_DATA_PIN, INPUT);

    this->spi_setup();

    ESP_LOGI(TAG, "Allocating 134KB frame buffer...");
    this->init_internal_(600 * 448 / 2);
    
    if (this->buffer_ == nullptr) {
        ESP_LOGE(TAG, "FATAL: OUT OF MEMORY!");
        return; 
    }
    
    this->clear(); 
    ESP_LOGI(TAG, "Buffer allocated successfully.");
    
    this->initialised_ = true;
  }

  // CORRECTED: Restored update() to satisfy the compiler
  void update() override {
    if (!this->initialised_) return;

    ESP_LOGI(TAG, "Rendering ESPHome graphics...");
    
    // 1. ESPHome executes your YAML graphics here
    this->do_update_(); 
    
    // 2. DIAGNOSTIC CHECK: Injects a 50x50 Blue Square in the top-left corner.
    for (int y = 0; y < 50; y++) {
      for (int x = 0; x < 50; x++) {
        int idx = (y * 600 + x) / 2;
        if (x % 2 == 0) this->buffer_[idx] = (this->buffer_[idx] & 0x0F) | (3 << 4); // 3 = Blue
        else this->buffer_[idx] = (this->buffer_[idx] & 0xF0) | 3;
      }
    }
    
    init_display();
    
    ESP_LOGI(TAG, "Powering ON E-Ink Panel...");
    send_cmd(0x04);
    wait_until_idle("Power ON"); 

    ESP_LOGI(TAG, "Transmitting buffer to display...");
    
    this->enable(); 
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW); 
    this->write_byte(0x10);
    
    digitalWrite(DC_PIN, HIGH);
    
    size_t remaining = 600 * 448 / 2;
    uint8_t *ptr = this->buffer_;
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
    send_cmd(0x11);
    
    ESP_LOGI(TAG, "Commanding screen refresh...");
    send_cmd(0x12); 
    wait_until_idle("Screen Refresh"); 

    ESP_LOGI(TAG, "Powering OFF E-Ink Panel...");
    send_cmd(0x02);
    wait_until_idle("Power OFF");

    ESP_LOGI(TAG, "Update sequence complete.");
  }

  void draw_absolute_pixel_internal(int x, int y, Color color) override {
    if (x < 0 || x >= 600 || y < 0 || y >= 448 || this->buffer_ == nullptr) return;

    // Enhanced contrast matcher to catch dark anti-aliased font pixels
    uint8_t c = 1; // Default to White
    if (color.r < 150 && color.g < 150 && color.b < 150) c = 0;      // Black (including dark greys)
    else if (color.r > 150 && color.g < 100 && color.b < 100) c = 4; // Red
    else if (color.r < 100 && color.g > 150 && color.b < 100) c = 2; // Green
    else if (color.r < 100 && color.g < 100 && color.b > 150) c = 3; // Blue
    else if (color.r > 150 && color.g > 150 && color.b < 100) c = 5; // Yellow
    else if (color.r > 150 && color.g > 100 && color.b < 80) c = 6;  // Orange

    int idx = (y * 600 + x) / 2;
    if (x % 2 == 0) {
      this->buffer_[idx] = (this->buffer_[idx] & 0x0F) | (c << 4);
    } else {
      this->buffer_[idx] = (this->buffer_[idx] & 0xF0) | c;
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
