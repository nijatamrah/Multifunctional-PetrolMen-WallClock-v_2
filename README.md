# Divar Saati v2 - ESP32 Smart Wall Clock

A DIY wall clock for ESP32 and a 160x128 ST7735 TFT screen, with a smooth animated UI.

## Features
- Digital clock (neon style) and analog clock with smooth second hand, NTP time (Baku, UTC+4)
- Weather: animated icons, feels-like, humidity, wind, 3-day forecast (OpenWeatherMap)
- Market: Brent oil, USD/AZN, EUR/AZN, Bitcoin
- WiFi scanner: radar animation, signal strength, encryption type, open networks in red
- Pomodoro timer with progress ring
- Cyberman: Matrix rain with cybersecurity tips
- Settings: brightness, night mode (dims 23:00-07:00), clock style (saved in flash)
- OTA updates over WiFi
- Flicker-free drawing (full frame in RAM, then sent to the screen)
- Network requests run in a separate task, so buttons never freeze

## Hardware
| Part | ESP32 pin |
|------|-----------|
| TFT CS | 5 |
| TFT DC | 27 |
| TFT RST | 33 |
| TFT MOSI | 15 |
| TFT SCK | 14 |
| TFT backlight | 22 |
| Button UP | 18 |
| Button DOWN | 19 |
| Button LEFT | 32 |
| Button RIGHT | 26 |
| Button MID | 25 |

Buttons connect between the pin and GND (internal pull-up is used).

## Libraries
- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- ArduinoJson (v6 or v7)

## Setup
1. Copy `secrets.example.h` to `secrets.h` and fill in your WiFi, OpenWeatherMap key and OTA password.
2. In Arduino IDE choose board **ESP32 Dev Module** and partition scheme **Minimal SPIFFS (1.9MB APP with OTA)**.
3. Upload. After the first upload you can update over WiFi (Tools > Port > network port).

## Controls
- Clock: MID = menu, LEFT/RIGHT = digital/analog
- Menu: arrows = choose, MID = open
- Apps: LEFT = back (Settings: MID = save and back)

## Security notes
- `secrets.h` is ignored by git. Never commit real passwords or API keys.
- HTTPS requests use `setInsecure()` (no certificate check) to keep the code simple.
