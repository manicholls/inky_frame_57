#pragma once

#include "esphome.h"
#include "esphome/components/spi/spi.h"
#include <Arduino.h>

namespace esphome {
namespace inky_frame_57 {

class InkyFrame57 : public display::DisplayBuffer,
                    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                          spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_10MHZ> {
 protected:
  uint8_t *buffer_;

  const int DC_PIN = 28;
  const int RST_PIN = 27;
  const int SR_CLK_PIN = 8;
  const int SR_LATCH_PIN = 9;
  const int SR_DATA_PIN = 10;
  
  // Critical Pimoroni Power Pin
  const int HOLD_VSYS_EN_PIN = 2; 

  void command(uint8_t command) {
    digitalWrite(DC_PIN, LOW);
    this->enable();
    this->transfer_byte(command);
    this->disable();
  }

  void data(uint8_t data) {
    digitalWrite(DC_PIN, HIGH);
    this->enable();
    this->transfer_byte(data);
    this->disable();
  }

  bool is_busy() {
    digitalWrite(SR_LATCH_PIN, LOW);
    delayMicroseconds(1);
    digitalWrite(SR_LATCH_PIN, HIGH);
    delayMicroseconds(1);

    uint8_t state = 0;
    for (int i = 0; i < 8; i++) {
      if (digitalRead(SR_DATA_PIN)) {
        state |= (1 << i);
      }
      digitalWrite(SR_CLK_PIN, HIGH);
      delayMicroseconds(1);
      digitalWrite(SR_CLK_PIN, LOW);
      delayMicroseconds(1);
    }
    return (state & 0x80) == 0; 
  }

  void wait_busy() {
    while (is_busy()) {
      delay(50);
      App.feed_wdt(); // Tells ESPHome's watchdog that the system hasn't frozen
      yield(); 
    }
  }

 public:
  void setup() override {
    // 1. TURN ON SYSTEM POWER IMMEDIATELY
    pinMode(HOLD_VSYS_EN_PIN, OUTPUT);
    digitalWrite(HOLD_VSYS_EN_PIN, HIGH);
    delay(50); // Give the peripherals a moment to wake up
      
    pinMode(DC_PIN, OUTPUT);
    pinMode(RST_PIN, OUTPUT);
    pinMode(SR_CLK_PIN, OUTPUT);
    pinMode(SR_LATCH_PIN, OUTPUT);
    pinMode(SR_DATA_PIN, INPUT);

    this->spi_setup();

    buffer_ = new uint8_t[600 * 448 / 2];
    memset(buffer_, 0x11, 600 * 448 / 2); 

    digitalWrite(RST_PIN, LOW);
    delay(20);
    digitalWrite(RST_PIN, HIGH);
    delay(20);

    wait_busy();

    command(0x01); 
    data(0x37);
    data(0x00);
    data(0x23);
    data(0x23);

    command(0x00); 
    data(0xEF);
    data(0x08);

    command(0x03); 
    data(0x00);

    command(0x06); 
    data(0xC7);
    data(0xC7);
    data(0x1D);

    command(0x30); 
    data(0x3C);

    command(0x41); 
    data(0x00);

    command(0x50); 
    data(0x37);

    command(0x60); 
    data(0x22);

    command(0x61); 
    data(0x02);
    data(0x58);
    data(0x01);
    data(0xC0);

    command(0xE3); 
    data(0xAA);

    command(0x04); 
    wait_busy();
  }

  void update() override {
    this->do_update_();
    
    command(0x10); 
    digitalWrite(DC_PIN, HIGH);
    this->enable();
    this->write_array(buffer_, 600 * 448 / 2);
    this->disable();
    
    command(0x12); 
    wait_busy();
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
