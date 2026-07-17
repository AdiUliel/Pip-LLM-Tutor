# Unit Tests

Validation sketches for the individual hardware components. Flash each one on
its own (Arduino IDE, same board settings as the main firmware) to verify a
part in isolation — useful for elimination-style debugging when something
stops working.

| Sketch | What it tests |
|---|---|
| `test_esp32_upload/` | Board + toolchain sanity — verifies the ESP32-S3 flashes and runs at all |
| `test_button/` | Push-to-talk button wiring (IO2↔IO3, `INPUT_PULLUP`, active LOW) |
| `test_display_touch/` | ILI9341 display + FT6336G capacitive touch (SPI + I2C) |
| `test_microphone/` | ES8311 codec ADC path — captures audio from the onboard mic over I2S |
| `test_speaker/` | ES8311 codec DAC path — plays audio through the FM8002E amp + speaker |

Cloud-side test utility: `firebase/functions/mock_pair.js` simulates an
app→child pairing from the terminal (no second phone needed) — see its header
comment for usage.
