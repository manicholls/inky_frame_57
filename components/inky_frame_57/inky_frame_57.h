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
                                          spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ> {
 protected:
  uint8_t *buffer_;
  bool initialised_ = false;

  const int SR_CLK_PIN = 8;
  const int SR_LATCH_PIN = 9;
  const int SR_DATA_OUT_PIN = 11;
  const int HOLD_VSYS_EN_PIN = 2; 

  // Fast, blocking shift register write
  void write_sr(bool rst, bool cs, bool dc) {
    // 0xF8 = 11111000 in binary. 
    // This forces bits 3-7 HIGH, turning OFF all the Active-LOW LEDs.
    uint8_t state = 0xF8; 
    
    if (rst) state |= (1 << 0);
    if (cs)  state |= (1 << 1);
    if (dc)  state |= (1 << 2);

    digitalWrite(SR_LATCH_PIN, LOW);
    
    // Shift out MSB first (Bit 7 down to 0)
    for (int i = 7; i >= 0; i--) {
      digitalWrite(SR_CLK_PIN, LOW);
      digitalWrite(SR_DATA_OUT_PIN, (state & (1 << i)) ? HIGH : LOW);
      digitalWrite(SR_CLK_PIN, HIGH);
    }
    
    digitalWrite(SR_LATCH_PIN, HIGH);
  }

  void command(uint8_t command) {
    this->enable(); // Locks SPI bus
    write_sr(true, false, false); // CS = LOW, DC = LOW
    this->write_byte(command);
    write_sr(true, true, true);   // CS = HIGH, DC = HIGH
    this->disable(); // Unlocks SPI bus
  }

  void data(uint8_t data) {
    this->enable();
    write_sr(true, false, true); // CS = LOW, DC = HIGH
    this->write_byte(data);
    write_sr(true, true, true);  // CS = HIGH, DC = HIGH
    this->disable();
  }

  void wait_busy(const char* step, uint32_t delay_ms) {
    ESP_LOGI(TAG, "Waiting %d ms for %s...", delay_ms, step);
    uint32_t start = millis();
    while (millis() - start < delay_ms) {
      delay(50);
      App.feed_wdt(); 
      yield(); 
    }
    ESP_LOGI(TAG, "%s complete!", step);
  }

  void init_display() {
    ESP_LOGI(TAG, "Starting Hardware Reset...");
    write_sr(false, true, true); // Pull RESET LOW
    delay(20);
    write_sr(true, true, true);  // Release RESET HIGH
    delay(200);

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
    
    ESP_LOGI(TAG, "Display Initialization Complete!");
  }

 public:
  void setup() override {
    pinMode(HOLD_VSYS_EN_PIN, OUTPUT);
    digitalWrite(HOLD_VSYS_EN_PIN, HIGH);
      
    pinMode(SR_CLK_PIN, OUTPUT);
    pinMode(SR_LATCH_PIN, OUTPUT);
    pinMode(SR_DATA_OUT_PIN, OUTPUT);

    // Initialize shift register immediately so LEDs start OFF
    write_sr(true, true, true); 

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
    
    init_display();
    
    ESP_LOGI(TAG, "Transmitting buffer to display...");
    
    this->enable(); // Lock SPI
    
    // Send Command 0x10
    write_sr(true, false, false); // CS LOW, DC LOW
    this->write_byte(0x10);
    
    // Switch to Data mode but KEEP CS LOW!
    write_sr(true, false, true); // CS LOW, DC HIGH
    
    size_t remaining = 600 * 448 / 2;
    uint8_t *ptr = buffer_;
    while (remaining > 0) {
      size_t chunk = remaining > 4096 ? 4096 : remaining;
      this->write_array(ptr, chunk);
      ptr += chunk;
      remaining -= chunk;
      
      // Feed watchdog, but don't yield so ESPHome can't interrupt us
      App.feed_wdt(); 
    }
    
    write_sr(true, true, true); // Transfer complete, release CS HIGH
    this->disable(); // Unlock SPI
    
    ESP_LOGI(TAG, "Powering ON E-Ink Panel...");
    command(0x04);
    wait_busy("Power ON", 1000);

    ESP_LOGI(TAG, "Commanding screen refresh...");
    command(0x12); 
    wait_busy("Screen Refresh", 35000); 

    ESP_LOGI(TAG, "Powering OFF E-Ink Panel...");
    command(0x02);
    wait_busy("Power OFF", 1000);

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
