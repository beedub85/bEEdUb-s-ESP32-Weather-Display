# Bundled source dependencies

This project bundles the following Espressif libraries under `lib/` so the driver versions stay fixed and PlatformIO does not repeatedly resolve incompatible Arduino `depends` version expressions.

| Library | Version | Upstream |
|---|---|---|
| ESP32_Display_Panel | 1.0.3 | https://github.com/esp-arduino-libs/ESP32_Display_Panel/tree/v1.0.3 |
| ESP32_IO_Expander | 1.0.1 | https://github.com/esp-arduino-libs/ESP32_IO_Expander/tree/v1.0.1 |
| esp-lib-utils | 0.1.2 | https://github.com/esp-arduino-libs/esp-lib-utils/tree/v0.1.2 |

Their upstream license files are retained in each library directory. Their implementation source is unchanged. A minimal PlatformIO `library.json` manifest has been added to each directory to make local library discovery deterministic. Upstream examples, documentation, tests, and git metadata are omitted from this distribution.

LVGL 8.4.0 and ArduinoJson 7.4.2 are downloaded by PlatformIO and retain their upstream licenses. The Arduino framework, toolchain, and platform package are also downloaded during build rather than redistributed in this ZIP.

The Google Trust Services root certificates in `include/google_roots.h` were obtained from https://pki.goog/roots.pem on 2026-11-10. They are used for certificate verification, not certificate bypass.
