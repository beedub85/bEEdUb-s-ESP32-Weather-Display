# Weather Display — Waveshare ESP32-S3 Touch LCD 7

A native 800 × 480 touchscreen dashboard that reads an ESP32 weather station wirelessly and displays Google Calendar events. It boots straight into weather cards, with swipe navigation to Calendar and Settings. All device configuration is entered on the touchscreen and saved between restarts; no configuration file is required. Open this folder in VS Code with PlatformIO installed. Firmware version: 0.1.0, first hardware-test release.

## Put it on GitHub

Follow [GITHUB_SETUP.md](docs/GITHUB_SETUP.md) for Windows/VS Code steps. The repository description and release-note text are in [GITHUB_WRITEUP.md](docs/GITHUB_WRITEUP.md). This README is ready to serve as the repository overview.

## Navigation

The display boots straight into **Weather**. Swipe right from Weather for **Calendar**; swipe left from Weather for **Settings**. Swipe back to return. There are no Calendar/Weather/Settings navigation buttons. Swipes are not circular: Calendar and Settings are the outer pages.

Weather cards show temperature, humidity, wind, pressure, rolling 24-hour rainfall, light, and ENS160 air quality, with native vector icons. The main condition icon changes for detected rain, estimated sunny/partly cloudy/cloudy conditions, low light, or unavailable data. Sunny/cloudy choices are rough light-level estimates (not a cloud measurement or forecast), labelled “est.”. Rain uses the station’s `is_raining` flag (rain detected within its last-five-minute window), rather than the day’s accumulated rainfall. Humidity does not determine cloud cover. No extra weather API or account is required. Stale data clears the main condition icon to unavailable. Tap any card for every available sensor reading plus station diagnostics. Values are `--` when unavailable. Station readings refresh every five seconds; failed requests retain the last values and label them stale after 30 seconds.

Calendar has a Today agenda and a Google Calendar-style seven-column Week view. The week starts Monday. Scroll vertically for the rest of the day; tap an event for details. Arrow buttons navigate weeks, and This week returns to the current week. Today resets to today's agenda. All-day and multi-day events and overlapping appointments are supported. Calendar sync is every five minutes, with a manual refresh button. Up to 100 events per selected week and eight calendars are supported; a visible warning indicates truncation. The Google Apps Script bridge expands recurring occurrences using Google's Calendar API.

## 1. Build and upload

1. Install VS Code and the PlatformIO IDE extension.
2. Extract this ZIP. Open the **weather-display** folder containing `platformio.ini` using PlatformIO → Open Project.
3. Connect the Waveshare board using a USB data cable and a stable 5 V supply. Use the board's UART/USB programming connection; this build logs through UART at 115200 baud.
4. Select **waveshare_7_8mb** and click PlatformIO **Build** (check mark), then **Upload** (arrow). This default works with 8 MB boards and uses only the first 8 MB on a 16 MB board. A `waveshare_7_16mb` environment is included for a board confirmed to have 16 MB flash; the partition layout remains the same.
5. The first build downloads the pinned Arduino 3 toolchain and libraries and can take several minutes. The project uses the pioarduino platform package, as required for Arduino 3 by the display-library example; PlatformIO installs it from the URL in `platformio.ini`.
6. If the board will not enter flashing mode, hold BOOT, tap RESET, release BOOT, and retry Upload. Choose the correct COM port if PlatformIO cannot detect it.
7. Open Serial Monitor at 115200 if troubleshooting. The boot log reports detected flash and PSRAM sizes. This board needs its 8 MB octal PSRAM; a plain ESP32 without it is unsupported.

Terminal equivalents (inside the extracted project folder):

```powershell
pio run -e waveshare_7_8mb
pio run -e waveshare_7_8mb -t upload
pio device monitor -b 115200
```

No filesystem upload is required. Never flash this display firmware onto the existing weather station.

## 2. First connection

1. On the screen, swipe left to **Settings**.
2. Tap **Wi-Fi**. Enter your 2.4 GHz network name and password with the onscreen keyboard; tap Save. Scroll the form to reach the password field.
3. Tap **Weather station** and enter its base address, e.g. `http://192.168.2.149`. Use the current IP shown on your working station dashboard. Do not append `/api/status`; the app appends it.
4. Swipe right back to Weather. Values should appear after connection. The display and station must be reachable on the same LAN; guest Wi-Fi/client isolation can block this.
5. If possible, reserve the station's IP in your router so its address stays consistent.

