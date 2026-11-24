#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "app.hpp"
#include "core.hpp"
#include "local_weather.hpp"
#include "lvgl.h"
#include "screen_idle.hpp"
#include "screen_rotation.hpp"
#include "weather_icons.hpp"

namespace {

// Modern Dark Glassmorphism Palette (16-bit RGB565 friendly)
constexpr uint32_t BG = 0x0D1117;
constexpr uint32_t CARD = 0x161B22;
constexpr uint32_t CARD_ALT = 0x1C232D;
constexpr uint32_t BORDER = 0x2A323D;
constexpr uint32_t TEXT = 0xF0F6FC;
constexpr uint32_t MUTED = 0x8B949E;
constexpr uint32_t CYAN = 0x38BDF8;
constexpr uint32_t CYAN_DARK = 0x0E3A52;
constexpr uint32_t BLUE = 0x3B82F6;
constexpr uint32_t EMERALD = 0x22C55E;
constexpr uint32_t AMBER = 0xF59E0B;
constexpr uint32_t RED = 0xEF4444;
constexpr uint32_t VIOLET = 0xA855F7;

constexpr int kScreenW = screenRotation::width;
constexpr int kScreenH = screenRotation::height;
constexpr int kSettingsRowHeight = 38;
constexpr int kSettingsRowPitch = 42;
constexpr int kSettingsTop = 58;

enum Page { SettingsPage = 0, WeatherPage = 1, CalendarPage = 2 };
enum class WeatherPanel { None, Today, Trends, Alerts, PrintRoom, StationHealth };

Page page = WeatherPage;
WeatherPanel activePanel = WeatherPanel::None;
localWeather::Metric trendMetric = localWeather::Temperature;

lv_obj_t *root = nullptr;
lv_obj_t *clockLabel = nullptr;
lv_obj_t *dateHeaderLabel = nullptr;
lv_obj_t *wifiIconLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *batteryLabel = nullptr;
lv_obj_t *modalBatteryLabel = nullptr;
lv_obj_t *dimmer = nullptr;
lv_obj_t *modal = nullptr;

// Weather Dashboard objects (0 = Hero Temp, 1..6 = 6 Grid Cards)
lv_obj_t *weatherValues[7]{};
lv_obj_t *weatherSub[7]{};
lv_obj_t *weatherImages[7]{};
lv_obj_t *conditionLabel = nullptr;
icons::State iconStates[7]{};

// Bottom dock & banners
lv_obj_t *alertsButton = nullptr;
lv_obj_t *alertsBtnLabel = nullptr;
lv_obj_t *printBanner = nullptr;

// Sub-panel objects
lv_obj_t *todayStation = nullptr;
lv_obj_t *todayEvents = nullptr;
lv_obj_t *todayForecast = nullptr;
lv_obj_t *todayAccumulation = nullptr;
lv_obj_t *alertsBody = nullptr;
lv_obj_t *stationHealth = nullptr;

// Print Room modal objects
lv_obj_t *printPrinter = nullptr;
lv_obj_t *printStatus = nullptr;
lv_obj_t *printJob = nullptr;
lv_obj_t *printPercent = nullptr;
lv_obj_t *printArc = nullptr;
lv_obj_t *printEta = nullptr;
lv_obj_t *printMetrics[7]{};
lv_obj_t *printConnection = nullptr;

// Trend modal objects
lv_obj_t *trendChart = nullptr;
lv_obj_t *trendScale = nullptr;
lv_obj_t *trendReading = nullptr;
lv_obj_t *trendPills[localWeather::MetricCount]{};
lv_point_t trendSegments[localWeather::sampleCount - 1][2]{};

// Calendar objects
lv_obj_t *calendarTodayList = nullptr;
lv_obj_t *calendarBody = nullptr;
lv_obj_t *calendarDate = nullptr;
lv_obj_t *contentScroller = nullptr;
lv_obj_t *btnTodayView = nullptr;
lv_obj_t *btnWeekView = nullptr;

// Settings & Editor objects
lv_obj_t *fields[3]{};
lv_obj_t *keyboard = nullptr;
lv_obj_t *form = nullptr;
lv_obj_t *calendarUsbStatus = nullptr;

// State & tracking
State snapshot;
LocalForecast shownForecast;
PrintRoom shownPrintRoom;
localWeather::History displayedHistory;
bool weekView = true;
bool redraw = true;
bool calendarDirty = true;
bool touchDown = false;
int navigation = 0;
int editKind = 0;
int batteryPercent = -1;
int dimmingPercent = 100;
uint32_t lastTick = 0;
uint32_t lastCalendar = 0;
time_t lastMinute = 0;
time_t shownWeek = 0;
time_t renderedWeek = 0;
String lastCalendarError;
time_t lastTrendSample = 0;
time_t lastAlertsCheck = 0;
bool lastAlertsFresh = false;
int lastActiveAlerts = -1;
String lastAlertsError;
Settings draft;

// USB Calendar setup buffer
uint32_t calendarUsbStarted = 0;
char calendarUsbLine[540]{};
size_t calendarUsbLength = 0;
bool calendarUsbOverflow = false;

const char *tzNames[] = {
    "Winnipeg (Central)",
    "Toronto / Ottawa (Eastern)",
    "Vancouver (Pacific)",
    "Edmonton / Calgary (Mountain)",
    "Regina (Saskatchewan)",
    "Halifax (Atlantic)",
    "St. John's (Newfoundland)",
    "UTC",
    "Chicago (US Central)",
};

const char *tzCodes[] = {
    core::winnipegTimeZone,
    "EST5EDT,M3.2.0,M11.1.0",
    "PST8PDT,M3.2.0,M11.1.0",
    "MST7MDT,M3.2.0,M11.1.0",
    "CST6",
    "AST4ADT,M3.2.0,M11.1.0",
    "NST3:30NDT,M3.2.0,M11.1.0",
    "UTC0",
    "CST6CDT,M3.2.0,M11.1.0",
};

constexpr int kTzCount = sizeof(tzCodes) / sizeof(tzCodes[0]);

// Forward declarations
void build();
void closeModal();
void weatherUpdate();
void calendarUpdate();
void drawTrend();
void updateToday(const core::Events &todayAgenda, bool todayStale);
void updateAlerts();
void updatePrintRoom();
void updateStationHealth();
void updatePrintBanner(const PrintRoom &room, uint32_t now);

void clearCalendarUsbLine() {
  calendarUsbLength = 0;
  calendarUsbOverflow = false;
  calendarUsbLine[0] = '\0';
}

String timezoneName() {
  for (int i = 0; i < kTzCount; ++i) {
    if (settings.timezone == tzCodes[i]) return tzNames[i];
  }
  return settings.timezone;
}

lv_color_t color(uint32_t hex) { return lv_color_hex(hex); }

void updateLabel(lv_obj_t *lbl, const String &txt) {
  if (!lbl) return;
  if (strcmp(lv_label_get_text(lbl), txt.c_str()) != 0) {
    lv_label_set_text(lbl, txt.c_str());
  }
}

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bgHex,
              int radius = 14, uint32_t borderHex = BORDER, int borderW = 1) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, color(bgHex), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  if (borderW > 0) {
    lv_obj_set_style_border_color(o, color(borderHex), 0);
    lv_obj_set_style_border_width(o, borderW, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
  }
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *label(lv_obj_t *parent, const String &txt, const lv_font_t *font,
                uint32_t hex, int x = 0, int y = 0) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt.c_str());
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color(hex), 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_obj_t *button(lv_obj_t *parent, const String &txt, int x, int y, int w, int h,
                 uint32_t bgHex, uint32_t textHex, lv_event_cb_t cb,
                 void *userData = nullptr, int radius = 18,
                 uint32_t borderHex = BORDER, int borderW = 1) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, color(bgHex), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(b, radius, 0);
  if (borderW > 0) {
    lv_obj_set_style_border_color(b, color(borderHex), 0);
    lv_obj_set_style_border_width(b, borderW, 0);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
  }
  lv_obj_set_style_bg_color(b, color(CARD_ALT), LV_STATE_PRESSED);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt.c_str());
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, color(textHex), 0);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, userData);
  return b;
}

String fmt(float v, int decimals = 1, const char *suffix = "") {
  if (!std::isfinite(v)) return "--";
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f%s", decimals, v, suffix);
  return String(buf);
}

String tempText(float c, int decimals = 0) {
  if (!std::isfinite(c)) return "--";
  float val = core::temp(c, settings.imperial);
  return fmt(val, decimals, settings.imperial ? "°F" : "°C");
}

String speedText(float kmh) {
  if (!std::isfinite(kmh)) return "--";
  float val = core::wind(kmh, settings.imperial);
  return fmt(val, 0, settings.imperial ? " mph" : " km/h");
}

String rainText(float mm) {
  if (!std::isfinite(mm)) return "--";
  float val = core::rain(mm, settings.imperial);
  return fmt(val, settings.imperial ? 2 : 1, settings.imperial ? " in" : " mm");
}

String pressText(float hpa) {
  if (!std::isfinite(hpa)) return "--";
  float val = core::pressure(hpa, settings.imperial);
  return fmt(val, settings.imperial ? 2 : 0,
             settings.imperial ? " inHg" : " hPa");
}

String screenTimeoutLabel(uint32_t seconds) {
  if (seconds < 60) return String(seconds) + " sec";
  return String(seconds / 60) + " min";
}

String dateText(time_t t, const char *format) {
  if (!t) return "";
  struct tm tm{};
  localtime_r(&t, &tm);
  char buf[48];
  strftime(buf, sizeof(buf), format, &tm);
  return String(buf);
}

