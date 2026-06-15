# PipFace

Animated procedural face for the ESP32-S3 + ILI9341 tutor device. Twelve emotion states drawn with simple shapes — no SD card, no image assets, ~30 fps via a 240×240 off-screen sprite.

## Hardware

- **MCU:** ESP32-S3 with **PSRAM enabled** (the sprite buffer is ~115 KB).
- **Display:** 2.8" IPS ILI9341V, 240×320, RGB565, 4-wire SPI, portrait orientation (face renders on the top 240×240; bottom 240×80 is the optional status strip).

## Dependencies

- [`TFT_eSPI`](https://github.com/Bodmer/TFT_eSPI) (Arduino Library Manager).

`TFT_eSPI/User_Setup.h` must match your wiring. **This project's board is the
LCDWIKI 2.8" ESP32-S3 Display** — use the pins below (a ready-to-copy file ships
as [`User_Setup_LCDWIKI.h`](User_Setup_LCDWIKI.h)):

```c
#define ILI9341_DRIVER       // works for ILI9341 and ILI9341V (same init)
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1          // tied to board reset; no dedicated GPIO
#define SPI_FREQUENCY 40000000

// IPS panels (this one is IPS) often need color inversion. If the demo
// looks inverted (black appears white), uncomment the next line:
// #define TFT_INVERSION_ON
```

> ⚠️ The previous generic example used `TFT_DC 9` / `TFT_RST 8`. **Do not use
> those on this board** — GPIO8 is the I2S speaker output (`I2S_DOUT`) and GPIO9
> is the battery ADC. The backlight (GPIO45) is driven by the main firmware, so
> there is no `TFT_BL` line here.

## Quick start

1. Copy this folder into your Arduino `libraries/` directory (so the path is `libraries/pip_face/PipFace.h`).
2. Open **File → Examples → PipFace → PipDemo** and flash it. The face should cycle through all 12 emotions every ~2.6 s.
3. In your main sketch, include `PipFace.h` and call:

```cpp
#include "PipFace.h"

void setup() {
  Pip::begin();              // false return = PSRAM disabled
}

void onDeviceStateUpdate(const char* status, int mood) {
  Pip::setDeviceStatus(status, mood);   // driven by Firebase
}

void loop() {
  pollFirebase();             // your code
  Pip::tick();                // animates; ~30 fps internally
}
```

`tick()` is cheap and non-blocking — call it from your main loop as often as you like.

## Status → emotion mapping

`setDeviceStatus()` speaks the project's `deviceState.status` vocabulary. Mood (1–5) refines the `feedback` state; pass `-1` if unknown.

| `deviceState.status` | Mood | → Pip emotion |
|---|---|---|
| `idle` | * | IDLE |
| `asking` | * | SPEAKING |
| `listening` | * | LISTENING |
| `feedback` | `>= 4` | PROUD |
| `feedback` | `<= 2` | CONCERNED |
| `feedback` | `3` or `-1` (unknown) | ENCOURAGING |
| `break` | * | SLEEPY |
| `error` | * | OOPS |

Unknown status strings are silently ignored (current state preserved).

`THINKING`, `HAPPY`, `CELEBRATING`, and `PLAYFUL` are not reachable through `setDeviceStatus()`. Use `Pip::setEmotion("happy")` etc. when your tutoring logic has finer-grained signals.

## API

```cpp
namespace Pip {
  bool begin();                                          // init TFT + sprite
  void tick();                                           // call every loop()
  void setEmotion(const char* label);                    // direct 12-state setter
  void setDeviceStatus(const char* status, int mood);    // project-vocabulary setter
  void setStrip(const char* questionOrLabel, int stars); // bottom strip (re-renders on change only)
}
```

All setters are non-blocking and safe to call as often as you want — strip redraws only fire when the underlying text/stars actually change.

## Pre-flash sanity (optional)

If you have `arduino-cli` installed locally you can compile-check the demo without flashing:

```
arduino-cli compile --fqbn esp32:esp32:esp32s3 pip_face/examples/PipDemo
```

This catches header path / TFT_eSPI symbol issues before the board is involved.

## Files

```
pip_face/
├── PipFace.h                       — public API
├── PipFace.cpp                     — drawing + state machine
├── library.properties              — Arduino library manifest
└── examples/
    └── PipDemo/
        └── PipDemo.ino             — flashable demo, cycles all states
```
