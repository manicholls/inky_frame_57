#pragma once

#include "esphome.h"
#include "esphome/components/spi/spi.h"
#include <Arduino.h>

namespace esphome {
namespace inky_frame_57 {

class InkyFrame57 : public PollingComponent, public display::DisplayBuffer,
                    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                          spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_10MHZ> {
 protected:
  uint8_t *buffer_;

  // CS_PIN is now managed entirely by ESPHome's SPI integration via this->enable() / this->disable()
  const int DC_PIN = 28;
  const int RST_PIN = 27;
  const int SR_CLK_PIN = 8;
  const int SR_LATCH_PIN = 9;
  const int SR_DATA_PIN = 10;

  void command(uint8_t command) {
    digitalWrite(DC_PIN, LOW);
    this->enable(); // Pulls CS LOW natively
    this->transfer_byte(command);
    this->disable(); // Pulls CS HIGH natively
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
    // Shift out the 8 bits (Buttons A-E + Busy flag)
    for (int i = 0; i < 8; i++) {
      if (digitalRead(SR_DATA_PIN)) {
        state |= (1 << i);
      }
      digitalWrite(SR_CLK_PIN, HIGH);
      delayMicroseconds(1);
      digitalWrite(SR_CLK_PIN, LOW);
      delayMicroseconds(1);
    }
    // Bit 7 is the Eink Busy flag. UC8159 is usually LOW when busy.
    return (state & 0x80) == 0; 
  }

  void wait_busy() {
    while (is_busy()) {
      delay(50);
      yield(); // Prevent watchdog timeouts during 30s display refresh
    }
  }

 public:
  // Update every 60 seconds to prevent hardware damage to 7-color e-ink
  InkyFrame57() : PollingComponent(60000) {} 

  void setup() override {
    pinMode(DC_PIN, OUTPUT);
    pinMode(RST_PIN, OUTPUT);
    pinMode(SR_CLK_PIN, OUTPUT);
    pinMode(SR_LATCH_PIN, OUTPUT);
    pinMode(SR_DATA_PIN, INPUT);

    this->spi_setup();

    // Allocate frame buffer (134.4 KB)
    buffer_ = new uint8_t[600 * 448 / 2];
    memset(buffer_, 0x11, 600 * 448 / 2); // Set initial buffer to all white

    // Hardware Reset
    digitalWrite(RST_PIN, LOW);
    delay(20);
    digitalWrite(RST_PIN, HIGH);
    delay(20);

    wait_busy();

    // Pimoroni/UC8159 Initialization sequence
    command(0x01); // Power Setting
    data(0x37);
    data(0x00);
    data(0x23);
    data(0x23);

    command(0x00); // Panel Setting
    data(0xEF);
    data(0x08);

    command(0x03); // Power off sequence setting
    data(0x00);

    command(0x06); // Booster soft start
    data(0xC7);
    data(0xC7);
    data(0x1D);

    command(0x30); // PLL control
    data(0x3C);

    command(0x41); // Temperature sensor
    data(0x00);

    command(0x50); // Vcom and data interval
    data(0x37);

    command(0x60); // TCON
    data(0x22);

    command(0x61); // Resolution setting (600x448)
    data(0x02);
    data(0x58);
    data(0x01);
    data(0xC0);

    command(0xE3); // PWS
    data(0xAA);

    command(0x04); // Power ON
    wait_busy();
  }

  void update() override {
    // 1. Ask ESPHome to calculate graphics
    this->do_update_();
    
    // 2. Start Data transmission
    command(0x10); 
    digitalWrite(DC_PIN, HIGH);
    this->enable();
    this->write_array(buffer_, 600 * 448 / 2);
    this->disable();
    
    // 3. Command Display Refresh
    command(0x12); 
    wait_busy();
  }

  void draw_absolute_pixel_internal(int x, int y, Color color) override {
    if (x < 0 || x >= 600 || y < 0 || y >= 448) return;

    // Nearest-color approximation mapping logic for the UC8159 7-Color palette
    uint8_t c = 1; // Default white
    if (color.r < 50 && color.g < 50 && color.b < 50) c = 0; // Black
    else if (color.r > 200 && color.g > 200 && color.b > 200) c = 1; // White
    else if (color.r < 100 && color.g > 150 && color.b < 100) c = 2; // Green
    else if (color.r < 100 && color.g < 100 && color.b > 150) c = 3; // Blue
    else if (color.r > 150 && color.g < 100 && color.b < 100) c = 4; // Red
    else if (color.r > 200 && color.g > 200 && color.b < 100) c = 5; // Yellow
    else if (color.r > 200 && color.g > 100 && color.b < 50) c = 6; // Orange

    // Pack into the 4-bit per pixel buffer
    int idx = (y * 600 + x) / 2;
    if (x % 2 == 0) {
      buffer_[idx] = (buffer_[idx] & 0x0F) | (c << 4);
    } else {
      buffer_[idx] = (buffer_[idx] & 0xF0) | c;
    }
  }

  int get_width_internal() override { return 600; }
  int get_height_internal() override { return 448; }
};

}  // namespace inky_frame_57
}  // namespace esphome