void updateBatteryLabel(lv_obj_t *lbl) {
  if (!lbl) return;
  if (batteryPercent < 0) {
    updateLabel(lbl, LV_SYMBOL_BATTERY_FULL " N/C");
    lv_obj_set_style_text_color(lbl, color(MUTED), 0);
  } else {
    const char *sym = LV_SYMBOL_BATTERY_FULL;
    uint32_t col = EMERALD;
    if (batteryPercent <= 15) {
      sym = LV_SYMBOL_BATTERY_EMPTY;
      col = RED;
    } else if (batteryPercent <= 35) {
      sym = LV_SYMBOL_BATTERY_1;
      col = AMBER;
    } else if (batteryPercent <= 65) {
      sym = LV_SYMBOL_BATTERY_2;
      col = TEXT;
    } else if (batteryPercent <= 85) {
      sym = LV_SYMBOL_BATTERY_3;
      col = EMERALD;
    }
    updateLabel(lbl, String(sym) + " " + String(batteryPercent) + "%");
    lv_obj_set_style_text_color(lbl, color(col), 0);
  }
}

lv_obj_t *batteryIndicator(lv_obj_t *parent, int x, int y) {
  lv_obj_t *lbl = label(parent, "", &lv_font_montserrat_14, MUTED, x, y);
  updateBatteryLabel(lbl);
  return lbl;
}

void addStatusBar(lv_obj_t *parent) {
  lv_obj_t *bar = box(parent, 0, 0, kScreenW, 38, BG, 0, BORDER, 0);
  lv_obj_t *line = box(bar, 16, 37, kScreenW - 32, 1, BORDER, 0, BORDER, 0);
  (void)line;

  time_t now = time(nullptr);
  String clk = now > 100000 ? dateText(now, "%I:%M %p") : "--:--";
  if (clk.startsWith("0")) clk.remove(0, 1);
  String dt = now > 100000 ? dateText(now, "%a, %b %d") : "";

  clockLabel = label(bar, clk, &lv_font_montserrat_18, TEXT, 18, 9);
  dateHeaderLabel = label(bar, dt, &lv_font_montserrat_14, MUTED, 118, 12);

  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  wifiIconLabel = label(bar, LV_SYMBOL_WIFI, &lv_font_montserrat_16,
                        wifiOk ? CYAN : MUTED, kScreenW - 118, 10);
  batteryLabel = batteryIndicator(bar, kScreenW - 86, 11);
}

void addPageDots(lv_obj_t *parent, Page current) {
  int startX = (kScreenW - 44) / 2;
  int y = kScreenH - 14;
  for (int i = 0; i < 3; ++i) {
    bool active = (i == static_cast<int>(current));
    box(parent, startX + i * 18, y, active ? 10 : 7, active ? 10 : 7,
        active ? CYAN : BORDER, 5, active ? CYAN : BORDER, 0);
  }
}

// ============================================================================
// Modals & Sub-Panels
// ============================================================================

void closeModal() {
  if (modal) {
    lv_obj_del(modal);
    modal = nullptr;
  }
  modalBatteryLabel = nullptr;
  keyboard = nullptr;
  form = nullptr;
  fields[0] = fields[1] = fields[2] = nullptr;
  calendarUsbStatus = nullptr;
  todayStation = nullptr;
  todayEvents = nullptr;
  todayForecast = nullptr;
  todayAccumulation = nullptr;
  alertsBody = nullptr;
  stationHealth = nullptr;
  printPrinter = nullptr;
  printStatus = nullptr;
  printJob = nullptr;
  printPercent = nullptr;
  printArc = nullptr;
  printEta = nullptr;
  for (int i = 0; i < 7; ++i) printMetrics[i] = nullptr;
  printConnection = nullptr;
  trendChart = nullptr;
  trendScale = nullptr;
  trendReading = nullptr;
  for (int i = 0; i < localWeather::MetricCount; ++i) trendPills[i] = nullptr;

  activePanel = WeatherPanel::None;
  todayOverviewOpen.store(false);
  printRoomOpen.store(false);
}

void dismiss(lv_event_t *) { closeModal(); }

lv_obj_t *newModal(const String &title, bool showClose = true) {
  closeModal();
  modal = box(lv_layer_top(), 0, 0, kScreenW, kScreenH, BG, 0, BORDER, 0);
  // Top modal header
  box(modal, 0, 0, kScreenW, 52, CARD, 0, BORDER, 0);
  box(modal, 0, 51, kScreenW, 1, BORDER, 0, BORDER, 0);
  label(modal, title, &lv_font_montserrat_20, TEXT, 20, 14);
  modalBatteryLabel = batteryIndicator(modal, 620, 18);
  if (showClose) {
    button(modal, "Close", kScreenW - 102, 10, 84, 32, CARD_ALT, TEXT, dismiss,
           nullptr, 16, BORDER, 1);
  }
  return modal;
}

void eventDetails(lv_event_t *ev) {
  if (navigation != 0) return;
  size_t idx = reinterpret_cast<size_t>(lv_event_get_user_data(ev));
  if (idx >= snapshot.events.size()) return;
  const core::Event &e = snapshot.events[idx];
  lv_obj_t *m = newModal("Event Details");
  lv_obj_t *card = box(m, 24, 70, kScreenW - 48, 386, CARD, 16, BORDER, 1);
  box(card, 0, 0, 8, 386, e.color ? e.color : CYAN, 4, e.color ? e.color : CYAN, 0);

  label(card, e.title.empty() ? "(No title)" : String(e.title.c_str()),
        &lv_font_montserrat_24, TEXT, 26, 20);

  String whenStr;
  if (e.allDay) {
    whenStr = String(e.startDate.c_str()) + " (All day)";
  } else {
    whenStr = dateText(e.start, "%a, %b %d • %I:%M %p") + " – " +
              dateText(e.end, "%I:%M %p");
  }
  label(card, whenStr, &lv_font_montserrat_16, CYAN, 26, 62);

  int y = 102;
  if (!e.calendar.empty()) {
    label(card, String("Calendar: ") + e.calendar.c_str(),
          &lv_font_montserrat_14, MUTED, 26, y);
    y += 28;
  }
  if (!e.location.empty()) {
    label(card, String("Location: ") + e.location.c_str(),
          &lv_font_montserrat_14, TEXT, 26, y);
    y += 32;
  }
  if (!e.description.empty()) {
    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, e.description.c_str());
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(desc, color(MUTED), 0);
    lv_obj_set_width(desc, kScreenW - 104);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(desc, 26, y + 6);
  }
}

void pickTrendPoint(lv_event_t *) {
  if (!trendChart || !trendReading || displayedHistory.count == 0) return;
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev) return;
  lv_point_t pt;
  lv_indev_get_point(indev, &pt);

  lv_area_t area;
  lv_obj_get_coords(trendChart, &area);
  int relX = pt.x - area.x1;
  int w = area.x2 - area.x1;
  if (w <= 0) return;
  float frac = std::clamp(static_cast<float>(relX) / w, 0.0f, 1.0f);

  const auto &latestSample =
      displayedHistory.at(displayedHistory.count - 1);
  time_t end = latestSample.at;
  time_t start = end - localWeather::historySeconds;
  time_t target = start + static_cast<time_t>(frac * localWeather::historySeconds);

  const localWeather::Sample *best = nullptr;
  long bestDist = 601;
  for (unsigned i = 0; i < displayedHistory.count; ++i) {
    const auto &s = displayedHistory.at(i);
    if (!std::isfinite(s.values[trendMetric])) continue;
    long d = labs(static_cast<long>(s.at - target));
    if (d < bestDist) {
      bestDist = d;
      best = &s;
    }
  }
  if (!best) {
    updateLabel(trendReading, "No sample near this time");
    return;
  }
  String valStr;
  float v = best->values[trendMetric];
  switch (trendMetric) {
    case localWeather::Temperature: valStr = tempText(v, 1); break;
    case localWeather::Pressure: valStr = pressText(v); break;
    case localWeather::Gust: valStr = speedText(v); break;
    case localWeather::Rain: valStr = rainText(v); break;
    default: valStr = fmt(v); break;
  }
  updateLabel(trendReading, dateText(best->at, "%I:%M %p") + "  •  " + valStr);
}

