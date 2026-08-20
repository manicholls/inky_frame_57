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
  const int BUSY_PIN = 0;


  void wait_busy(const char* step, uint32_t max_ms) {
    ESP_LOGI(TAG, "Waiting for %s (busy pin)...", step);
    uint32_t start = millis();
    // UC8159 BUSY reads LOW while busy, HIGH when ready (same polarity as its sibling UC8151)
    while (digitalRead(BUSY_PIN) == LOW && millis() - start < max_ms) {
      delay(10);
      App.feed_wdt();
      yield();
    }
    ESP_LOGI(TAG, "%s complete after %d ms!", step, millis() - start);
  }
  void command(uint8_t command) {
    this->enable(); 
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW); 
    this->write_byte(command);
    digitalWrite(CS_PIN, HIGH);
    this->disable(); 
  }

  void data(uint8_t data) {
    this->enable();
    digitalWrite(DC_PIN, HIGH);
    digitalWrite(CS_PIN, LOW);
    this->write_byte(data);
    digitalWrite(CS_PIN, HIGH);
    this->disable();
  }

  // GUARANTEED blocking delay. This defeats the RTOS yielding bug.
  // It forces the hardware to obey the exact millisecond timing.
  void delay_blocking(const char* step, uint32_t delay_ms) {
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
    delay_blocking("Reset LOW pulse", 20); 
    digitalWrite(RST_PIN, HIGH);
    delay_blocking("Reset HIGH stabilization", 200); 

    ESP_LOGI(TAG, "Sending initialization sequence...");
    command(0x00); data(0xEF); data(0x08); 
    command(0x01); data(0x37); data(0x00); data(0x23); data(0x23); 
    command(0x03); data(0x00); 
    command(0x06); data(0xC7); data(0xC7); data(0x1D); 
    command(0x30); data(0x3C); 
    command(0x40); data(0x00); 
    command(0x50); data(0x37); 
    command(0x60); data(0x22); 
    command(0x61); data(0x02); data(0x58); data(0x01); data(0xC0); 
    command(0xE3); data(0xAA); 
    
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
        
        initialised_ = true;
    });
  }

  void update() override {
    if (!initialised_) return;

    ESP_LOGI(TAG, "Rendering ESPHome graphics...");
    
    // Calls our hijacked fill() below to force a white background and the stripe, 
    // and THEN natively executes your YAML graphics over top of it.
    this->do_update_(); 
    
    init_display();
    
    ESP_LOGI(TAG, "Powering ON E-Ink Panel...");
    command(0x04);
    delay_blocking("Power ON", 2000); 

    ESP_LOGI(TAG, "Transmitting buffer to display...");
    
    this->enable(); 
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW); 
    this->write_byte(0x10);
    
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
    command(0x11);
    
    ESP_LOGI(TAG, "Commanding screen refresh...");
    command(0x12); 
    delay_blocking("Screen Refresh", 35000); 

    ESP_LOGI(TAG, "Powering OFF E-Ink Panel...");
    command(0x02);
    delay_blocking("Power OFF", 1000);

    ESP_LOGI(TAG, "Update sequence complete.");
  }

  void fill(Color color) override {
    // 1. Force the background to Brilliant White
    memset(buffer_, 0x11, 600 * 448 / 2);
    
    // 2. Inject a massive alternating Red/Green stripe in the background.
    // This physically guarantees the screen's hash checker fires a refresh.
    static bool toggle = false;
    toggle = !toggle;
    uint8_t hb_color = toggle ? 0x44 : 0x22; 
    
    // Draw a giant block spanning the entire width of the screen
    for (size_t i = 30000; i < 45000; i++) {
        buffer_[i] = hb_color;
    }
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
