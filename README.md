# ESPHome Inky Frame 5.7" (UC8159e) Component

A custom ESPHome display driver for the **Pimoroni Inky Frame 5.7"** 7-color e-ink display. This component utilizes the Raspberry Pi Pico W (RP2040) and properly maps the UC8159e controller to ESPHome's internal graphics engine, complete with physical button support via the onboard 74HC165 shift register.

## Features
*   **7-Color Support:** Maps standard ESPHome `Color(r, g, b)` commands to the panel's physical palette (Black, White, Red, Green, Blue, Yellow, Orange).
*   **Aggressive Anti-Aliasing Snapping:** Custom C++ color matching forces faint ESPHome font anti-aliasing to snap to pure black, preventing text from washing out or vanishing against white backgrounds.
*   **180-Degree Hardware Flip:** Corrects the physical panel's native bottom-up drawing sequence so standard `(0,0)` coordinates map perfectly to the Top-Left of the screen.
*   **Fast Memory Fill:** Includes a dedicated `memset` override to bypass ESPHome's default 268,000-iteration pixel loop, preventing lambda timeouts when clearing the canvas.

## Installation

Create a `custom_components` folder in your ESPHome configuration directory and copy the driver files into it. Your file structure should look like this:

```text
/config/esphome/
├── your_device.yaml
└── custom_components/
    └── inky_frame_57/
        ├── display.py       # Python wrapper that forces lambda compilation
        └── inky_frame_57.h  # Core C++ hardware driver
```

## YAML Configuration

Below is a complete minimal configuration to initialize the display, SPI bus, and the physical buttons.

```yaml
spi:
  id: spi_bus
  clk_pin: 18
  mosi_pin: 19

# Initialize the 74HC165 Shift Register for physical buttons
sn74hc165:
  - id: inky_shift_register
    data_pin: 10
    clock_pin: 8
    latch_pin: 9

display:
  - platform: inky_frame_57
    id: inky_display
    spi_id: spi_bus
    cs_pin: 17
    # Set to 'never' if you plan to delay updates until API sensors populate
    update_interval: 300s 
    lambda: |-
      it.fill(Color(255, 255, 255));
      it.filled_rectangle(0, 0, 600, 60, Color(0, 0, 255));
      it.print(20, 20, id(roboto_font), Color(255, 255, 255), "Inky Frame is Online!");

# Map the physical buttons (A through E)
binary_sensor:
  - platform: gpio
    name: "Button A"
    pin:
      sn74hc165: inky_shift_register
      number: 0
    on_press:
      - logger.log: "Button A Pressed!"
```

## Troubleshooting

*   **"Ghost Lambda" / Screen remains pure White or Black:** If your text or shapes are not rendering, ESPHome's compiler likely cached an old C++ file and dropped your YAML lambda. Go to the ESPHome Dashboard, click the three vertical dots on your device card, and execute a **Clean Build** to force the lambda to recompile.
*   **Missing Glyph Boxes:** ESPHome cannot interpret standard `\n` characters natively without custom C++ string splitting. If you attempt to print a `\n` via a text template, you will see an unrenderable square box on the screen.
*   **Compile Error on Lambda Registration:** Ensure your `display.py` wrapper sets the writer to reference the base display class (`DisplayRef = display_ns.class_('Display').operator('ref')`), not a pointer.



  ### Disclaimer
  Lots of AI use to create this and get it working properly.  I haven't done C programming in 30+ years... 