void drawTrend() {
  if (!trendChart) return;
  lv_obj_clean(trendChart);

  // Draw subtle horizontal gridlines
  int w = lv_obj_get_width(trendChart);
  int h = lv_obj_get_height(trendChart);
  for (int g = 1; g <= 3; ++g) {
    int gy = (h * g) / 4;
    box(trendChart, 0, gy, w, 1, BORDER, 0, BORDER, 0);
  }

  for (int i = 0; i < localWeather::MetricCount; ++i) {
    if (!trendPills[i]) continue;
    bool sel = (i == static_cast<int>(trendMetric));
    lv_obj_set_style_bg_color(trendPills[i], color(sel ? CYAN : CARD_ALT), 0);
    lv_obj_t *lbl = lv_obj_get_child(trendPills[i], 0);
    if (lbl) lv_obj_set_style_text_color(lbl, color(sel ? BG : TEXT), 0);
  }

  if (displayedHistory.count == 0) {
    updateLabel(trendScale, "");
    updateLabel(trendReading,
                "History starts after a station reading and internet time sync.");
    return;
  }

  time_t end = displayedHistory.at(displayedHistory.count - 1).at;
  time_t start = end - localWeather::historySeconds;
  float lo = 1e9f, hi = -1e9f, latest = NAN;
  int count = 0;
  time_t latestAt = 0;

  for (unsigned i = 0; i < displayedHistory.count; ++i) {
    const auto &s = displayedHistory.at(i);
    if (s.at < start || s.at > end) continue;
    float v = s.values[trendMetric];
    if (!std::isfinite(v)) continue;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
    latest = v;
    latestAt = s.at;
    ++count;
  }

  if (count == 0) {
    updateLabel(trendScale, "");
    updateLabel(trendReading, "No readings available for this sensor.");
    return;
  }

  auto fmtMetric = [](localWeather::Metric m, float v) {
    switch (m) {
      case localWeather::Temperature: return tempText(v, 1);
      case localWeather::Pressure: return pressText(v);
      case localWeather::Gust: return speedText(v);
      case localWeather::Rain: return rainText(v);
      default: return fmt(v);
    }
  };

  updateLabel(trendScale, dateText(start, "%I:%M %p") + " – " +
                              dateText(end, "%I:%M %p") + "   |   Low: " +
                              fmtMetric(trendMetric, lo) + "   High: " +
                              fmtMetric(trendMetric, hi));
  updateLabel(trendReading,
              "Latest (" + dateText(latestAt, "%I:%M %p") + "): " +
                  fmtMetric(trendMetric, latest));

  if (count == 1) {
    updateLabel(trendReading, "Waiting for another reading to draw the trend.");
    return;
  }

  if (fabsf(hi - lo) < 0.01f) {
    lo -= 1.0f;
    hi += 1.0f;
  }

  int padX = 12;
  int padY = 16;
  int plotW = w - 2 * padX;
  int plotH = h - 2 * padY;
  time_t maxGap = 2 * (weatherPollIntervalMs / 1000);

  bool hasPrev = false;
  time_t prevAt = 0;
  lv_point_t prevPt{};
  int segIdx = 0;

  uint32_t lineCol = (trendMetric == localWeather::Pressure) ? AMBER
                     : (trendMetric == localWeather::Rain)   ? BLUE
                                                             : CYAN;

  for (unsigned i = 0; i < displayedHistory.count; ++i) {
    const auto &s = displayedHistory.at(i);
    if (s.at < start || s.at > end) continue;
    float v = s.values[trendMetric];
    if (!std::isfinite(v)) {
      hasPrev = false;
      continue;
    }
    float xFrac = static_cast<float>(s.at - start) / localWeather::historySeconds;
    float yFrac = (v - lo) / (hi - lo);
    lv_point_t pt;
    pt.x = padX + static_cast<lv_coord_t>(xFrac * plotW);
    pt.y = padY + plotH - static_cast<lv_coord_t>(yFrac * plotH);

    if (hasPrev && (s.at - prevAt) <= maxGap &&
        segIdx < static_cast<int>(localWeather::sampleCount - 1)) {
      trendSegments[segIdx][0] = prevPt;
      trendSegments[segIdx][1] = pt;
      lv_obj_t *ln = lv_line_create(trendChart);
      lv_line_set_points(ln, trendSegments[segIdx], 2);
      lv_obj_set_style_line_color(ln, color(lineCol), 0);
      lv_obj_set_style_line_width(ln, 3, 0);
      lv_obj_set_style_line_rounded(ln, true, 0);
      ++segIdx;
    }
    prevPt = pt;
    prevAt = s.at;
    hasPrev = true;
  }
}

void selectTrendMetric(lv_event_t *e) {
  int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (idx >= 0 && idx < localWeather::MetricCount) {
    trendMetric = static_cast<localWeather::Metric>(idx);
    drawTrend();
  }
}

void openTrends(lv_event_t *e) {
  if (navigation != 0) return;
  if (e && lv_event_get_user_data(e)) {
    trendMetric = static_cast<localWeather::Metric>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  }
  lv_obj_t *m = newModal("Weather Trends (24h)");
  activePanel = WeatherPanel::Trends;

  lv_obj_t *card = box(m, 20, 64, kScreenW - 40, 396, CARD, 16, BORDER, 1);

  const char *pillNames[] = {"Temp", "Pressure", "Wind Gust", "24h Rain"};
  int px = 16;
  for (int i = 0; i < localWeather::MetricCount; ++i) {
    int pw = (i == 2) ? 108 : 92;
    trendPills[i] = button(card, pillNames[i], px, 14, pw, 32, CARD_ALT, TEXT,
                           selectTrendMetric,
                           reinterpret_cast<void *>(static_cast<intptr_t>(i)),
                           16, BORDER, 1);
    px += pw + 8;
  }

  trendReading = label(card, "Tap chart to inspect reading", &lv_font_montserrat_14,
                       CYAN, 430, 21);

  trendChart = box(card, 16, 58, kScreenW - 72, 286, BG, 12, BORDER, 1);
  lv_obj_add_flag(trendChart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(trendChart, pickTrendPoint, LV_EVENT_CLICKED, nullptr);

  trendScale = label(card, "", &lv_font_montserrat_12, MUTED, 18, 358);

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  displayedHistory = weatherHistory;
  xSemaphoreGive(stateMutex);
  drawTrend();
}

void weatherDetails(lv_event_t *event) {
  if (navigation != 0) return;
  int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  const Weather &w = snapshot.weather;

  const char *titles[] = {"Temperature", "Humidity", "Wind", "Pressure",
                          "Rainfall",    "Light / Solar", "Air Quality"};
  if (idx < 0 || idx > 6) return;

  lv_obj_t *m = newModal(titles[idx]);
  lv_obj_t *card = box(m, 24, 68, kScreenW - 48, 388, CARD, 16, BORDER, 1);

  String body;
  localWeather::Metric chartMetric = localWeather::Temperature;
  bool hasTrend = false;

  switch (idx) {
    case 0:
      body = "Current: " + tempText(w.temp, 1) + "\nDaily High: " +
             tempText(w.max, 1) + "\nDaily Low: " + tempText(w.min, 1) +
             "\nDew Point: " + tempText(w.dew, 1);
      chartMetric = localWeather::Temperature;
      hasTrend = true;
      break;
    case 1:
      body = "Relative Humidity: " + fmt(w.hum, 0, "%") + "\nDew Point: " +
             tempText(w.dew, 1);
      break;
    case 2:
      body = "Wind Speed: " + speedText(w.wind) + "\nDirection: " +
             (std::isfinite(w.dir) ? String(core::compass(w.dir)) + " (" +
                                         fmt(w.dir, 0, "°") + ")"
                                   : "--") +
             "\nGust: " + speedText(w.gust) + "\n10m Peak Gust: " +
             speedText(w.gust10);
      chartMetric = localWeather::Gust;
      hasTrend = true;
      break;
    case 3:
      body = "Barometric Pressure: " + pressText(w.press);
      chartMetric = localWeather::Pressure;
      hasTrend = true;
      break;
    case 4:
      body = "24h Rolling Rain: " + rainText(w.rain) + "\nHourly Rain: " +
             rainText(w.hour) + "\nTotal Rain: " + rainText(w.total) +
             "\nRain Tips: " + String(w.tips) + "\nRaining Now: " +
             (w.raining ? "Yes" : "No");
      chartMetric = localWeather::Rain;
      hasTrend = true;
      break;
    case 5:
      body = "Light Level: " + fmt(w.lux, 0, " lx") + "\nSolar Reading: " +
             fmt(w.solar, 1);
      break;
    case 6:
      body = "Air Quality Index (ENS160): " +
             (std::isfinite(w.aqi) ? fmt(w.aqi, 0) : "--") +
             "\neCO2: " + fmt(w.eco2, 0, " ppm") + "\nTVOC: " +
             fmt(w.tvoc, 0, " ppb");
      break;
  }

  lv_obj_t *lbl = label(card, body, &lv_font_montserrat_18, TEXT, 28, 28);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, kScreenW - 104);
  lv_obj_set_style_text_line_space(lbl, 10, 0);

  if (hasTrend) {
    button(card, "View 24h Trend", 28, 316, 180, 42, CYAN, BG, openTrends,
           reinterpret_cast<void *>(static_cast<intptr_t>(chartMetric)), 21,
           CYAN, 0);
  }
}

void updateToday(const core::Events &todayAgenda, bool todayStale) {
  if (!todayStation || !todayEvents || !todayForecast) return;

  const Weather &w = snapshot.weather;
  String stText = "Current: " + tempText(w.temp, 1) + "   •   High: " +
                  tempText(w.max, 0) + "   •   Low: " + tempText(w.min, 0);
  updateLabel(todayStation, stText);

  String evText;
  if (todayAgenda.empty()) {
    evText = snapshot.calendarLoaded ? "No remaining events today."
                                     : snapshot.calendarError;
  } else {
    int shown = 0;
    for (const auto &e : todayAgenda) {
      if (shown >= 2) break;
      String timePart = e.allDay ? "All day" : dateText(e.start, "%I:%M %p");
      evText += "• " + timePart + " — " +
                String(e.title.empty() ? "(No title)" : e.title.c_str()) + "\n";
      ++shown;
    }
    if (todayStale) evText += "(Calendar syncing...)";
  }
  updateLabel(todayEvents, evText);

  String fcText;
  if (!shownForecast.error.isEmpty()) {
    fcText = shownForecast.error;
  } else if (shownForecast.hours.empty()) {
    fcText = settings.postalCode.isEmpty()
                 ? "Set a Canadian postal code in Settings for local forecast."
                 : "Waiting for Environment Canada forecast...";
  } else {
    int count = 0;
    for (const auto &h : shownForecast.hours) {
      if (count >= 4) break;
      fcText += dateText(h.at, "%I %p") + ": " + tempText(h.temperature, 0) +
                "  (" + String(h.precipitation) + "% precip)  " + h.condition +
                "\n";
      ++count;
    }
  }
  updateLabel(todayForecast, fcText);
  if (todayAccumulation) {
    updateLabel(todayAccumulation, shownForecast.accumulationSummary);
  }
}

