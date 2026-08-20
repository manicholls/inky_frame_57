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

  const int SR_CLK_PIN = 8;
  const int SR_LATCH_PIN = 9;
  const int SR_DATA_PIN = 10;

  // THE FIX: Send a command and its arguments in a SINGLE continuous SPI transaction.
  // E-ink controllers reject arguments if the CS pin pulses HIGH between bytes!
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
    send_cmd(0x00, {0xAF, 0x08}); 
    send_cmd(0x01, {0x37, 0x00, 0x23, 0x23}); 
    send_cmd(0x03, {0x00}); 
    send_cmd(0x06, {0xC7, 0xC7, 0x1D}); 
    send_cmd(0x30, {0x3C}); 
    send_cmd(0x40, {0x00}); 
    send_cmd(0x50, {0x37}); 
    send_cmd(0x60, {0x22}); 
    send_cmd(0x61, {0x02, 0x58, 0x01, 0xC0}); // 600x448 Resolution
    send_cmd(0xE3, {0xAA}); 
    
    // Pimoroni specific quirk: Wait 100ms then resend CDI (0x50)
    uint32_t t3 = millis(); while(millis() - t3 < 100) { App.feed_wdt(); yield(); }
    send_cmd(0x50, {0x37});
    
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
    
    pinMode(SR_CLK_PIN, OUTPUT);
    pinMode(SR_LATCH_PIN, OUTPUT);
    pinMode(SR_DATA_PIN, INPUT);

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
    
    static bool toggle = false;
    toggle = !toggle;
    uint8_t hb_color = toggle ? 0x44 : 0x22; 
    
    for (size_t i = 30000; i < 45000; i++) {
        buffer_[i] = hb_color;
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
    send_cmd(0x11);
    
    ESP_LOGI(TAG, "Commanding screen refresh...");
    send_cmd(0x12); 
    wait_until_idle("Screen Refresh"); 

    ESP_LOGI(TAG, "Powering OFF E-Ink Panel...");
    send_cmd(0x02);
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
