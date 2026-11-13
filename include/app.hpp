#pragma once
#include <Arduino.h>
#include <atomic>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "core.hpp"
#include "screen_idle.hpp"
#include "local_weather.hpp"

constexpr uint32_t weatherPollIntervalMs = 5 * 60 * 1000;
constexpr uint32_t weatherStaleAfterMs = weatherPollIntervalMs + 30 * 1000;
struct Settings {
  String ssid, password, station="http://192.168.2.149", calendarUrl, calendarToken;
  String timezone=core::winnipegTimeZone;
  String postalCode, printRoomUrl, printRoomKey;
  bool imperial=false;
  bool rotateScreen=false;
  int brightness=100;
  uint32_t screenTimeoutSeconds = ScreenIdle::defaultTimeoutSeconds;
};
struct Weather {
  bool loaded=false;
  uint32_t received=0;
  float temp=NAN, hum=NAN, dew=NAN, min=NAN, max=NAN, press=NAN;
  float wind=NAN, gust=NAN, gust10=NAN, dir=NAN, rain=NAN, hour=NAN, total=NAN;
  float lux=NAN, solar=NAN, aqi=NAN, eco2=NAN, tvoc=NAN, vcc=NAN;
  int tips=0, freeHeap=0;
  bool raining=false;
  String sensors;
  String firmware, uptime, ip, rssi, ssid, stationTime;
};
struct ForecastHour {
  time_t at = 0;
  float temperature = NAN;
  int precipitation = -1;
  String condition;
};
struct WeatherAlert {
  String title;
  time_t expires = 0;
};
struct LocalForecast {
  String postalCode, location, region, error;
  time_t checked = 0;
  bool alertsKnown = false;
  std::vector<ForecastHour> hours;
  String accumulationSummary;
  std::vector<WeatherAlert> alerts;
};
extern LocalForecast localForecast;
extern localWeather::History weatherHistory;

struct PrintRoom {
  String source, printer, status, job, stage, error;
  float progress = NAN, remainingMinutes = NAN, layer = NAN, layers = NAN;
  float nozzle = NAN, bed = NAN, chamber = NAN, roomTemp = NAN, humidity = NAN, pm25 = NAN;
  time_t finishAt = 0;
  uint32_t received = 0;
  bool loaded = false;
};
extern PrintRoom printRoom;
extern std::atomic<bool> printRoomOpen;
inline bool validPrintRoomUrl(const String &url) {
  return url.isEmpty() || (url.startsWith("http://") && url.endsWith("/status") &&
      url.length() > 14 && strspn(url.c_str() + 7,
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-:/") == url.length() - 7);
}

inline bool validPrintRoomKey(const String &key) {
  return key.length() == 32 && strspn(key.c_str(), "0123456789abcdefABCDEF") == 32;
}
struct State {
  Weather weather;
  core::Events events, todayEvents;
  time_t todayWeek = 0;
  uint32_t todayReceived = 0;
  time_t calendarWeek=0;
  uint32_t calendarReceived=0;
  bool calendarLoaded=false, truncated=false;
  String weatherError="Waiting for connection", calendarError="Calendar not configured";
};
extern Settings settings;
extern State shared;
extern SemaphoreHandle_t stateMutex;
extern volatile int requestedWeek;
extern std::atomic<bool> calendarRefreshRequested, todayOverviewOpen;
void loadSettings();
void saveSettings();
void startNetwork();
void uiBegin();
void uiTick();
bool uiTouch(bool pressed,int x,int y);
void uiSetDimming(int percent);

enum class CalendarUsbResult { Pending, Saved, Invalid };
CalendarUsbResult uiCalendarUsbReceive(char byte);
void uiCalendarUsbExpire();

// Negative means the battery gauge is unavailable.
void uiSetBatteryPercent(int percent);