void openToday(lv_event_t *) {
  if (navigation != 0) return;
  lv_obj_t *m = newModal("Today at a Glance");
  activePanel = WeatherPanel::Today;
  todayOverviewOpen.store(true);

  lv_obj_t *topCard = box(m, 20, 64, kScreenW - 40, 68, CARD, 14, BORDER, 1);
  label(topCard, "STATION SUMMARY", &lv_font_montserrat_12, MUTED, 16, 12);
  todayStation = label(topCard, "", &lv_font_montserrat_18, CYAN, 16, 32);

  lv_obj_t *leftCard = box(m, 20, 144, 370, 316, CARD, 14, BORDER, 1);
  label(leftCard, "UPCOMING TODAY", &lv_font_montserrat_12, MUTED, 16, 14);
  todayEvents = label(leftCard, "", &lv_font_montserrat_14, TEXT, 16, 42);
  lv_label_set_long_mode(todayEvents, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(todayEvents, 338);
  lv_obj_set_style_text_line_space(todayEvents, 8, 0);

  lv_obj_t *rightCard = box(m, 404, 144, 376, 316, CARD, 14, BORDER, 1);
  label(rightCard, "HOURLY FORECAST", &lv_font_montserrat_12, MUTED, 16, 14);
  todayForecast = label(rightCard, "", &lv_font_montserrat_14, TEXT, 16, 42);
  lv_label_set_long_mode(todayForecast, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(todayForecast, 344);
  lv_obj_set_style_text_line_space(todayForecast, 8, 0);

  todayAccumulation =
      label(rightCard, "", &lv_font_montserrat_12, AMBER, 16, 250);
  lv_label_set_long_mode(todayAccumulation, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(todayAccumulation, 344);

  core::Events todayAgenda;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  shownForecast = localForecast;
  todayAgenda = shared.todayEvents;
  xSemaphoreGive(stateMutex);
  updateToday(todayAgenda, false);
}

bool forecastFresh(const LocalForecast &forecast, time_t now) {
  return forecast.error.isEmpty() && forecast.checked != 0 &&
         now >= forecast.checked && (now - forecast.checked) <= 11 * 60;
}

bool activeAlert(const WeatherAlert &alert, time_t now) {
  return !alert.expires || alert.expires > now;
}

void updateAlerts() {
  if (!alertsBody) return;
  time_t now = time(nullptr);
  String out;
  if (!shownForecast.error.isEmpty()) {
    out = shownForecast.error;
  } else if (shownForecast.alerts.empty()) {
    out = "No active Environment Canada alerts for " +
          (shownForecast.location.isEmpty() ? settings.postalCode
                                            : shownForecast.location) +
          ".";
  } else {
    for (const auto &a : shownForecast.alerts) {
      if (!activeAlert(a, now)) continue;
      out += "• " + a.title;
      if (a.expires) out += " (Expires " + dateText(a.expires, "%I:%M %p") + ")";
      out += "\n\n";
    }
    if (out.isEmpty()) out = "No active alerts right now.";
  }
  updateLabel(alertsBody, out);
}

void openAlerts(lv_event_t *) {
  if (navigation != 0) return;
  lv_obj_t *m = newModal("Weather Alerts");
  activePanel = WeatherPanel::Alerts;

  lv_obj_t *card = box(m, 20, 64, kScreenW - 40, 396, CARD, 16, BORDER, 1);
  alertsBody = label(card, "", &lv_font_montserrat_16, TEXT, 22, 22);
  lv_label_set_long_mode(alertsBody, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(alertsBody, kScreenW - 84);
  lv_obj_set_style_text_line_space(alertsBody, 6, 0);

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  shownForecast = localForecast;
  xSemaphoreGive(stateMutex);
  updateAlerts();
}

void updatePrintRoom() {
  if (!printPrinter) return;
  const PrintRoom &pr = shownPrintRoom;
  if (!pr.loaded) {
    updateLabel(printPrinter, "Print Room");
    updateLabel(printStatus, pr.error.isEmpty() ? "Waiting for bridge..." : pr.error);
    updateLabel(printJob, "");
    updateLabel(printPercent, "--%");
    if (printArc) lv_arc_set_value(printArc, 0);
    updateLabel(printEta, "");
    updateLabel(printConnection, settings.printRoomUrl.isEmpty()
                                     ? "Not configured in Settings"
                                     : settings.printRoomUrl);
    return;
  }

  updateLabel(printPrinter, pr.printer.isEmpty() ? "3D Printer" : pr.printer);
  String st = pr.status;
  if (!pr.stage.isEmpty()) st += " • " + pr.stage;
  updateLabel(printStatus, st);
  updateLabel(printJob, pr.job.isEmpty() ? "Idle" : pr.job);

  int pct = std::isfinite(pr.progress) ? std::clamp(static_cast<int>(lroundf(pr.progress)), 0, 100) : 0;
  updateLabel(printPercent, String(pct) + "%");
  if (printArc) lv_arc_set_value(printArc, pct);

  String eta;
  if (pr.remainingMinutes > 0) {
    const int totalMinutes = static_cast<int>(pr.remainingMinutes);
    int hrs = totalMinutes / 60;
    int mins = totalMinutes % 60;
    eta = (hrs > 0 ? String(hrs) + "h " : "") + String(mins) + "m left";
    if (pr.finishAt) eta += "  •  ETA " + dateText(pr.finishAt, "%I:%M %p");
  }
  if (pr.layers > 0) {
    if (!eta.isEmpty()) eta += "\n";
    eta += "Layer " + String(pr.layer) + " / " + String(pr.layers);
  }
  updateLabel(printEta, eta);

  if (printMetrics[0]) updateLabel(printMetrics[0], tempText(pr.nozzle, 0));
  if (printMetrics[1]) updateLabel(printMetrics[1], tempText(pr.bed, 0));
  if (printMetrics[2]) updateLabel(printMetrics[2], tempText(pr.chamber, 0));
  if (printMetrics[3]) {
    String roomStrip = "Room " + tempText(pr.roomTemp, 1) + "  •  " +
                       fmt(pr.humidity, 0, "% RH") + "  •  PM2.5 " +
                       fmt(pr.pm25, 0, " ug/m3");
    updateLabel(printMetrics[3], roomStrip);
  }
  updateLabel(printConnection, pr.error.isEmpty() ? "Connected" : pr.error);
}

void openPrintRoom(lv_event_t *) {
  if (navigation != 0) return;
  lv_obj_t *m = newModal("Print Room");
  activePanel = WeatherPanel::PrintRoom;
  printRoomOpen.store(true);

  lv_obj_t *card = box(m, 20, 64, kScreenW - 40, 396, CARD, 16, BORDER, 1);

  // Left Circular Progress Ring (lv_arc)
  printArc = lv_arc_create(card);
  lv_obj_set_size(printArc, 190, 190);
  lv_obj_set_pos(printArc, 44, 52);
  lv_arc_set_rotation(printArc, 270);
  lv_arc_set_bg_angles(printArc, 0, 360);
  lv_arc_set_range(printArc, 0, 100);
  lv_obj_remove_style(printArc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(printArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(printArc, color(CARD_ALT), LV_PART_MAIN);
  lv_obj_set_style_arc_width(printArc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_color(printArc, color(CYAN), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(printArc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(printArc, true, LV_PART_INDICATOR);

  printPercent = label(printArc, "0%", &lv_font_montserrat_32, TEXT, 0, 0);
  lv_obj_center(printPercent);

  // Right Info Column
  printPrinter = label(card, "3D Printer", &lv_font_montserrat_24, TEXT, 274, 34);
  printStatus = label(card, "", &lv_font_montserrat_16, EMERALD, 274, 70);
  printJob = label(card, "", &lv_font_montserrat_14, MUTED, 274, 98);
  lv_obj_set_width(printJob, 456);
  lv_label_set_long_mode(printJob, LV_LABEL_LONG_DOT);

  printEta = label(card, "", &lv_font_montserrat_16, TEXT, 274, 130);
  lv_obj_set_style_text_line_space(printEta, 6, 0);

  // Nozzle / Bed / Chamber pills
  const char *pLabels[] = {"Nozzle", "Bed", "Chamber"};
  for (int i = 0; i < 3; ++i) {
    lv_obj_t *pill =
        box(card, 274 + i * 154, 198, 142, 68, CARD_ALT, 12, BORDER, 1);
    label(pill, pLabels[i], &lv_font_montserrat_12, MUTED, 14, 10);
    printMetrics[i] = label(pill, "--", &lv_font_montserrat_20, TEXT, 14, 32);
  }

  // Bottom Room Air Strip
  lv_obj_t *strip = box(card, 24, 304, kScreenW - 88, 50, BG, 12, BORDER, 1);
  printMetrics[3] =
      label(strip, "Room --", &lv_font_montserrat_14, TEXT, 18, 16);
  printConnection = label(card, "", &lv_font_montserrat_12, MUTED, 24, 368);

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  shownPrintRoom = printRoom;
  xSemaphoreGive(stateMutex);
  updatePrintRoom();
}

String stationSignal(const Weather &weather) {
  if (weather.rssi.isEmpty()) return "Unavailable";
  char *end = nullptr;
  long val = strtol(weather.rssi.c_str(), &end, 10);
  if (end && *end == '\0' && val < 0) return weather.rssi + " dBm";
  return weather.rssi;
}

void updateStationHealth() {
  if (!stationHealth) return;
  const Weather &w = snapshot.weather;
  String txt = "Station IP: " + (w.ip.isEmpty() ? String("--") : w.ip) +
               "\nWi-Fi SSID: " + (w.ssid.isEmpty() ? String("--") : w.ssid) +
               "\nSignal (RSSI): " + stationSignal(w) +
               "\nFirmware: " + (w.firmware.isEmpty() ? String("--") : w.firmware) +
               "\nUptime: " + (w.uptime.isEmpty() ? String("--") : w.uptime) +
               "\nSupply Voltage (VCC): " + fmt(w.vcc, 2, " V") +
               "\nFree Heap: " + String(w.freeHeap) + " bytes" +
               "\nSensors: " + (w.sensors.isEmpty() ? String("--") : w.sensors) +
               "\nLast Reading: " + (w.stationTime.isEmpty() ? String("--") : w.stationTime);
  updateLabel(stationHealth, txt);
}

void openStationHealth(lv_event_t *) {
  if (navigation != 0) return;
  lv_obj_t *m = newModal("Station Health");
  activePanel = WeatherPanel::StationHealth;
  lv_obj_t *card = box(m, 20, 64, kScreenW - 40, 396, CARD, 16, BORDER, 1);
  stationHealth = label(card, "", &lv_font_montserrat_16, TEXT, 24, 24);
  lv_obj_set_style_text_line_space(stationHealth, 8, 0);
  updateStationHealth();
}

void updatePrintBanner(const PrintRoom &room, uint32_t now) {
  if (!printBanner) return;
  String caption;
  if (room.loaded && room.error.isEmpty() && (now - room.received) < 20000) {
    String st = room.status;
    st.toLowerCase();
    if (st == "finish" || st == "finished" || st == "complete" ||
        st == "completed") {
      caption = "Print complete";
    } else if (st == "pause" || st == "paused") {
      caption = "Print paused";
    }
  }
  if (caption.isEmpty()) {
    lv_obj_add_flag(printBanner, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_t *lbl = lv_obj_get_child(printBanner, 0);
    if (lbl) updateLabel(lbl, caption);
    lv_obj_clear_flag(printBanner, LV_OBJ_FLAG_HIDDEN);
  }
}

// ============================================================================
// Settings & USB Setup
// ============================================================================

void focusField(lv_event_t *e) {
  if (!keyboard) return;
  lv_obj_t *target = lv_event_get_target(e);
  lv_keyboard_set_textarea(keyboard, target);
  lv_obj_scroll_to_view(target, LV_ANIM_ON);
}

lv_obj_t *input(lv_obj_t *p, const char *name, const String &value, int y,
                bool secret = false, int maxLen = 128) {
  label(p, name, &lv_font_montserrat_14, MUTED, 12, y);
  lv_obj_t *ta = lv_textarea_create(p);
  lv_obj_set_pos(ta, 12, y + 24);
  lv_obj_set_size(ta, 704, 46);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, maxLen);
  lv_textarea_set_password_mode(ta, secret);
  lv_textarea_set_text(ta, value.c_str());
  lv_obj_set_style_bg_color(ta, color(CARD_ALT), 0);
  lv_obj_set_style_text_color(ta, color(TEXT), 0);
  lv_obj_set_style_border_color(ta, color(BORDER), 0);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_radius(ta, 10, 0);
  lv_obj_add_event_cb(ta, focusField, LV_EVENT_FOCUSED, nullptr);
  return ta;
}

void applyEditorSettings() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  settings = draft;
  if (editKind == 5) {
    printRoom = PrintRoom{};
  } else if (editKind == 2 || editKind == 3) {
    shared.events.clear();
    shared.todayEvents.clear();
    shared.todayWeek = 0;
    shared.calendarLoaded = false;
    shared.calendarError = settings.calendarUrl.isEmpty()
                               ? "Calendar not configured"
                               : "Loading calendar...";
  } else if (editKind == 1) {
    shared.weather = Weather{};
    shared.weatherError = "Connecting to station...";
  }
  xSemaphoreGive(stateMutex);

  saveSettings();
  if (editKind == 2 || editKind == 3) {
    calendarRefreshRequested.store(true);
    setenv("TZ", settings.timezone.c_str(), 1);
    tzset();
  }
  closeModal();
  redraw = true;
  calendarDirty = true;
}

void startCalendarUsb(lv_event_t *) {
  lv_obj_t *m = newModal("Google Calendar over USB");
  lv_obj_t *card = box(m, 24, 68, kScreenW - 48, 386, CARD, 16, BORDER, 1);
  label(card,
        "Connect your display to a computer via USB serial (115200 baud)\n"
        "and send a single line in the format:\n\n"
        "CALENDAR <https://script.google.com/macros/s/.../exec> <hex-token>",
        &lv_font_montserrat_14, TEXT, 24, 24);
  calendarUsbStatus = label(card, "Waiting for the URL and token (2 minutes).",
                            &lv_font_montserrat_16, CYAN, 24, 160);
  lv_obj_set_width(calendarUsbStatus, kScreenW - 96);
  lv_label_set_long_mode(calendarUsbStatus, LV_LABEL_LONG_WRAP);
  calendarUsbStarted = millis();
  clearCalendarUsbLine();
}

void editorSave(lv_event_t *) {
  auto val = [](int idx) {
    if (!fields[idx]) return String("");
    String s = lv_textarea_get_text(fields[idx]);
    s.trim();
    return s;
  };
  auto showErr = [](const char *msg) {
    static const char *btns[] = {"OK", ""};
    lv_obj_t *mb = lv_msgbox_create(modal, "Invalid Input", msg, btns, false);
    lv_obj_center(mb);
  };

  if (editKind == 0) {
    draft.ssid = val(0);
    draft.password = fields[1] ? String(lv_textarea_get_text(fields[1])) : "";
  } else if (editKind == 1) {
    String u = val(0);
    if (!u.startsWith("http://")) {
      showErr("Station URL must start with http://");
      return;
    }
    draft.station = u;
  } else if (editKind == 2) {
    String u = val(0), tok = val(1);
    if (!u.isEmpty() && (!u.startsWith("https://script.google.com/macros/s/") ||
                         !u.endsWith("/exec") || tok.length() < 32)) {
      showErr("Check the Apps Script /exec URL and token (32+ chars).");
      return;
    }
    draft.calendarUrl = u;
    draft.calendarToken = tok;
  } else if (editKind == 4) {
    char prefix[4]{};
    String p = val(0);
    if (!p.isEmpty() && !localWeather::postalPrefix(p.c_str(), prefix)) {
      showErr("Enter a valid Canadian postal code (e.g. R0J 1H0).");
      return;
    }
    draft.postalCode = p.isEmpty() ? "" : String(prefix);
  } else if (editKind == 5) {
    String u = val(0), k = val(1);
    if (u.isEmpty()) k = "";
    if (!validPrintRoomUrl(u) || !validPrintRoomKey(k)) {
      showErr("Check the Print Room bridge URL and API key.");
      return;
    }
    draft.printRoomUrl = u;
    draft.printRoomKey = k;
  }
  applyEditorSettings();
}

void timezoneSelect(lv_event_t *e) {
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= kTzCount) return;

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  settings.timezone = tzCodes[i];
  shared.calendarLoaded = false;
  shared.events.clear();
  shared.todayEvents.clear();
  shared.todayWeek = 0;
  shared.calendarError = "Loading calendar...";
  xSemaphoreGive(stateMutex);

  setenv("TZ", settings.timezone.c_str(), 1);
  tzset();
  saveSettings();
  calendarRefreshRequested.store(true);
  closeModal();
  redraw = true;
  calendarDirty = true;
}

void edit(lv_event_t *e) {
  if (navigation != 0) return;
  editKind = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  draft = settings;

  if (editKind == 3) {
    lv_obj_t *m = newModal("Choose your time zone");
    lv_obj_t *list = box(m, 24, 66, kScreenW - 48, 392, CARD, 14, BORDER, 1);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kTzCount; ++i) {
      bool active = (settings.timezone == tzCodes[i]);
      button(list, tzNames[i], 14, 12 + i * 44, kScreenW - 76, 38,
             active ? CYAN_DARK : CARD_ALT, active ? CYAN : TEXT, timezoneSelect,
             reinterpret_cast<void *>(static_cast<intptr_t>(i)), 10,
             active ? CYAN : BORDER, 1);
    }
    lv_obj_invalidate(modal);
    return;
  }

  const char *titles[] = {"Wi-Fi Network",      "Weather Station",
                          "Google Calendar",    "Time Zone",
                          "Canadian Postal Code", "Print Room Bridge"};
  lv_obj_t *m = newModal(titles[editKind]);
  button(m, "Save", kScreenW - 196, 10, 84, 32, CYAN, BG, editorSave, nullptr,
         16, CYAN, 0);
  if (editKind == 2) {
    if (modalBatteryLabel) lv_obj_set_pos(modalBatteryLabel, 380, 18);
    button(m, "Set up over USB", kScreenW - 360, 10, 152, 32, CARD_ALT, CYAN,
           startCalendarUsb, nullptr, 16, CYAN, 1);
  }

  form = box(m, 24, 62, kScreenW - 48, 180, CARD, 14, BORDER, 1);
  lv_obj_add_flag(form, LV_OBJ_FLAG_SCROLLABLE);
  keyboard = lv_keyboard_create(m);
  lv_obj_set_pos(keyboard, 0, 252);
  lv_obj_set_size(keyboard, kScreenW, 228);

  if (editKind == 0) {
    fields[0] = input(form, "Wi-Fi SSID", draft.ssid, 10, false, 32);
    fields[1] = input(form, "Password", draft.password, 92, true, 63);
  } else if (editKind == 1) {
    fields[0] = input(form, "Station URL (http://...)", draft.station, 10);
  } else if (editKind == 2) {
    fields[0] = input(form, "Apps Script /exec URL", draft.calendarUrl, 10, false, 400);
    fields[1] = input(form, "Token", draft.calendarToken, 92, true, 128);
  } else if (editKind == 4) {
    fields[0] = input(form, "Postal Code (e.g. R0J 1H0)", draft.postalCode, 10, false, 16);
  } else if (editKind == 5) {
    fields[0] = input(form, "Bridge URL", draft.printRoomUrl, 10, false, 256);
    fields[1] = input(form, "API Key", draft.printRoomKey, 92, true, 128);
  }
  if (fields[0]) lv_keyboard_set_textarea(keyboard, fields[0]);
}

void unitsChanged(lv_event_t *e) {
  bool imp = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  settings.imperial = imp;
  xSemaphoreGive(stateMutex);
  saveSettings();
  redraw = true;
}

void brightnessChanged(lv_event_t *e) {
  int val = lv_slider_get_value(lv_event_get_target(e));
  uiSetDimming(val);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    settings.brightness = val;
    xSemaphoreGive(stateMutex);
    saveSettings();
  }
}

void selectScreenTimeout(lv_event_t *e) {
  uint32_t sec =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  settings.screenTimeoutSeconds = sec;
  xSemaphoreGive(stateMutex);
  saveSettings();
  closeModal();
  redraw = true;
}

void openScreenTimeout(lv_event_t *) {
  if (navigation != 0) return;
  lv_obj_t *m = newModal("Screen timeout");
  lv_obj_t *list = box(m, 24, 66, kScreenW - 48, 392, CARD, 14, BORDER, 1);
  int i = 0;
  for (uint32_t sec : ScreenIdle::timeoutOptions) {
    bool active = (settings.screenTimeoutSeconds == sec);
    button(list, screenTimeoutLabel(sec), 18, 18 + i * 54, kScreenW - 84, 44,
           active ? CYAN_DARK : CARD_ALT, active ? CYAN : TEXT,
           selectScreenTimeout, reinterpret_cast<void *>(static_cast<uintptr_t>(sec)),
           12, active ? CYAN : BORDER, 1);
    ++i;
  }
}

void rotationChanged(lv_event_t *e) {
  bool rot = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  settings.rotateScreen = rot;
  xSemaphoreGive(stateMutex);
  saveSettings();
  lv_obj_invalidate(lv_scr_act());
}

// ============================================================================
// Calendar Callbacks & Renderers
// ============================================================================

void viewChange(lv_event_t *e) {
  int cmd = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (cmd == -2) {
    weekView = false;
    requestedWeek = 0;
  } else if (cmd == -3) {
    weekView = true;
    requestedWeek = 0;
  } else {
    requestedWeek += cmd;
  }
  calendarDirty = true;
}

void refreshCalendar(lv_event_t *) {
  calendarRefreshRequested.store(true);
  calendarDirty = true;
}

void renderCalendarSplit() {
  if (!calendarBody || !calendarTodayList) return;
  lv_obj_clean(calendarBody);
  lv_obj_clean(calendarTodayList);

  time_t now = time(nullptr);
  time_t todayStart = core::midnight(now);
  time_t sun = core::sunday(now, requestedWeek);
  shownWeek = sun;
  renderedWeek = sun;

  if (calendarDate) {
    updateLabel(calendarDate, "Week of " + dateText(sun, "%b %d"));
  }

  // Left card: Today's Agenda
  auto todaySegs = core::daySegments(snapshot.events, todayStart);
  if (todaySegs.empty()) {
    label(calendarTodayList,
          snapshot.calendarLoaded ? "No events today." : snapshot.calendarError,
          &lv_font_montserrat_14, MUTED, 14, 16);
  } else {
    int y = 8;
    for (const auto &seg : todaySegs) {
      if (seg.event >= snapshot.events.size()) continue;
      const auto &ev = snapshot.events[seg.event];
      lv_obj_t *row = box(calendarTodayList, 8, y, 200, 64, CARD_ALT, 12, BORDER, 1);
      uint32_t accent = ev.color ? ev.color : CYAN;
      box(row, 0, 0, 5, 64, accent, 3, accent, 0);

      String tStr = ev.allDay ? "All Day" : dateText(ev.start, "%I:%M %p");
      label(row, tStr, &lv_font_montserrat_12, MUTED, 14, 10);
      lv_obj_t *tl = label(row, ev.title.empty() ? "(No title)" : ev.title.c_str(),
                           &lv_font_montserrat_14, TEXT, 14, 30);
      lv_obj_set_width(tl, 174);
      lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);

      lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(row, eventDetails, LV_EVENT_CLICKED,
                          reinterpret_cast<void *>(seg.event));
      y += 74;
    }
  }

  // Right card: 7-Column Week View (Sun–Sat)
  int gridW = lv_obj_get_width(calendarBody);
  int gridH = lv_obj_get_height(calendarBody);
  int colW = (gridW - 16) / 7;
  const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

  for (int d = 0; d < 7; ++d) {
    time_t dayT = core::addDays(sun, d);
    bool isToday = (dayT == todayStart);
    int cx = 8 + d * colW;

    if (isToday) {
      box(calendarBody, cx + 2, 8, colW - 4, gridH - 16, CYAN_DARK, 10, CYAN, 1);
    } else if (d > 0) {
      box(calendarBody, cx, 12, 1, gridH - 24, BORDER, 0, BORDER, 0);
    }

    String hdr = String(days[d]) + " " + dateText(dayT, "%d");
    lv_obj_t *hl = label(calendarBody, hdr, &lv_font_montserrat_12,
                         isToday ? CYAN : MUTED, cx + 8, 16);
    (void)hl;

    auto segs = core::daySegments(snapshot.events, dayT);
    int ey = 42;
    for (const auto &seg : segs) {
      if (ey + 30 > gridH - 14) break;
      if (seg.event >= snapshot.events.size()) continue;
      const auto &ev = snapshot.events[seg.event];
      lv_obj_t *pill =
          box(calendarBody, cx + 5, ey, colW - 10, 26, CARD_ALT, 8, BORDER, 1);
      uint32_t accent = ev.color ? ev.color : CYAN;
      box(pill, 0, 0, 4, 26, accent, 2, accent, 0);
      lv_obj_t *pl = label(pill, ev.title.empty() ? "Event" : ev.title.c_str(),
                           &lv_font_montserrat_12, TEXT, 8, 5);
      lv_obj_set_width(pl, colW - 22);
      lv_label_set_long_mode(pl, LV_LABEL_LONG_DOT);

      lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(pill, eventDetails, LV_EVENT_CLICKED,
                          reinterpret_cast<void *>(seg.event));
      ey += 32;
    }
  }
}

void calendarUpdate() {
  renderCalendarSplit();
  calendarDirty = false;
  lastCalendarError = snapshot.calendarError;
}

// ============================================================================
// Weather Screen Updater
// ============================================================================

void weatherUpdate() {
  const Weather &w = snapshot.weather;
  uint32_t nowMs = millis();
  bool fresh = w.loaded && (nowMs - w.received) <= weatherStaleAfterMs;

  icons::Kind cond = icons::condition(fresh, w.raining, w.lux);
  iconStates[0] = {cond, CYAN};
  if (weatherImages[0]) lv_obj_invalidate(weatherImages[0]);

  if (fresh) {
    updateLabel(weatherValues[0], tempText(w.temp, 0));
    updateLabel(weatherSub[0], "H: " + tempText(w.max, 0) + "  L: " +
                                   tempText(w.min, 0) + "\nDew Point " +
                                   tempText(w.dew, 0));
    updateLabel(conditionLabel, icons::caption(cond));
    updateLabel(statusLabel, "");
  } else {
    updateLabel(weatherValues[0], "--");
    updateLabel(weatherSub[0], "H: --  L: --\nDew Point --");
    updateLabel(conditionLabel, "Unavailable");
    updateLabel(statusLabel, snapshot.weatherError);
  }

  // 1: Humidity
  iconStates[1] = {icons::Kind::Humidity, BLUE};
  updateLabel(weatherValues[1], fresh ? fmt(w.hum, 0, "%") : "--");
  updateLabel(weatherSub[1], fresh ? ("Dew " + tempText(w.dew, 0)) : "");

  // 2: Wind
  iconStates[2] = {icons::Kind::Wind, CYAN};
  String windVal = fresh ? speedText(w.wind) : "--";
  if (fresh && std::isfinite(w.dir)) windVal += " " + String(core::compass(w.dir));
  updateLabel(weatherValues[2], windVal);
  updateLabel(weatherSub[2], fresh ? ("Gust " + speedText(w.gust)) : "");

  // 3: Pressure
  iconStates[3] = {icons::Kind::Gauge, MUTED};
  updateLabel(weatherValues[3], fresh ? pressText(w.press) : "--");
  updateLabel(weatherSub[3], "Barometric");

  // 4: Rainfall
  iconStates[4] = {w.raining ? icons::Kind::Rain : icons::Kind::DryRain, BLUE};
  updateLabel(weatherValues[4], fresh ? rainText(w.rain) : "--");
  updateLabel(weatherSub[4], fresh ? (rainText(w.hour) + "/hr") : "");

  // 5: Solar / Light
  iconStates[5] = {icons::Kind::Light, AMBER};
  updateLabel(weatherValues[5], fresh ? fmt(w.lux, 0, " lx") : "--");
  updateLabel(weatherSub[5], fresh ? ("Solar " + fmt(w.solar, 0)) : "");

  // 6: Air Quality
  uint32_t aqiCol = EMERALD;
  String aqiTag = "Good";
  if (fresh && std::isfinite(w.aqi)) {
    if (w.aqi >= 4) {
      aqiCol = RED;
      aqiTag = "Poor";
    } else if (w.aqi >= 3) {
      aqiCol = AMBER;
      aqiTag = "Moderate";
    }
  }
  iconStates[6] = {icons::Kind::Air, aqiCol};
  updateLabel(weatherValues[6],
              fresh ? ("AQI " + fmt(w.aqi, 0) + " • " + aqiTag) : "--");
  updateLabel(weatherSub[6], fresh ? ("eCO2 " + fmt(w.eco2, 0, " ppm")) : "");

  for (int i = 1; i < 7; ++i) {
    if (weatherImages[i]) lv_obj_invalidate(weatherImages[i]);
  }
}

// ============================================================================
// Screen Builders
// ============================================================================

void weatherScreen() {
  addStatusBar(root);

  // Optional Print Complete/Paused Banner in status bar
  printBanner = box(root, 260, 6, 170, 26, CYAN_DARK, 13, CYAN, 1);
  lv_obj_t *pbLbl = label(printBanner, "", &lv_font_montserrat_12, CYAN, 0, 0);
  lv_obj_center(pbLbl);
  lv_obj_add_flag(printBanner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(printBanner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(printBanner, openPrintRoom, LV_EVENT_CLICKED, nullptr);

  // Left Hero Condition Card
  lv_obj_t *hero = box(root, 16, 46, 274, 348, CARD, 16, BORDER, 1);
  lv_obj_add_flag(hero, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(hero, weatherDetails, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(0));

  weatherImages[0] = icons::create(hero, 87, 24, 100, &iconStates[0]);
  weatherValues[0] = label(hero, "--", &lv_font_montserrat_48, TEXT, 0, 134);
  lv_obj_set_width(weatherValues[0], 274);
  lv_obj_set_style_text_align(weatherValues[0], LV_TEXT_ALIGN_CENTER, 0);

  weatherSub[0] = label(hero, "", &lv_font_montserrat_16, MUTED, 0, 198);
  lv_obj_set_width(weatherSub[0], 274);
  lv_obj_set_style_text_align(weatherSub[0], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(weatherSub[0], 6, 0);

  conditionLabel = label(hero, "", &lv_font_montserrat_18, TEXT, 0, 262);
  lv_obj_set_width(conditionLabel, 274);
  lv_obj_set_style_text_align(conditionLabel, LV_TEXT_ALIGN_CENTER, 0);

  statusLabel = label(hero, "", &lv_font_montserrat_12, AMBER, 16, 304);
  lv_obj_set_width(statusLabel, 242);
  lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);

  // Right 3x2 Sensor Grid (Cards 1..6)
  const char *cardTitles[] = {"",         "Humidity",    "Wind",
                              "Pressure", "24h Rain",    "Solar / Light",
                              "Air Quality"};
  for (int i = 1; i <= 6; ++i) {
    int idx = i - 1;
    int col = idx % 2;
    int row = idx / 2;
    int cx = 304 + col * 244;
    int cy = 46 + row * 120;

    lv_obj_t *c = box(root, cx, cy, 236, 108, CARD, 14, BORDER, 1);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, weatherDetails, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));

    weatherImages[i] = icons::create(c, 16, 31, 44, &iconStates[i]);
    label(c, cardTitles[i], &lv_font_montserrat_14, MUTED, 74, 14);
    weatherValues[i] = label(c, "--", &lv_font_montserrat_20, TEXT, 74, 38);
    lv_obj_set_width(weatherValues[i], 150);
    lv_label_set_long_mode(weatherValues[i], LV_LABEL_LONG_DOT);
    weatherSub[i] = label(c, "", &lv_font_montserrat_12, MUTED, 74, 70);
  }

  // Bottom Sleek Pill Navigation Dock
  lv_obj_t *dock = box(root, 16, 404, kScreenW - 32, 48, CARD, 24, BORDER, 1);
  struct DockItem {
    const char *name;
    lv_event_cb_t cb;
  };
  DockItem items[] = {
      {"Today", openToday},
      {"Trends", openTrends},
      {"Alerts", openAlerts},
      {"Print Room", openPrintRoom},
      {"Station Health", openStationHealth},
  };
  int tabW = (kScreenW - 48) / 5;
  for (int i = 0; i < 5; ++i) {
    lv_obj_t *btn =
        button(dock, items[i].name, 8 + i * tabW, 6, tabW - 6, 36,
               (i == 0) ? CARD_ALT : CARD, (i == 0) ? CYAN : TEXT, items[i].cb,
               nullptr, 18, (i == 0) ? CYAN : CARD, (i == 0) ? 1 : 0);
    if (i == 2) {
      alertsButton = btn;
      alertsBtnLabel = lv_obj_get_child(btn, 0);
    }
  }

  addPageDots(root, WeatherPage);
  weatherUpdate();
}

void calendarScreen() {
  addStatusBar(root);

  // Header row
  label(root, "Calendar", &lv_font_montserrat_28, TEXT, 20, 46);
  calendarDate = label(root, "", &lv_font_montserrat_14, MUTED, 168, 56);

  // Pill navigation buttons
  button(root, "<", 424, 46, 40, 34, CARD, TEXT, viewChange,
         reinterpret_cast<void *>(-1), 17, BORDER, 1);
  btnTodayView = button(root, "Today", 472, 46, 78, 34, CARD, TEXT, viewChange,
                        reinterpret_cast<void *>(-2), 17, BORDER, 1);
  btnWeekView = button(root, "This Week", 558, 46, 98, 34, CYAN_DARK, CYAN,
                       viewChange, reinterpret_cast<void *>(-3), 17, CYAN, 1);
  button(root, "Refresh", 664, 46, 76, 34, CARD, TEXT, refreshCalendar, nullptr,
         17, BORDER, 1);
  button(root, ">", 748, 46, 40, 34, CARD, TEXT, viewChange,
         reinterpret_cast<void *>(1), 17, BORDER, 1);

  // Left card: Today's Agenda
  lv_obj_t *leftCard = box(root, 16, 90, 224, 366, CARD, 16, BORDER, 1);
  label(leftCard, "Today", &lv_font_montserrat_18, TEXT, 16, 12);
  calendarTodayList =
      box(leftCard, 4, 40, 216, 320, CARD, 12, CARD, 0);
  lv_obj_add_flag(calendarTodayList, LV_OBJ_FLAG_SCROLLABLE);

  // Right card: 7-Column Week View
  calendarBody = box(root, 250, 90, 534, 366, CARD, 16, BORDER, 1);

  addPageDots(root, CalendarPage);
  calendarUpdate();
}

void settingsScreen() {
  addStatusBar(root);
  label(root, "Settings", &lv_font_montserrat_24, TEXT, 20, 42);

  contentScroller = box(root, 16, 76, kScreenW - 32, 380, BG, 0, BG, 0);
  lv_obj_add_flag(contentScroller, LV_OBJ_FLAG_SCROLLABLE);

  struct RowDef {
    const char *title;
    String value;
    int kind;
  };
  RowDef rows[] = {
      {"Wi-Fi Network", settings.ssid.isEmpty() ? "Tap to connect" : settings.ssid, 0},
      {"Weather Station", settings.station, 1},
      {"Google Calendar", settings.calendarUrl.isEmpty() ? "Tap to link" : "Configured", 2},
      {"Display Brightness", "", -1},
      {"Imperial Units (°F / mph / in)", "", -2},
      {"Time Zone", timezoneName(), 3},
      {"Screen Timeout", screenTimeoutLabel(settings.screenTimeoutSeconds), -3},
      {"Canadian Postal Code",
       settings.postalCode.isEmpty() ? "Tap to set location" : settings.postalCode, 4},
      {"Print Room Bridge",
       settings.printRoomUrl.isEmpty() ? "Tap to set bridge" : settings.printRoomUrl, 5},
      {"Rotate Screen 180°", "", -4},
  };

  for (int i = 0; i < 10; ++i) {
    int ry = i * kSettingsRowPitch;
    lv_obj_t *row = box(contentScroller, 0, ry, kScreenW - 40,
                        kSettingsRowHeight, CARD, 10, BORDER, 1);
    label(row, rows[i].title, &lv_font_montserrat_14, TEXT, 16, 10);

    if (rows[i].kind == -1) {
      lv_obj_t *sl = lv_slider_create(row);
      lv_obj_set_size(sl, 220, 10);
      lv_obj_set_pos(sl, kScreenW - 280, 14);
      lv_slider_set_range(sl, 20, 100);
      lv_slider_set_value(sl, settings.brightness, LV_ANIM_OFF);
      lv_obj_set_style_bg_color(sl, color(CARD_ALT), LV_PART_MAIN);
      lv_obj_set_style_bg_color(sl, color(CYAN), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(sl, color(TEXT), LV_PART_KNOB);
      lv_obj_add_event_cb(sl, brightnessChanged, LV_EVENT_VALUE_CHANGED, nullptr);
      lv_obj_add_event_cb(sl, brightnessChanged, LV_EVENT_RELEASED, nullptr);
    } else if (rows[i].kind == -2 || rows[i].kind == -4) {
      lv_obj_t *sw = lv_switch_create(row);
      lv_obj_set_pos(sw, kScreenW - 106, 6);
      bool checked = (rows[i].kind == -2) ? settings.imperial : settings.rotateScreen;
      if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_set_style_bg_color(sw, color(CYAN), LV_PART_INDICATOR | LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw,
                          (rows[i].kind == -2) ? unitsChanged : rotationChanged,
                          LV_EVENT_VALUE_CHANGED, nullptr);
    } else {
      lv_obj_t *vl = label(row, rows[i].value + "  >", &lv_font_montserrat_14,
                           CYAN, kScreenW - 360, 10);
      lv_obj_set_width(vl, 300);
      lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_long_mode(vl, LV_LABEL_LONG_DOT);
      lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
      if (rows[i].kind == -3) {
        lv_obj_add_event_cb(row, openScreenTimeout, LV_EVENT_CLICKED, nullptr);
      } else {
        lv_obj_add_event_cb(row, edit, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(rows[i].kind)));
      }
    }
  }

  addPageDots(root, SettingsPage);
}

void build() {
  redraw = false;
  closeModal();

  if (root) {
    lv_obj_del(root);
    root = nullptr;
  }

  clockLabel = nullptr;
  dateHeaderLabel = nullptr;
  wifiIconLabel = nullptr;
  statusLabel = nullptr;
  batteryLabel = nullptr;
  conditionLabel = nullptr;
  alertsButton = nullptr;
  alertsBtnLabel = nullptr;
  printBanner = nullptr;
  calendarTodayList = nullptr;
  calendarBody = nullptr;
  calendarDate = nullptr;
  contentScroller = nullptr;
  for (int i = 0; i < 7; ++i) {
    weatherValues[i] = nullptr;
    weatherSub[i] = nullptr;
    weatherImages[i] = nullptr;
  }

  root = box(lv_scr_act(), 0, 0, kScreenW, kScreenH, BG, 0, BG, 0);

  if (page == SettingsPage) {
    settingsScreen();
  } else if (page == CalendarPage) {
    calendarScreen();
  } else {
    weatherScreen();
  }
}

}  // namespace

// ============================================================================
// Public Interface (declared in app.hpp)
// ============================================================================

void uiSetBatteryPercent(int percent) {
  batteryPercent = percent;
  updateBatteryLabel(batteryLabel);
  updateBatteryLabel(modalBatteryLabel);
}

void uiCalendarUsbExpire() {
  if (calendarUsbStatus && (millis() - calendarUsbStarted) >= 120000) {
    updateLabel(calendarUsbStatus,
                "USB setup expired. Close this screen and start again.");
    calendarUsbStatus = nullptr;
    clearCalendarUsbLine();
  }
}

CalendarUsbResult uiCalendarUsbReceive(char byte) {
  if (!calendarUsbStatus) return CalendarUsbResult::Pending;
  if (byte == '\r') return CalendarUsbResult::Pending;
  if (byte == '\n') {
    String line(calendarUsbLine);
    bool overflow = calendarUsbOverflow;
    clearCalendarUsbLine();

    if (overflow || !line.startsWith("CALENDAR ")) {
      updateLabel(calendarUsbStatus,
                  "Check the /exec URL and hexadecimal token, then try again.");
      return CalendarUsbResult::Invalid;
    }
    String rest = line.substring(9);
    rest.trim();
    int sp = rest.indexOf(' ');
    if (sp <= 0) {
      updateLabel(calendarUsbStatus,
                  "Check the /exec URL and hexadecimal token, then try again.");
      return CalendarUsbResult::Invalid;
    }
    String url = rest.substring(0, sp);
    String tok = rest.substring(sp + 1);
    tok.trim();

    if (!url.startsWith("https://script.google.com/macros/s/") ||
        !url.endsWith("/exec") || url.length() > 400 ||
        tok.length() < 32 || tok.length() > 128) {
      updateLabel(calendarUsbStatus,
                  "Check the /exec URL and hexadecimal token, then try again.");
      return CalendarUsbResult::Invalid;
    }

    draft = settings;
    draft.calendarUrl = url;
    draft.calendarToken = tok;
    editKind = 2;
    applyEditorSettings();
    return CalendarUsbResult::Saved;
  }

  if (byte >= 32 && byte <= 126) {
    if (calendarUsbLength + 1 < sizeof(calendarUsbLine)) {
      calendarUsbLine[calendarUsbLength++] = byte;
      calendarUsbLine[calendarUsbLength] = '\0';
    } else {
      calendarUsbOverflow = true;
    }
  }
  return CalendarUsbResult::Pending;
}

void uiSetDimming(int percent) {
  dimmingPercent = std::clamp(percent, 20, 100);
  if (!dimmer) {
    dimmer = box(lv_layer_sys(), 0, 0, kScreenW, kScreenH, 0x000000, 0, 0, 0);
    lv_obj_clear_flag(dimmer, LV_OBJ_FLAG_CLICKABLE);
  }
  if (dimmingPercent >= 100) {
    lv_obj_add_flag(dimmer, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(dimmer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(dimmer, (100 - dimmingPercent) * 2, 0);
  }
}

void uiBegin() {
  lv_obj_set_style_bg_color(lv_scr_act(), color(BG), 0);
  uiSetDimming(settings.brightness);
  build();
}

bool uiTouch(bool pressed, int x, int y) {
  static int startX = 0, startY = 0, endX = 0, endY = 0;
  if (pressed) {
    if (!touchDown) {
      startX = x;
      startY = y;
      touchDown = true;
    }
    endX = x;
    endY = y;
    return false;
  }

  if (!touchDown) return false;
  touchDown = false;

  int dx = endX - startX;
  int dy = endY - startY;
  bool dimmingDrag = (page == SettingsPage && startY >= 200 && startY <= 250);
  if (modal || dimmingDrag || abs(dx) < 100 || abs(dx) <= abs(dy) * 1.6f) {
    return false;
  }

  int dir = (dx > 0) ? -1 : 1;
  int next = static_cast<int>(page) + dir;
  if (next >= SettingsPage && next <= CalendarPage) {
    navigation = dir;
  }
  return true;
}

void uiTick() {
  if (navigation != 0) {
    page = static_cast<Page>(static_cast<int>(page) + navigation);
    navigation = 0;
    redraw = true;
  }
  if (redraw) {
    build();
    return;
  }

  uint32_t nowMs = millis();
  if (nowMs - lastTick < 1000) return;
  lastTick = nowMs;

  core::Events todayAgenda;
  bool todayStale = false;
  bool trendChanged = false;
  bool hasAlerts = false;
  bool updateCalendar = (page == CalendarPage && !modal && !touchDown);

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  snapshot.weather = shared.weather;
  snapshot.weatherError = shared.weatherError;
  snapshot.calendarLoaded = shared.calendarLoaded;
  snapshot.calendarError = shared.calendarError;

  if (page == WeatherPage) {
    if (printRoom.source == settings.printRoomUrl) {
      shownPrintRoom = printRoom;
    }
    if (localForecast.postalCode == settings.postalCode) {
      shownForecast = localForecast;
    }
    time_t nowSec = time(nullptr);
    if (forecastFresh(shownForecast, nowSec)) {
      for (const auto &a : shownForecast.alerts) {
        if (activeAlert(a, nowSec)) {
          hasAlerts = true;
          break;
        }
      }
    }
    if (activePanel == WeatherPanel::Today) {
      todayAgenda = shared.todayEvents;
      todayStale = (nowMs - shared.todayReceived) > (11 * 60 * 1000);
    }
    if (activePanel == WeatherPanel::Trends && weatherHistory.count > 0) {
      time_t newest = weatherHistory.at(weatherHistory.count - 1).at;
      if (newest != lastTrendSample) {
        displayedHistory = weatherHistory;
        lastTrendSample = newest;
        trendChanged = true;
      }
    }
  }

  if (updateCalendar) {
    snapshot.events = shared.events;
    snapshot.calendarWeek = shared.calendarWeek;
    snapshot.calendarReceived = shared.calendarReceived;
    snapshot.truncated = shared.truncated;
  }
  xSemaphoreGive(stateMutex);

  // Update status bar clock & Wi-Fi
  time_t nowSec = time(nullptr);
  if (clockLabel && nowSec > 100000) {
    String clk = dateText(nowSec, "%I:%M %p");
    if (clk.startsWith("0")) clk.remove(0, 1);
    updateLabel(clockLabel, clk);
    if (dateHeaderLabel) updateLabel(dateHeaderLabel, dateText(nowSec, "%a, %b %d"));
  }
  if (wifiIconLabel) {
    lv_obj_set_style_text_color(
        wifiIconLabel, color((WiFi.status() == WL_CONNECTED) ? CYAN : MUTED), 0);
  }

  if (page == WeatherPage) {
    weatherUpdate();
    updatePrintBanner(shownPrintRoom, nowMs);

    if (alertsButton && alertsBtnLabel) {
      lv_obj_set_style_bg_color(alertsButton, color(hasAlerts ? AMBER : CARD), 0);
      lv_obj_set_style_text_color(alertsBtnLabel, color(hasAlerts ? BG : TEXT), 0);
    }

    if (activePanel == WeatherPanel::Today) updateToday(todayAgenda, todayStale);
    if (activePanel == WeatherPanel::Alerts) updateAlerts();
    if (activePanel == WeatherPanel::Trends && trendChanged) drawTrend();
    if (activePanel == WeatherPanel::PrintRoom) updatePrintRoom();
    if (activePanel == WeatherPanel::StationHealth) updateStationHealth();
  }

  if (updateCalendar) {
    time_t curMinute = nowSec / 60;
    time_t curWeek = core::sunday(nowSec, requestedWeek);
    if (calendarDirty || curMinute != lastMinute || curWeek != shownWeek ||
        snapshot.calendarError != lastCalendarError) {
      lastMinute = curMinute;
      calendarUpdate();
    }
  }
}