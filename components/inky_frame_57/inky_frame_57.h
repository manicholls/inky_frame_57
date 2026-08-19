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

  void command(uint8_t command) {
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW); 
    this->write_byte(command); // Locks & unlocks internally safely
    digitalWrite(CS_PIN, HIGH);
  }

  void data(uint8_t data) {
    digitalWrite(DC_PIN, HIGH);
    digitalWrite(CS_PIN, LOW);
    this->write_byte(data); 
    digitalWrite(CS_PIN, HIGH);
  }

  // DUMB, BULLETPROOF WAIT TIMER
  void wait_busy(const char* step) {
    ESP_LOGI(TAG, "Executing %s...", step);
    
    int delay_ms = 200;
    if (strcmp(step, "Screen Refresh") == 0) {
      delay_ms = 35000; // Force a 35 second wait
    } else if (strcmp(step, "Power ON") == 0) {
      delay_ms = 1000; 
    } else if (strcmp(step, "Power OFF") == 0) {
      delay_ms = 1000; 
    } else if (strcmp(step, "Hardware Reset") == 0) {
      delay_ms = 500; 
    }

    uint32_t start = millis();
    while (millis() - start < delay_ms) {
      delay(50);
      App.feed_wdt(); // Keeps ESPHome alive while waiting
      yield(); 
    }
    ESP_LOGI(TAG, "%s complete!", step);
  }

  void init_display() {
    ESP_LOGI(TAG, "Starting Hardware Reset...");
    digitalWrite(RST_PIN, LOW);
    delay(100);
    digitalWrite(RST_PIN, HIGH);
    wait_busy("Hardware Reset");

    ESP_LOGI(TAG, "Sending initialization sequence...");
    command(0x01); data(0x37); data(0x00); data(0x23); data(0x23);
    command(0x00); data(0xEF); data(0x08);
    command(0x03); data(0x00);
    command(0x06); data(0xC7); data(0xC7); data(0x1D);
    command(0x30); data(0x3C);
    command(0x41); data(0x00);
    command(0x50); data(0x37);
    command(0x60); data(0x22);
    command(0x61); data(0x02); data(0x58); data(0x01); data(0xC0);
    command(0xE3); data(0xAA);
    
    initialised_ = true;
    ESP_LOGI(TAG, "Display Initialization Complete!");
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

        this->init_display();
        this->update(); 
    });
  }

  void update() override {
    if (!initialised_) return;

    ESP_LOGI(TAG, "Rendering ESPHome graphics...");
    this->do_update_();
    
    // 1. Transmit Image Data BEFORE Powering ON
    ESP_LOGI(TAG, "Transmitting buffer to display...");
    command(0x10); 
    
    digitalWrite(DC_PIN, HIGH);
    digitalWrite(CS_PIN, LOW); // Lock CS LOW manually
    
    // Brute-force byte-by-byte transfer bypasses all DMA and Chunking bugs.
    // It's slower, but 100% mathematically reliable.
    for (size_t i = 0; i < 600 * 448 / 2; i++) {
      this->write_byte(buffer_[i]);
      if (i % 4096 == 0) {
        App.feed_wdt();
      }
    }
    
    digitalWrite(CS_PIN, HIGH); // Release CS manually
    
    // 2. Power ON
    ESP_LOGI(TAG, "Powering ON E-Ink Panel...");
    command(0x04);
    wait_busy("Power ON");

    // 3. Screen Refresh
    ESP_LOGI(TAG, "Commanding screen refresh...");
    command(0x12); 
    wait_busy("Screen Refresh");

    // 4. Power OFF (Burn-in protection)
    ESP_LOGI(TAG, "Powering OFF E-Ink Panel...");
    command(0x02);
    wait_busy("Power OFF");

    ESP_LOGI(TAG, "Update sequence complete.");
  }

  void fill(Color color) override {
    uint8_t c = 1; 
    if (color.r < 50 && color.g < 50 && color.b < 50) c = 0; 
    else if (color.r > 200 && color.g > 200 && color.b > 200) c = 1; 
    else if (color.r < 100 && color.g > 150 && color.b < 100) c = 2; 
    else if (color.r < 100 && color.g < 100 && color.b > 150) c = 3; 
    else if (color.r > 150 && color.g < 100 && color.b < 100) c = 4; 
    else if (color.r > 200 && color.g > 200 && color.b < 100) c = 5; 
    else if (color.r > 200 && color.g > 100 && color.b < 50) c = 6; 

    uint8_t packed = (c << 4) | c;
    memset(buffer_, packed, 600 * 448 / 2);
  }

  void draw_absolute_pixel_internal(int x, int y, Color color) override {
    if (x < 0 || x >= 600 || y < 0 || y >= 448) return;

    uint8_t c = 1; 
    if (color.r < 50 && color.g < 50 && color.b < 50) c = 0; 
    else if (color.r > 200 && color.g > 200 && color.b > 200) c = 1; 
    else if (color.r < 100 && color.g > 150 && color.b < 100) c = 2; 
    else if (color.r < 100 && color.g < 100 && color.b > 150) c = 3; 
    else if (color.r > 150 && color.g < 100 && color.b < 100) c = 4; 
    else if (color.r > 200 && color.g > 200 && color.b < 100) c = 5; 
    else if (color.r > 200 && color.g > 100 && color.b < 50) c = 6; 

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
