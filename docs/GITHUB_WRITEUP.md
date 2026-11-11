# GitHub text for ESP32 Weather Display

## Repository name

`esp32-weather-display`

## Short About description

Swipe-controlled weather and Google Calendar touchscreen for the Waveshare ESP32-S3 7-inch display, built with PlatformIO and LVGL.

## Project overview

ESP32 Weather Display turns a Waveshare ESP32-S3 7-inch touchscreen into a dedicated wireless weather and calendar dashboard. It boots directly into a set of weather cards showing readings from an existing ESP32 weather station. Swipe right to open Google Calendar, or swipe left to open Settings.

The weather dashboard groups temperature, humidity, wind, pressure, rainfall, light, and ENS160 air-quality readings into readable cards. Tapping a card opens the available sensor details and station diagnostics. The display reads the station's existing `/api/status` endpoint, so the station firmware can stay as it is.

Google Calendar includes a Today agenda and a Google Calendar-inspired Week grid, with colored events, all-day entries, overlapping appointments, vertical scrolling, and event details. A read-only Google Apps Script bridge runs in the owner's account and supplies the selected calendar data over HTTPS.

All device setup happens through the touchscreen Settings screen, including Wi-Fi, station address, calendar connection, time zone, units, and display dimming. Settings persist between restarts. The project is built and uploaded using VS Code and PlatformIO.

The first release has passed an ESP32-S3 firmware compile, host logic tests, and native rendering tests of the actual LVGL interface. Physical hardware, local weather-station connectivity, and live Google Calendar authorization still need testing on the owner's device.

## Suggested first release title

v0.1.0 — Weather, calendar, and touchscreen setup

## Suggested first release notes

Initial hardware-test release for the Waveshare ESP32-S3-Touch-LCD-7 (800 × 480).

- Weather startup dashboard with sensor cards and a detailed readings view.
- Swipe navigation between Calendar, Weather, and Settings.
- Google Calendar Today agenda and Week view through a read-only Apps Script bridge.
- Onscreen Wi-Fi and calendar setup, saved settings, Canadian time-zone choices, units, and software dimming.
- PlatformIO project, setup instructions, actual UI renders, and automated logic/rendering tests.

Build and simulator checks pass. This release still needs verification on the physical display, against the owner's live station and calendar.
