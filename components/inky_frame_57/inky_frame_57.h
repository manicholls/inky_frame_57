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
  const int BUSY_PIN = 26; 
  const int HOLD_VSYS_EN_PIN = 2; 

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

  // GUARANTEED blocking delay that ignores the broken background yielding
  void delay_blocking(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
      App.feed_wdt();
      yield();
    }
  }

  void wait_until_idle(const char* step) {
    ESP_LOGI(TAG, "Waiting for hardware: %s...", step);
    
    // RACE CONDITION FIX: The e-ink controller takes a few milliseconds 
    // to process the SPI command and physically pull the BUSY pin LOW. 
    // We MUST wait 50ms before checking the pin, otherwise we read HIGH 
    // instantly and accidentally abort the wait!
    delay_blocking(50); 
    
    while (digitalRead(BUSY_PIN) == LOW) {
      delay_blocking(50);
    }
    ESP_LOGI(TAG, "%s complete!", step);
  }

  void init_display() {
    ESP_LOGI(TAG, "Starting Hardware Reset...");
    digitalWrite(RST_PIN, LOW);
    
    // Using our guaranteed delay so the reset pulse is actually sent
    delay_blocking(20); 
    digitalWrite(RST_PIN, HIGH);
    delay_blocking(200); 

    wait_until_idle("Reset Stabilization");

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
    pinMode(BUSY_PIN, INPUT);

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
    this->do_update_(); 
    
    // Massive Alternating Stripe to guarantee a visual change on the panel
    static bool toggle = false;
    toggle = !toggle;
    uint8_t hb_color = toggle ? 0x44 : 0x22; 

    for (size_t i = 30000; i < 45000; i++) {
        buffer_[i] = hb_color;
    }
    
    init_display();
    
    ESP_LOGI(TAG, "Powering ON E-Ink Panel...");
    command(0x04);
    wait_until_idle("Power ON"); 

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
    wait_until_idle("Screen Refresh"); 

    ESP_LOGI(TAG, "Powering OFF E-Ink Panel...");
    command(0x02);
    wait_until_idle("Power OFF");

    ESP_LOGI(TAG, "Update sequence complete.");
  }

  void fill(Color color) override {
    memset(buffer_, 0x11, 600 * 448 / 2);
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
