#pragma once
#include <Arduino.h>
#include <atomic>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "core.hpp"

constexpr uint32_t weatherPollIntervalMs = 5 * 60 * 1000;
constexpr uint32_t weatherStaleAfterMs = weatherPollIntervalMs + 30 * 1000;
struct Settings {
  String ssid, password, station="http://192.168.2.149", calendarUrl, calendarToken;
  String timezone="CST6CDT,M3.2.0,M11.1.0"; // Central with DST; editable in Settings
  bool imperial=false;
  int brightness=100;
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
struct State {
  Weather weather;
  core::Events events;
  time_t calendarWeek=0;
  uint32_t calendarReceived=0;
  bool calendarLoaded=false, truncated=false;
  String weatherError="Waiting for connection", calendarError="Calendar not configured";
};
extern Settings settings;
extern State shared;
extern SemaphoreHandle_t stateMutex;
extern volatile int requestedWeek;
extern std::atomic<bool> calendarRefreshRequested;
void loadSettings();
void saveSettings();
void startNetwork();
void uiBegin();
void uiTick();
void uiTouch(bool pressed,int x,int y);
void uiSetDimming(int percent);