Settings are saved in the display's NVS flash. They do not modify the station. A Wi-Fi password is needed locally on the device, not in this conversation. Saved secrets are not flash-encrypted in this first release; anyone with physical access to the board may extract them.

## 3. Google Calendar

Follow [GOOGLE_CALENDAR_SETUP.md](docs/GOOGLE_CALENDAR_SETUP.md). The included bridge is deployed in your Google account and requests **read-only Calendar** access. You do not need a separate home server, MQTT broker, Home Assistant, or paid API key.

The calendar screen is a custom native interface inspired by Google Calendar, not the Google website running on the ESP32. It displays events only; it does not create/edit/delete them, show Google Tasks, or send invitations.

## 4. Time, units, and dimming

- Default time zone: Central Canada with daylight saving (`CST6CDT,M3.2.0,M11.1.0`). Choose a named Canadian region in Settings if needed. The internal time-zone strings use POSIX notation; Eastern: `EST5EDT,M3.2.0,M11.1.0`; Saskatchewan without DST: `CST6`; UTC: `UTC0`. Internet time sync is required for calendar access and TLS verification. The app handles local day/week boundaries with DST rather than assuming all days are 24 hours.
- Imperial units toggle converts from the station's canonical metric API fields. The station's own dashboard unit preference has no effect on this display. Temperature: °C/°F; wind: km/h/mph; rain: mm/in; pressure: hPa/inHg.
- This board's backlight is switched through a CH422G expander, not a PWM output. **Display dimming** changes rendered image intensity (20–100%) while preserving touch input. It does not reduce the backlight's electrical power.
- ENS160 eCO2 is an estimate, not a direct CO2 measurement. Its AQI is the sensor's 1–5 index, not a government particulate AQI. Solar radiation is the station's light-derived estimate.

## Actual rendered screens

These are rendered from the firmware's LVGL UI with sample data, not photographs of tested hardware:

![Weather](docs/weather.png)
![Today](docs/calendar-today.png)
![Week](docs/calendar-week.png)
![Settings](docs/settings.png)

## Verification and limits

See [VALIDATION.md](docs/VALIDATION.md) for build results and tests. The actual Waveshare device, the station on your LAN, and your Google account are not available in this environment. Hardware touch orientation, sustained Wi-Fi/display stability, and live account authorization must be verified on your board. This release uses partial LVGL refresh into a single RGB framebuffer with the manufacturer's bounce-buffer configuration; brief tearing during page redraw is possible.

Fonts in this first release use LVGL's built-in Montserrat character coverage (basic Latin, degree sign, bullet, and UI icons); some accented/non-Latin event text may display missing-glyph boxes. Certificate roots are included from Google Trust Services and should be updated periodically for long-term use. Certificate verification is enabled; the firmware does not use insecure TLS mode.

## Source layout

- `src/main.cpp`: Waveshare ST7262 RGB display / GT911 touch port and LVGL loop.
- `src/ui.cpp`: cards, swipe gestures, calendar views, event details, settings editor, and software dimming.
- `src/network.cpp`: Wi-Fi reconnects, NVS, read-only station requests, calendar HTTPS requests, background worker.
- `include/core.hpp`: testable date/week, overlap, and unit helpers.
- `calendar-bridge/`: Google Apps Script source and manifest.
- `tests/`: host tests without hardware or account credentials.
- `lib/`: the three pinned Espressif driver libraries, bundled with licenses and normalized PlatformIO manifests; see `THIRD_PARTY.md`.

Pinned dependencies: pioarduino Espressif32 53.03.11 / Arduino ESP32 3.1.1 (ESP-IDF 5.3); ESP32_Display_Panel 1.0.3; ESP32_IO_Expander 1.0.1; esp-lib-utils 0.1.2; LVGL 8.4.0; ArduinoJson 7.4.2.

Hardware references: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7 and https://github.com/esp-arduino-libs/ESP32_Display_Panel . Station source: https://github.com/beedub85/weatherstation/tree/esp32-support .
