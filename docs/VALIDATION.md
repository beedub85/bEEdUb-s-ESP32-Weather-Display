# Verification — Weather Display 0.1.0

Date: 2026-11-10.

## Automated checks completed

- Host C++ tests passed: local-day and Monday/week boundaries, spring 23-hour and fall 25-hour DST days, all-day exclusive end dates, overlapping event columns, events crossing midnight, metric/imperial conversions, compass headings, and missing-direction handling.
- Google Apps Script tests passed against mocked Google services: token rejection before Calendar API access, range limits, recurring-event expansion request, multiple calendars, Google event colors, all-day date preservation, bounded event count/pagination, and sanitized service errors.
- Native LVGL rendering passed for Weather, Calendar Today, Calendar Week, and Settings using the **actual `src/ui.cpp`** with mock hardware/network data. Swipe page ordering, the brightness-slider swipe guard, scroll-position preservation during refresh, modal navigation blocking, and time-zone selection passed. Keyboard and time-zone picker layouts were also rendered. Condition-icon integration renders verify rain priority, sunny/partly cloudy/cloudy light estimates, low light, unavailable light readings, and stale-data fallback. The test also checks that the removed page navigation buttons are absent. Included PNGs are outputs of the native renderer, not hardware captures.

## Firmware build

`waveshare_7_8mb`: **PASS**. The linked ESP32-S3 image was built with Arduino 3.1.1 / ESP-IDF 5.3 and GCC 13.2.0 (20240530).

The normal package registry was unreliable in this environment. Validation obtained the same compiler release from Espressif's upstream GitHub assets and skipped unused GDB/filesystem-tool downloads in the local validation platform. The delivered `platformio.ini` keeps the portable platform's normal tool packages; the application source, framework, libraries, board definition, and build flags are the same. No custom validation paths or Linux-only package URLs are included in the delivered project.

The initial linked build used 48,668 bytes of static internal RAM (14.9% of 320 KiB) and 1,674,884 bytes of application flash (50.1% of the configured application partition). These are static/link-time figures, not measurements of runtime heap usage.

The project uses Arduino 3.1.1 / ESP-IDF 5.3 and the display driver's supported Waveshare board configuration. The older Arduino 2 toolchain is incompatible with this driver version and is not the delivered project configuration.

## Run the host checks

From the project folder on a machine with g++, Node.js, and Python:

```sh
g++ -std=c++17 -Iinclude tests/core_test.cpp -o core-test
./core-test
node tests/bridge_test.js
```

To reproduce the actual UI screenshots, clone LVGL v8.4.0 separately, then run:

```sh
git clone --depth 1 --branch v8.4.0 https://github.com/lvgl/lvgl.git /tmp/lvgl
python tests/render_ui.py --lvgl-dir /tmp/lvgl --output rendered
```

The renderer needs gcc/g++; Pillow is optional for PNG output. It uses tiny Arduino/Wi-Fi/FreeRTOS shims for a host build; those are outside the firmware source directory and are not included in the ESP32 build.

## Still needs verification on your board

1. Upload and reboot; confirm the UI starts on Weather and touch coordinates match the screen.
2. Save Wi-Fi settings, confirm they survive a restart, and check LAN access to the weather station.
3. Compare temperature, humidity, wind, and rain to the station's existing dashboard in the same units.
4. Confirm horizontal navigation, vertical agenda/week scrolling, event details, settings keyboard/forms, and dimming.
5. Deploy/authorize the Google bridge in your own account; verify real events, recurrence instances, all-day dates, overlapping appointments, and the selected time zone against Google Calendar.
6. Disconnect/reconnect Wi-Fi and stop/restart the station; confirm last-update/stale indicators and recovery.
7. Leave the screen running for several hours to check RGB refresh/Wi-Fi stability. Partial framebuffer refresh can tear briefly during redraw; the hardware is not available for a stability soak here.

No physical ESP32, local weather station, or live Google account was available in the development environment. Automated tests and a successful compile do not establish those hardware/integration results.
