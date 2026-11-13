#include "app.hpp"
#include <WiFi.h>
#include "weather_icons.hpp"
static constexpr uint32_t BG=0x081A28,CARD=0x142E40,TEXT=0xF3F7FA,MUTED=0xA9BDCA,TEAL=0x22D1BD,BLUE=0x1A73E8,INK=0x202124;
static lv_obj_t *batteryLabel, *modalBatteryLabel;
static int batteryPercent = -1;
static lv_obj_t *root,*clockLabel,*statusLabel,*calendarBody,*calendarDate,*modal=nullptr,*dimmer;
static lv_obj_t *weatherValues[7],*weatherSub[7],*weatherImages[7],*conditionLabel;
static icons::State iconStates[7];
static State snapshot;
static int page=1; // Calendar=0, Weather=1, Settings=2.
static bool weekView=false,redraw=false,calendarDirty=true;
static bool touchDown = false;
static int navigation=0,editKind=0;
static uint32_t lastTick=0,lastCalendar=0;
static time_t lastMinute=0,shownWeek=0;
static String lastCalendarError;
static lv_obj_t *fields[3],*keyboard,*form,*contentScroller=nullptr;
static bool renderedWeek=false;
static Settings draft;
enum class WeatherPanel { None, Today, Trends, Alerts, PrintRoom };
static WeatherPanel weatherPanel = WeatherPanel::None;
static localWeather::Metric trendMetric = localWeather::Temperature;
static lv_obj_t *todayStation, *todayEvents, *todayForecast, *alertsBody, *alertsButton;
static lv_obj_t *printPrinter, *printStatus, *printJob, *printPercent, *printBar, *printEta;
static lv_obj_t *printMetrics[7], *printConnection;
static lv_obj_t *trendChart, *trendScale, *trendReading;
static lv_point_t trendSegments[localWeather::sampleCount - 1][2];
static time_t lastTrendSample;
static time_t lastAlertsCheck;
static bool lastAlertsFresh;
static unsigned lastActiveAlerts;
static String lastAlertsError;
static bool alertBanner = false;
static lv_obj_t *calendarUsbStatus = nullptr;
static uint32_t calendarUsbStarted;
static char calendarUsbLine[540];
static size_t calendarUsbLength;
static bool calendarUsbOverflow;

static void clearCalendarUsbLine() {
  for (volatile char &byte : calendarUsbLine) byte = 0;
  calendarUsbLength = 0;
  calendarUsbOverflow = false;
}
static const char*tzNames[]={"Manitoba (Winnipeg)","Eastern (Toronto)","Saskatchewan (no DST)","Mountain (Calgary)","Pacific (Vancouver)","Atlantic (Halifax)","Newfoundland (St. John's)","Yukon (no DST)","UTC"};
static const char*tzCodes[]={core::winnipegTimeZone,"EST5EDT,M3.2.0,M11.1.0","CST6","MST7MDT,M3.2.0,M11.1.0","PST8PDT,M3.2.0,M11.1.0","AST4ADT,M3.2.0,M11.1.0","NST3:30NDT,M3.2.0,M11.1.0","MST7","UTC0"};
static String timezoneName(){for(size_t i=0;i<9;++i)if(settings.timezone==tzCodes[i])return tzNames[i];return "Custom time zone";}

static void updateLabel(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static lv_color_t color(uint32_t c){return lv_color_hex(c);}
static lv_obj_t* box(lv_obj_t*p,int x,int y,int w,int h,uint32_t bg,int radius=14){
  auto *o=lv_obj_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
  lv_obj_set_style_bg_color(o,color(bg),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_radius(o,radius,0);
  lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static lv_obj_t* label(lv_obj_t*p,const String&s,int x,int y,int w,const lv_font_t*f=&lv_font_montserrat_16,uint32_t c=TEXT){
  auto*l=lv_label_create(p);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);lv_label_set_text(l,s.c_str());
  lv_obj_set_style_text_color(l,color(c),0);lv_obj_set_style_text_font(l,f,0);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);return l;
}
static void updateBatteryLabel(lv_obj_t *indicator) {
  if (!indicator) return;
  const String caption = String(LV_SYMBOL_BATTERY_FULL) + " " +
      (batteryPercent < 0 ? String("--%") : String(batteryPercent) + "%");
  updateLabel(indicator, caption.c_str());
}

void uiSetBatteryPercent(int percent) {
  if (percent == batteryPercent) return;
  batteryPercent = percent;
  updateBatteryLabel(batteryLabel);
  updateBatteryLabel(modalBatteryLabel);
}

static lv_obj_t *batteryIndicator(lv_obj_t *parent, int x, uint32_t tint) {
  auto *indicator = label(parent, "", x, 20, 100, &lv_font_montserrat_16, tint);
  lv_obj_set_style_text_align(indicator, LV_TEXT_ALIGN_RIGHT, 0);
  updateBatteryLabel(indicator);
  return indicator;
}

static lv_obj_t* button(lv_obj_t*p,const String&s,int x,int y,int w,int h,lv_event_cb_t cb,void *data=nullptr,uint32_t bg=BLUE){
  auto*b=lv_btn_create(p);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);lv_obj_set_style_bg_color(b,color(bg),0);
  lv_obj_set_style_radius(b,10,0);lv_obj_set_style_shadow_width(b,0,0);lv_obj_set_style_pad_all(b,0,0);
  auto*l=label(b,s,0,0,w,&lv_font_montserrat_16,TEXT);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);
  lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);return b;
}
static String fmt(float n,int precision=1){return std::isfinite(n)?String(n,precision):String("--");}
static String dateText(time_t t,const char*pattern){char b[80];tm d=*localtime(&t);strftime(b,sizeof(b),pattern,&d);return b;}
static String temp(float n){return fmt(core::temp(n,settings.imperial))+ (settings.imperial?"°F":"°C");}
static String speed(float n){return fmt(core::wind(n,settings.imperial),0)+(settings.imperial?" mph":" km/h");}
static String rain(float n){return fmt(core::rain(n,settings.imperial),settings.imperial?2:1)+(settings.imperial?" in":" mm");}
static void closeModal(){modalBatteryLabel=nullptr;if(weatherPanel==WeatherPanel::Today)todayOverviewOpen.store(false);if(weatherPanel==WeatherPanel::PrintRoom)printRoomOpen.store(false);weatherPanel=WeatherPanel::None;if(calendarUsbStatus){calendarUsbStatus=nullptr;clearCalendarUsbLine();}if(modal){lv_obj_del(modal);modal=nullptr;lv_obj_invalidate(lv_scr_act());}keyboard=nullptr;}
static void dismiss(lv_event_t*){closeModal();}
static lv_obj_t* newModal(const String &title){
  closeModal();modal=box(lv_layer_top(),0,0,800,480,0x07131F,0);
  label(modal,title,22,18,580,&lv_font_montserrat_24);
  button(modal,"Close",684,10,96,44,dismiss,nullptr,CARD);
  modalBatteryLabel = batteryIndicator(modal, 450, MUTED);
  return modal;
}
static void eventDetails(lv_event_t *ev){
  if(navigation)return;
  size_t i=(uintptr_t)lv_event_get_user_data(ev);if(i>=snapshot.events.size())return;
  const auto&e=snapshot.events[i];auto*m=newModal("Event details");
  auto*body=box(m,20,72,760,380,CARD);lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(body,LV_DIR_VER);
  label(body,e.title.c_str(),20,18,700,&lv_font_montserrat_28);
  String when=e.allDay?String(e.startDate.c_str())+" - "+e.endDate.c_str()+" (end date exclusive)":dateText(e.start,"%a %b %d, %H:%M")+" - "+dateText(e.end,"%a %b %d, %H:%M");
  label(body,when,20,98,700,&lv_font_montserrat_18,TEAL);
  label(body,String("Calendar: ")+e.calendar.c_str()+"\n\nLocation: "+e.location.c_str()+"\n\n"+e.description.c_str(),20,160,700);
}
static const char *weatherCardTitles[] = {
  "TEMPERATURE", "HUMIDITY", "WIND", "PRESSURE", "RAINFALL", "LIGHT", "AIR QUALITY"
};

static void openTrends(lv_event_t *event);

static void weatherDetails(lv_event_t *event) {
  if (navigation) return;
  const auto card = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  const auto &weather = snapshot.weather;
  String readings;

  switch (card) {
    case 0:
      readings = "Temperature   " + temp(weather.temp) +
        "\nToday's low / high   " + temp(weather.min) + " / " + temp(weather.max) +
        "\nDew point   " + temp(weather.dew);
      break;
    case 1:
      readings = "Relative humidity   " + fmt(weather.hum) + " %";
      break;
    case 2:
      readings = "Wind speed   " + speed(weather.wind) +
        "\nPeak gust today   " + speed(weather.gust) +
        "\nPeak gust (10 minutes)   " + speed(weather.gust10) +
        "\nWind direction   " + fmt(weather.dir, 0) + "° " + core::compass(weather.dir);
      break;
    case 3:
      readings = "Atmospheric pressure   " +
        fmt(core::pressure(weather.press, settings.imperial), settings.imperial ? 2 : 1) +
        (settings.imperial ? " inHg" : " hPa");
      break;
    case 4: {
      const bool fresh = weather.loaded && millis() - weather.received <= weatherStaleAfterMs &&
        snapshot.weatherError.isEmpty();
      readings = "Rain (rolling 24 hours)   " + rain(weather.rain) +
        "\nRain (rolling hour)   " + rain(weather.hour) +
        "\nRain (since reset)   " + rain(weather.total) +
        "\nBucket tips   " + (weather.loaded ? String(weather.tips) : String("--")) +
        "\nRain in last 5 minutes   " + (fresh ? (weather.raining ? "Yes" : "No") : "--");
      break;
    }
    case 5:
      readings = "Light   " + fmt(weather.lux, 0) + " lx" +
        "\nEstimated solar radiation   " + fmt(weather.solar, 0) + " W/m2";
      break;
    case 6:
      readings = "ENS160 AQI   " + fmt(weather.aqi, 0) + " / 5" +
        "\neCO2 estimate   " + fmt(weather.eco2, 0) + " ppm" +
        "\nTVOC   " + fmt(weather.tvoc, 0) + " ppb";
      break;
    default:
      return;
  }

  auto *details = newModal(weatherCardTitles[card]);
  auto *body = box(details, 20, 72, 760, 390, CARD);
  label(body, readings, 20, 16, 715, &lv_font_montserrat_18);
  if (card == 0 || card == 2 || card == 3 || card == 4) {
    const auto metric = card == 0 ? localWeather::Temperature : card == 2 ? localWeather::Gust :
                        card == 3 ? localWeather::Pressure : localWeather::Rain;
    button(body, "24-hour trend", 20, 310, 175, 48, openTrends,
           reinterpret_cast<void *>(static_cast<uintptr_t>(metric)));
  }
}
static void weatherUpdate(){
  const Weather&w=snapshot.weather;
  bool fresh=w.loaded && millis()-w.received<=weatherStaleAfterMs && snapshot.weatherError.isEmpty();
  icons::Kind sky=icons::condition(fresh,w.raining,w.lux);
  icons::State nextIcons[7];
  nextIcons[0].kind=sky;
  nextIcons[1]={std::isfinite(w.hum)?icons::Kind::Humidity:icons::Kind::Unknown,w.hum>70?0x55BCFF:w.hum<30?0xFFC85A:TEAL};
  nextIcons[2]={std::isfinite(w.wind)?icons::Kind::Wind:icons::Kind::Unknown,TEAL};
  nextIcons[3]={std::isfinite(w.press)?icons::Kind::Gauge:icons::Kind::Unknown,TEAL};
  nextIcons[4]={fresh?(w.raining?icons::Kind::Rain:icons::Kind::DryRain):icons::Kind::Unknown,0x55BCFF};
  nextIcons[5]={std::isfinite(w.lux)?icons::Kind::Light:icons::Kind::Unknown,0xFFC85A};
  nextIcons[6]={std::isfinite(w.aqi)?icons::Kind::Air:icons::Kind::Unknown,w.aqi>=4?0xFFB56B:TEAL};
  for (size_t i = 0; i < 7; ++i) {
    if (iconStates[i].kind != nextIcons[i].kind || iconStates[i].accent != nextIcons[i].accent) {
      iconStates[i] = nextIcons[i];
      lv_obj_invalidate(weatherImages[i]);
    }
  }
  updateLabel(conditionLabel,icons::caption(sky));
  updateLabel(weatherValues[0],temp(w.temp).c_str());
  updateLabel(weatherValues[1],(fmt(w.hum,0)+"%").c_str());
  updateLabel(weatherValues[2],speed(w.wind).c_str());
  updateLabel(weatherValues[3],(fmt(core::pressure(w.press,settings.imperial),settings.imperial?2:0)+(settings.imperial?" inHg":" hPa")).c_str());
  updateLabel(weatherValues[4],rain(w.rain).c_str());
  updateLabel(weatherValues[5],(fmt(w.lux,0)+" lx").c_str());
  updateLabel(weatherValues[6],(String("AQI ")+fmt(w.aqi,0)+" / 5").c_str());
  updateLabel(weatherSub[0],(String("Dew point ")+temp(w.dew)+"\nLow "+temp(w.min)+"  High "+temp(w.max)).c_str());
  updateLabel(weatherSub[1],"Relative humidity");
  updateLabel(weatherSub[2],(String(core::compass(w.dir))+"  "+fmt(w.dir,0)+"°\nGust "+speed(w.gust)).c_str());
  updateLabel(weatherSub[3],"Atmospheric pressure");
  updateLabel(weatherSub[4],(String("Rolling 24h\nLast hour ")+rain(w.hour)+"\n"+(fresh?(w.raining?"Rain detected":"No recent rain"):"Status unavailable")).c_str());
  updateLabel(weatherSub[5],(String("Solar est. ")+fmt(w.solar,0)+" W/m2").c_str());
  updateLabel(weatherSub[6],(String("eCO2 ")+fmt(w.eco2,0)+" ppm\nTVOC "+fmt(w.tvoc,0)+" ppb").c_str());
  String status;
  if(!snapshot.weatherError.isEmpty())status=snapshot.weatherError;
  else if(w.loaded)status="Updated "+String((millis()-w.received)/1000)+" sec ago • Tap a card for details";
  else status="Waiting for weather readings";
  if(w.loaded && millis()-w.received>weatherStaleAfterMs)status="STALE DATA • "+status;
  updateLabel(statusLabel,status.c_str());
}
static bool forecastFresh(const LocalForecast &forecast, time_t now) {
  return forecast.error.isEmpty() && forecast.checked && now >= forecast.checked &&
         now - forecast.checked <= 11 * 60;
}

static bool activeAlert(const WeatherAlert &alert, time_t now) {
  return !alert.expires || alert.expires > now;
}

static String forecastStatus(const LocalForecast &forecast) {
  if (settings.postalCode.isEmpty()) return "Set your postal code in Settings.";
  if (!forecast.error.isEmpty()) return forecast.error;
  if (!forecast.checked) return "Waiting for the Environment Canada feed...";
  return "Environment Canada feed is out of date.";
}

static void updateToday(const LocalForecast &forecast, const String &agenda, time_t now) {
  const auto &w = snapshot.weather;
  bool fresh = w.loaded && millis() - w.received <= weatherStaleAfterMs && snapshot.weatherError.isEmpty();
  String station = w.loaded ? temp(w.temp) + "  |  Low " + temp(w.min) + "  |  High " + temp(w.max) :
                              String("Waiting for station readings");
  if (w.loaded && !fresh) station += "  |  Stale";
  updateLabel(todayStation, station.c_str());
  updateLabel(todayEvents, agenda.c_str());
  String hours;
  if (forecastFresh(forecast, now)) {
    hours = forecast.location;
    unsigned shown = 0;
    for (const auto &hour : forecast.hours) {
      if (hour.at < now - now % 3600 || shown >= 4) continue;
      hours += "\n" + dateText(hour.at, "%H:%M") + "   " + temp(hour.temperature) + "   " + hour.condition;
      if (hour.precipitation >= 0) hours += "   " + String(hour.precipitation) + "% precip.";
      ++shown;
    }
    if (!shown) hours += "\nHourly forecast unavailable";
  } else hours = forecastStatus(forecast);
  updateLabel(todayForecast, hours.c_str());
}

static void openToday(lv_event_t *) {
  if (navigation) return;
  auto *screen = newModal("Today at a Glance");
  weatherPanel = WeatherPanel::Today;
  todayOverviewOpen.store(true);
  auto *station = box(screen, 20, 70, 760, 77, CARD);
  label(station, "YOUR WEATHER STATION", 16, 10, 728, &lv_font_montserrat_14, TEAL);
  todayStation = label(station, "", 16, 32, 728, &lv_font_montserrat_24);
  auto *agenda = box(screen, 20, 157, 760, 105, CARD);
  label(agenda, "NEXT ON YOUR CALENDAR", 16, 10, 728, &lv_font_montserrat_14, TEAL);
  todayEvents = label(agenda, "", 16, 33, 728, &lv_font_montserrat_16);
  lv_obj_set_height(todayEvents, 64);
  lv_label_set_long_mode(todayEvents, LV_LABEL_LONG_DOT);
  auto *hours = box(screen, 20, 272, 760, 185, CARD);
  label(hours, "HOURLY FORECAST  /  ENVIRONMENT CANADA", 16, 10, 728, &lv_font_montserrat_14, TEAL);
  todayForecast = label(hours, "", 16, 34, 728, &lv_font_montserrat_18);
  lv_obj_set_height(todayForecast, 145);
  lv_label_set_long_mode(todayForecast, LV_LABEL_LONG_DOT);
  lastTick = millis() - 1000;
}

static void updateAlerts(const LocalForecast &forecast, time_t now) {
  const bool fresh = forecastFresh(forecast, now);
  const unsigned active = std::count_if(forecast.alerts.begin(), forecast.alerts.end(),
      [now](const auto &alert) { return activeAlert(alert, now); });
  if (lastAlertsCheck == forecast.checked && lastAlertsFresh == fresh &&
      lastAlertsError == forecast.error && lastActiveAlerts == active) return;
  lastActiveAlerts = active;
  lastAlertsCheck = forecast.checked;
  lastAlertsFresh = fresh;
  lastAlertsError = forecast.error;
  lv_obj_clean(alertsBody);
  String where = forecast.location;
  if (forecast.region.length() && forecast.region != forecast.location) where += "  /  " + forecast.region;
  label(alertsBody, where, 16, 10, 720, &lv_font_montserrat_18, TEAL);
  int y = 43;
  if (!fresh) {
    label(alertsBody, forecastStatus(forecast), 16, y, 710);
    y += 35;
  }
  unsigned shown = 0;
  for (const auto &alert : forecast.alerts) {
    if (!activeAlert(alert, now)) continue;
    auto *card = box(alertsBody, 12, y, 724, 85, 0x412C25);
    auto *title = label(card, alert.title, 14, 10, 690, &lv_font_montserrat_18);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    label(card, alert.expires ? "Expires " + dateText(alert.expires, "%a %b %d, %H:%M") :
          String("Expiry unavailable"), 14, 45, 690, &lv_font_montserrat_16, MUTED);
    y += 94;
    ++shown;
  }
  if (!shown && fresh) label(alertsBody, forecast.alertsKnown ? "No active alerts reported." :
                            "Alert status unavailable.", 16, y, 710);
}

static void openAlerts(lv_event_t *) {
  if (navigation) return;
  auto *screen = newModal("Weather Alerts");
  weatherPanel = WeatherPanel::Alerts;
  alertsBody = box(screen, 20, 70, 760, 360, CARD);
  lv_obj_add_flag(alertsBody, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(alertsBody, LV_DIR_VER);
  label(screen, "Source: Environment and Climate Change Canada", 24, 442, 752,
        &lv_font_montserrat_14, MUTED);
  lastAlertsCheck = -1;
  lastTick = millis() - 1000;
}

static const char *trendNames[] = {"Temperature", "Pressure", "Wind gusts", "Rainfall"};
static localWeather::History displayedHistory;

static String trendValue(float value) {
  switch (trendMetric) {
    case localWeather::Temperature: return temp(value);
    case localWeather::Pressure:
      return fmt(core::pressure(value, settings.imperial), settings.imperial ? 2 : 1) +
             (settings.imperial ? " inHg" : " hPa");
    case localWeather::Gust: return speed(value);
    default: return rain(value);
  }
}

static void pickTrendPoint(lv_event_t *) {
  auto *input = lv_indev_get_act();
  if (!input || !displayedHistory.count) return;
  lv_point_t point;
  lv_indev_get_point(input, &point);
  lv_area_t area;
  lv_obj_get_coords(trendChart, &area);
  const time_t end = displayedHistory.at(displayedHistory.count - 1).at;
  const time_t start = end - localWeather::historySeconds;
  const time_t target = start + static_cast<int64_t>(std::clamp<int>(point.x - area.x1, 0, 623)) *
                                localWeather::historySeconds / 623;
  const localWeather::Sample *nearest = nullptr;
  time_t distance = 601;
  for (unsigned i = 0; i < displayedHistory.count; ++i) {
    const auto &sample = displayedHistory.at(i);
    const auto delta = sample.at > target ? sample.at - target : target - sample.at;
    if (sample.at >= start && delta < distance && std::isfinite(sample.values[trendMetric])) {
      nearest = &sample;
      distance = delta;
    }
  }
  const String reading = nearest ? dateText(nearest->at, "%a %H:%M") + "  /  " +
                        trendValue(nearest->values[trendMetric]) : String("No sample near this time");
  updateLabel(trendReading, reading.c_str());
}

static void drawTrend() {
  lv_obj_clean(trendChart);
  if (!displayedHistory.count) {
    updateLabel(trendScale, "");
    updateLabel(trendReading, "History starts after a station reading and internet time sync.");
    return;
  }
  const time_t end = displayedHistory.at(displayedHistory.count - 1).at;
  const time_t start = end - localWeather::historySeconds;
  float lo = INFINITY, hi = -INFINITY;
  unsigned count = 0;
  const localWeather::Sample *latest = nullptr;
  for (unsigned i = 0; i < displayedHistory.count; ++i) {
    const auto &sample = displayedHistory.at(i);
    const float value = sample.values[trendMetric];
    if (sample.at < start || !std::isfinite(value)) continue;
    lo = std::min(lo, value);
    hi = std::max(hi, value);
    latest = &sample;
    ++count;
  }
  if (!latest) {
    updateLabel(trendScale, "");
    updateLabel(trendReading, "No readings available for this sensor.");
    return;
  }
  updateLabel(trendScale, (dateText(start, "%a %H:%M") + "  to  " + dateText(end, "%a %H:%M") +
             "     |     Low " + trendValue(lo) + "   High " + trendValue(hi)).c_str());
  updateLabel(trendReading, ("Last sample " + dateText(latest->at, "%a %H:%M") +
                             "  /  " + trendValue(latest->values[trendMetric])).c_str());
  if (count == 1) {
    label(trendChart, "Waiting for another reading to draw the trend.", 16, 25, 590);
    return;
  }
  if (lo == hi) { lo -= 1; hi += 1; }
  unsigned lines = 0;
  time_t previousTime = 0;
  lv_point_t previous{};
  for (unsigned i = 0; i < displayedHistory.count; ++i) {
    const auto &sample = displayedHistory.at(i);
    const float value = sample.values[trendMetric];
    if (sample.at < start || !std::isfinite(value)) { previousTime = 0; continue; }
    lv_point_t point{static_cast<lv_coord_t>((sample.at - start) * 623 / localWeather::historySeconds),
                     static_cast<lv_coord_t>(220 - (value - lo) * 205 / (hi - lo))};
    if (previousTime && sample.at - previousTime <= 2 * weatherPollIntervalMs / 1000) {
      auto &segment = trendSegments[lines++];
      segment[0] = previous;
      segment[1] = point;
      auto *line = lv_line_create(trendChart);
      lv_obj_set_pos(line, 0, 0);
      lv_line_set_points(line, segment, 2);
      lv_obj_set_style_line_color(line, color(TEAL), 0);
      lv_obj_set_style_line_width(line, 3, 0);
      lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }
    previous = point;
    previousTime = sample.at;
  }
}

static void openTrends(lv_event_t *event) {
  if (navigation) return;
  trendMetric = static_cast<localWeather::Metric>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  auto *screen = newModal("Weather Trends");
  weatherPanel = WeatherPanel::Trends;
  for (unsigned i = 0; i < localWeather::MetricCount; ++i) {
    button(screen, trendNames[i], 20 + i * 191, 66, 185, 44, openTrends,
           reinterpret_cast<void *>(static_cast<uintptr_t>(i)), i == trendMetric ? 0x147C78 : CARD);
  }
  auto *panel = box(screen, 20, 119, 760, 304, CARD);
  label(panel, trendMetric == localWeather::Rain ? "Rolling 24-hour rainfall at each sample" :
        trendMetric == localWeather::Gust ? "10-minute peak gust at each sample" : "Last 24 hours",
        16, 10, 728, &lv_font_montserrat_14, TEAL);
  trendChart = box(panel, 65, 39, 624, 234, BG, 0);
  lv_obj_add_flag(trendChart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(trendChart, pickTrendPoint, LV_EVENT_CLICKED, nullptr);
  trendScale = label(panel, "", 16, 277, 728, &lv_font_montserrat_14, MUTED);
  trendReading = label(screen, "", 24, 438, 752, &lv_font_montserrat_16);
  lastTrendSample = -1;
  lastTick = millis() - 1000;
}

static void updatePrintRoom(const PrintRoom &room, uint32_t now, time_t clock) {
  const bool fresh = room.loaded && room.error.isEmpty() && now - room.received < 12000;
  const bool printerOnline = fresh && !room.status.isEmpty();
  String connection = "Updates every 3 seconds while this screen is open";
  if (settings.printRoomUrl.isEmpty()) connection = "Set the bridge address in Settings > Print Room.";
  else if (!validPrintRoomKey(settings.printRoomKey)) connection = "Enter the bridge access key in Settings > Print Room.";
  else if (!room.error.isEmpty()) connection = room.error;
  else if (!fresh) connection = room.loaded ? "Bridge data is stale" : "Connecting to Print Room bridge...";
  updateLabel(printConnection, connection.c_str());
  updateLabel(printPrinter, room.printer.isEmpty() ? "Printer" : room.printer.c_str());
  updateLabel(printStatus, printerOnline ? room.status.c_str() : fresh ? "Offline / unavailable" : "--");
  updateLabel(printJob, printerOnline && !room.job.isEmpty() ? room.job.c_str() : "--");
  const bool hasProgress = printerOnline && std::isfinite(room.progress) && room.progress >= 0 && room.progress <= 100;
  const int percent = hasProgress ? lroundf(room.progress) : 0;
  updateLabel(printPercent, hasProgress ? (String(percent) + "%").c_str() : "--");
  lv_bar_set_value(printBar, percent, LV_ANIM_OFF);

  String eta;
  if (printerOnline && !room.stage.isEmpty()) eta = room.stage;
  if (printerOnline && std::isfinite(room.remainingMinutes) && room.remainingMinutes > 0)
    eta += (eta.length() ? String("  |  ") : String("")) + fmt(room.remainingMinutes, 0) + " min left";
  if (printerOnline && clock > 1700000000 && room.finishAt >= clock)
    eta += (eta.length() ? String("  |  ") : String("")) + "Finish ~" + dateText(room.finishAt, "%H:%M");
  updateLabel(printEta, eta.c_str());

  const String readings[] = {
    printerOnline && std::isfinite(room.layer) ? fmt(room.layer, 0) + " / " + fmt(room.layers, 0) : String("--"),
    printerOnline && std::isfinite(room.nozzle) ? temp(room.nozzle) : String("--"),
    printerOnline && std::isfinite(room.bed) ? temp(room.bed) : String("--"),
    printerOnline && std::isfinite(room.chamber) ? temp(room.chamber) : String("--"),
    fresh && std::isfinite(room.roomTemp) ? temp(room.roomTemp) : String("--"),
    fresh && std::isfinite(room.humidity) ? fmt(room.humidity, 0) + "%" : String("--"),
    fresh && std::isfinite(room.pm25) ? fmt(room.pm25) + " ug/m3" : String("--")
  };
  for (unsigned i = 0; i < 7; ++i) updateLabel(printMetrics[i], readings[i].c_str());
}

static void openPrintRoom(lv_event_t *) {
  if (navigation) return;
  auto *screen = newModal("Print Room");
  weatherPanel = WeatherPanel::PrintRoom;
  printRoomOpen.store(true);
  auto *printer = box(screen, 20, 67, 760, 243, CARD);
  printPrinter = label(printer, "", 20, 15, 430, &lv_font_montserrat_16, MUTED);
  printStatus = label(printer, "", 465, 15, 275, &lv_font_montserrat_16, TEAL);
  lv_obj_set_style_text_align(printStatus, LV_TEXT_ALIGN_RIGHT, 0);
  printJob = label(printer, "", 20, 43, 720, &lv_font_montserrat_24);
  lv_label_set_long_mode(printJob, LV_LABEL_LONG_DOT);
  printPercent = label(printer, "", 20, 80, 180, &lv_font_montserrat_48);
  printEta = label(printer, "", 195, 98, 545, &lv_font_montserrat_16, MUTED);
  lv_obj_set_style_text_align(printEta, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(printEta, LV_LABEL_LONG_DOT);
  printBar = lv_bar_create(printer);
  lv_obj_set_pos(printBar, 20, 149);
  lv_obj_set_size(printBar, 720, 9);
  lv_obj_set_style_bg_color(printBar, color(BG), LV_PART_MAIN);
  lv_obj_set_style_bg_color(printBar, color(TEAL), LV_PART_INDICATOR);
  const char *metrics[] = {"LAYER", "NOZZLE", "BED", "CHAMBER", "ROOM TEMPERATURE", "ROOM HUMIDITY", "PM2.5"};
  for (unsigned i = 0; i < 4; ++i) {
    label(printer, metrics[i], 20 + i * 182, 183, 170, &lv_font_montserrat_12, MUTED);
    printMetrics[i] = label(printer, "", 20 + i * 182, 201, 170, &lv_font_montserrat_18);
  }
  for (unsigned i = 4; i < 7; ++i) {
    auto *sensor = box(screen, 20 + (i - 4) * 257, 324, 246, 104, CARD);
    label(sensor, metrics[i], 16, 12, 210, &lv_font_montserrat_14, MUTED);
    printMetrics[i] = label(sensor, "", 16, 42, 210, &lv_font_montserrat_28);
  }
  printConnection = label(screen, "", 22, 446, 750, &lv_font_montserrat_14, MUTED);
  lastTick = millis() - 1000;
}

static void weatherScreen(){
  const int xs[]={16,360,568,16,212,408,604},ys[]={76,76,76,254,254,254,254},ws[]={328,192,216,180,180,180,180};
  for(int i=0;i<7;++i){
    auto*b=box(root,xs[i],ys[i],ws[i],i<3?162:152,CARD);
    lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(b,weatherDetails,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    label(b,weatherCardTitles[i],16,14,i==0?150:ws[i]-66,&lv_font_montserrat_14,MUTED);
    weatherImages[i]=icons::create(b,i==0?16:ws[i]-50,i==0?42:8,i==0?64:38,&iconStates[i]);
    if(i==0)conditionLabel=label(b,"",160,16,156,&lv_font_montserrat_12,TEAL);
    weatherValues[i]=label(b,"--",i==0?96:16,i==0?46:55,i==0?216:ws[i]-24,i==0?&lv_font_montserrat_48:i==1?&lv_font_montserrat_32:i==2?&lv_font_montserrat_28:&lv_font_montserrat_24);
    weatherSub[i]=label(b,"",16,i<3?112:99,ws[i]-24,&lv_font_montserrat_14,MUTED);
  }
  statusLabel=label(root,"",20,411,760,&lv_font_montserrat_16,MUTED);
  button(root, "Today at a Glance", 16, 438, 184, 39, openToday, nullptr, CARD);
  button(root, "Weather Trends", 211, 438, 184, 39, openTrends, nullptr, CARD);
  alertsButton = button(root, "Weather Alerts", 406, 438, 184, 39, openAlerts, nullptr, CARD);
  button(root, "Print Room", 601, 438, 183, 39, openPrintRoom, nullptr, CARD);
  alertBanner = false;
  weatherUpdate();
}
static void focusField(lv_event_t *e){
  lv_keyboard_set_textarea(keyboard,lv_event_get_target(e));
  lv_obj_scroll_to_view(lv_event_get_target(e),LV_ANIM_ON);
}
static lv_obj_t *input(lv_obj_t*p,const char*name,const String&value,int y,bool secret=false,int maxLen=128){
  label(p,name,12,y,690,&lv_font_montserrat_14,MUTED);
  auto*t=lv_textarea_create(p);lv_obj_set_pos(t,12,y+24);lv_obj_set_size(t,704,48);
  lv_textarea_set_one_line(t,true);lv_textarea_set_max_length(t,maxLen);lv_textarea_set_password_mode(t,secret);
  lv_textarea_set_text(t,value.c_str());lv_obj_add_event_cb(t,focusField,LV_EVENT_FOCUSED,nullptr);return t;
}
static void applyEditorSettings() {
  xSemaphoreTake(stateMutex,portMAX_DELAY);settings=draft;
  if(editKind==5) printRoom = PrintRoom{};
  if(editKind==2 || editKind==3){shared.events.clear();shared.todayEvents.clear();shared.todayWeek=0;shared.calendarLoaded=false;shared.calendarError="Loading calendar...";}
  if(editKind==1){shared.weather=Weather();shared.weatherError="Connecting to station...";}
  xSemaphoreGive(stateMutex);saveSettings();
  if(editKind==2 || editKind==3) calendarRefreshRequested.store(true);
  setenv("TZ",settings.timezone.c_str(),1);tzset();closeModal();redraw=true;calendarDirty=true;
}

static void startCalendarUsb(lv_event_t *) {
  auto *screen = newModal("Google Calendar over USB");
  label(screen, "Connect the display to your PC with its programming USB cable. "
                "Close any serial monitor, then run the Windows helper from the setup guide.",
        24, 86, 748);
  calendarUsbStatus = label(screen, "Waiting for the URL and token (2 minutes).", 24, 170, 748);
  calendarUsbStarted = millis();
}

void uiCalendarUsbExpire() {
  if (!calendarUsbStatus || millis() - calendarUsbStarted < 120000) return;
  lv_label_set_text(calendarUsbStatus, "USB setup expired. Close this screen and start again.");
  calendarUsbStatus = nullptr;
  clearCalendarUsbLine();
}

CalendarUsbResult uiCalendarUsbReceive(char byte) {
  if (!calendarUsbStatus) return CalendarUsbResult::Pending;
  if (byte != '\n') {
    if (calendarUsbLength < sizeof(calendarUsbLine) - 1 && byte >= 32 && byte <= 126)
      calendarUsbLine[calendarUsbLength++] = byte;
    else
      calendarUsbOverflow = true;
    return CalendarUsbResult::Pending;
  }

  constexpr char prefix[] = "CALENDAR ";
  calendarUsbLine[calendarUsbLength] = 0;
  const char *url = calendarUsbLine + sizeof(prefix) - 1;
  const char *separator = strchr(url, ' ');
  if (!calendarUsbOverflow && strncmp(calendarUsbLine, prefix, sizeof(prefix) - 1) == 0 && separator) {
    constexpr char googlePrefix[] = "https://script.google.com/macros/s/";
    const size_t urlLength = separator - url;
    const char *token = separator + 1;
    const size_t tokenLength = strlen(token);
    if (urlLength > sizeof(googlePrefix) - 1 + 5 && urlLength <= 400 &&
        strncmp(url, googlePrefix, sizeof(googlePrefix) - 1) == 0 &&
        strncmp(separator - 5, "/exec", 5) == 0 &&
        tokenLength >= 32 && tokenLength <= 128 &&
        strspn(token, "0123456789abcdefABCDEF") == tokenLength &&
        strspn(url + sizeof(googlePrefix) - 1,
               "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") ==
            urlLength - (sizeof(googlePrefix) - 1) - 5) {
      calendarUsbLine[urlLength + sizeof(prefix) - 1] = 0;
      draft = settings;
      draft.calendarUrl = url;
      draft.calendarToken = token;
      applyEditorSettings();
      return CalendarUsbResult::Saved;
    }
  }
  clearCalendarUsbLine();
  lv_label_set_text(calendarUsbStatus, "Check the /exec URL and hexadecimal token, then try again.");
  return CalendarUsbResult::Invalid;
}

static void editorSave(lv_event_t*){
  auto value=[](int i){String s=lv_textarea_get_text(fields[i]);return s;};
  if(editKind==0){draft.ssid=value(0);draft.password=value(1);}
  if(editKind==1){draft.station=value(0);draft.station.trim();}
  if(editKind==2){draft.calendarUrl=value(0);draft.calendarToken=value(1);draft.calendarUrl.trim();draft.calendarToken.trim();}
  if(editKind==3){draft.timezone=value(0);draft.timezone.trim();}
  if (editKind == 4) {
    char prefix[4];
    if (!localWeather::postalPrefix(value(0).c_str(), prefix)) {
      auto *msg = lv_msgbox_create(modal, "Check postal code",
          "Enter a Canadian postal code, for example A1A 1A1.", nullptr, true);
      lv_obj_center(msg);
      return;
    }
    draft.postalCode = prefix;
  }
  if (editKind == 5) {
    draft.printRoomUrl = value(0);
    draft.printRoomUrl.trim();
    draft.printRoomKey = value(1);
    draft.printRoomKey.trim();
    if (draft.printRoomUrl.isEmpty()) draft.printRoomKey = "";
    if (!validPrintRoomUrl(draft.printRoomUrl) ||
        (!draft.printRoomUrl.isEmpty() && !validPrintRoomKey(draft.printRoomKey))) {
      auto *msg = lv_msgbox_create(modal, "Check bridge address",
          "Enter an http:// LAN address ending in /status and a 32-character hex access key.", nullptr, true);
      lv_obj_center(msg);
      return;
    }
  }
  if((editKind==0 && (draft.ssid.length()>32 || draft.password.length()>63)) ||
     (editKind==1 && !draft.station.startsWith("http://")) ||
     (editKind==2 && draft.calendarUrl.length() && (!draft.calendarUrl.startsWith("https://script.google.com/macros/s/") || !draft.calendarUrl.endsWith("/exec") || draft.calendarToken.length()<32)) ||
     (editKind==3 && draft.timezone.isEmpty())){
    lv_obj_t *msg=lv_msgbox_create(modal,"Check settings","Check the address, token, Wi-Fi length, or time zone format.",nullptr,true);lv_obj_center(msg);return;
  }
  applyEditorSettings();
}
static void timezoneSelect(lv_event_t*e){
  int i=(intptr_t)lv_event_get_user_data(e);if(i<0 || i>=9)return;
  xSemaphoreTake(stateMutex,portMAX_DELAY);settings.timezone=tzCodes[i];shared.calendarLoaded=false;shared.events.clear();shared.todayEvents.clear();shared.todayWeek=0;shared.calendarError="Loading calendar...";xSemaphoreGive(stateMutex);
  setenv("TZ",settings.timezone.c_str(),1);tzset();saveSettings();closeModal();redraw=true;calendarDirty=true;
}
static void edit(lv_event_t *e){
  if(navigation)return;
  editKind=(intptr_t)lv_event_get_user_data(e);draft=settings;
  if(editKind==3){
    auto*m=newModal("Choose your time zone");auto*list=box(m,20,72,760,390,BG);
    lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(list,LV_DIR_VER);
    for(int i=0;i<9;++i)button(list,tzNames[i],12,i*58,720,48,timezoneSelect,(void*)(intptr_t)i,settings.timezone==tzCodes[i]?0x147C78:CARD);
    lv_obj_invalidate(modal);return;
  }
  const char*titles[]={"Wi-Fi setup","Weather station address","Google Calendar setup","Time zone (POSIX)","Postal code","Print Room bridge"};
  auto*m=newModal(titles[editKind]);button(m,"Save",570,10,96,44,editorSave);
  if(editKind==2)button(m,"Set up over USB",396,10,158,44,startCalendarUsb,nullptr,CARD);
  lv_obj_set_x(modalBatteryLabel, editKind == 2 ? 280 : 450);
  form=box(m,24,72,752,164,CARD);lv_obj_add_flag(form,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(form,LV_DIR_VER);
  keyboard=lv_keyboard_create(m);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(keyboard,0,252);lv_obj_set_size(keyboard,800,228);
  if(editKind==0){fields[0]=input(form,"Network name (2.4 GHz)",draft.ssid,8,false,32);fields[1]=input(form,"Wi-Fi password",draft.password,96,true,63);}
  if(editKind==1)fields[0]=input(form,"Base URL, for example http://192.168.2.149",draft.station,8,false,200);
  if(editKind==2){fields[0]=input(form,"Apps Script web app URL ending in /exec",draft.calendarUrl,8,false,400);fields[1]=input(form,"Bridge token (from your own Apps Script setup)",draft.calendarToken,96,true,128);}
  if(editKind==3)fields[0]=input(form,"Manitoba: MBT5  |  Eastern: EST5EDT,M3.2.0,M11.1.0",draft.timezone,8,false,100);
  if (editKind == 4) {
    fields[0] = input(form, "Canadian postal code (first three characters are enough)", draft.postalCode, 8, false, 10);
    label(form, "Leave blank to clear the location.", 12, 90, 700, &lv_font_montserrat_14, MUTED);
  }
  if (editKind == 5) {
    fields[0] = input(form, "Bridge URL, for example http://192.168.1.10:8787/status",
                      draft.printRoomUrl, 8, false, 160);
    fields[1] = input(form, "Bridge access key (32 hex characters)", draft.printRoomKey, 92, true, 32);
  }
  lv_keyboard_set_textarea(keyboard,fields[0]);lv_obj_invalidate(modal);
}
static void unitsChanged(lv_event_t *e){
  bool value=lv_obj_has_state(lv_event_get_target(e),LV_STATE_CHECKED);
  xSemaphoreTake(stateMutex,portMAX_DELAY);settings.imperial=value;xSemaphoreGive(stateMutex);saveSettings();
}
static void brightnessChanged(lv_event_t*e){int value=lv_slider_get_value(lv_event_get_target(e));
  xSemaphoreTake(stateMutex,portMAX_DELAY);settings.brightness=value;xSemaphoreGive(stateMutex);uiSetDimming(value);
  if(lv_event_get_code(e)==LV_EVENT_RELEASED)saveSettings();
}
static String screenTimeoutLabel(uint32_t seconds) {
  return seconds < 60 ? String(seconds) + " seconds" :
         String(seconds / 60) + (seconds == 60 ? " minute" : " minutes");
}

static void selectScreenTimeout(lv_event_t *event) {
  const auto seconds = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  settings.screenTimeoutSeconds = seconds;
  xSemaphoreGive(stateMutex);
  saveSettings();
  closeModal();
  redraw = true;
}

static void openScreenTimeout(lv_event_t *) {
  if (navigation) return;
  auto *screen = newModal("Screen timeout");
  label(screen, "Turn off the backlight after this long without a touch.", 24, 74, 752);
  int y = 110;
  for (uint32_t seconds : ScreenIdle::timeoutOptions) {
    button(screen, screenTimeoutLabel(seconds), 24, y, 752, 48, selectScreenTimeout,
           reinterpret_cast<void *>(static_cast<uintptr_t>(seconds)),
           seconds == settings.screenTimeoutSeconds ? 0x147C78 : CARD);
    y += 58;
  }
}

static void rotationChanged(lv_event_t *event) {
  const bool enabled = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  settings.rotateScreen = enabled;
  xSemaphoreGive(stateMutex);
  saveSettings();
  lv_obj_invalidate(lv_scr_act());
}

static constexpr int settingsTop = 61;
static constexpr int settingsRowPitch = 41;
static constexpr int settingsRowHeight = 38;
static constexpr int dimmingTop = settingsTop + 3 * settingsRowPitch;

static void settingsScreen(){
  const char*names[]={"Wi-Fi","Weather station","Google Calendar","Display dimming","Imperial units","Time zone","Screen timeout","Postal code","Print Room","Rotate screen 180°"};
  String values[]={settings.ssid.isEmpty()?"Tap to connect":settings.ssid,settings.station,settings.calendarUrl.isEmpty()?"Tap to link":"Configured", "", "°F / mph / in", timezoneName(), screenTimeoutLabel(settings.screenTimeoutSeconds), settings.postalCode.isEmpty() ? "Tap to set location" : settings.postalCode, settings.printRoomUrl.isEmpty() ? "Tap to set bridge" : settings.printRoomUrl, ""};
  int kinds[]={0,1,2,-1,-1,3,-1,4,5,-1};
  for(int i=0;i<10;++i){auto*b=box(root,20,settingsTop+i*settingsRowPitch,760,settingsRowHeight,CARD);
    label(b,names[i],16,11,220,&lv_font_montserrat_18);
    if(i==3){auto*s=lv_slider_create(b);lv_obj_set_pos(s,300,20);lv_obj_set_size(s,416,10);lv_slider_set_range(s,20,100);lv_slider_set_value(s,settings.brightness,LV_ANIM_OFF);lv_obj_add_event_cb(s,brightnessChanged,LV_EVENT_VALUE_CHANGED,nullptr);lv_obj_add_event_cb(s,brightnessChanged,LV_EVENT_RELEASED,nullptr);}
    else if(i==4 || i==9){
      if(i==4) label(b,values[i],300,11,320,&lv_font_montserrat_14,MUTED);
      auto *toggle = lv_switch_create(b);
      lv_obj_set_pos(toggle,680,4);lv_obj_set_size(toggle,60,30);
      if(i==4 ? settings.imperial : settings.rotateScreen) lv_obj_add_state(toggle,LV_STATE_CHECKED);
      lv_obj_add_event_cb(toggle,i==4?unitsChanged:rotationChanged,LV_EVENT_VALUE_CHANGED,nullptr);
    }
    else{auto*l=label(b,values[i]+"  >",250,16,490,&lv_font_montserrat_14,TEAL);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(b,i==6?openScreenTimeout:edit,LV_EVENT_CLICKED,(void*)(intptr_t)kinds[i]);}
  }
}
static void viewChange(lv_event_t *e){
  if(navigation)return;
  int action=(intptr_t)lv_event_get_user_data(e);
  if(action==0){weekView=false;requestedWeek=0;}
  if(action==1)weekView=true;
  if(action==2){requestedWeek=0;weekView=true;}
  if(action==3){requestedWeek=std::max(-12,(int)requestedWeek-1);weekView=true;}
  if(action==4){requestedWeek=std::min(12,(int)requestedWeek+1);weekView=true;}
  redraw=true;calendarDirty=true;
}
static void refreshCalendar(lv_event_t*) {
  calendarRefreshRequested.store(true);
}
static void calendarScreen(){
  button(root,"Today",20,70,86,42,viewChange,(void*)0,weekView?0x64748B:BLUE);
  button(root,"Week",114,70,86,42,viewChange,(void*)1,weekView?BLUE:0x64748B);
  button(root,"<",222,70,48,42,viewChange,(void*)3,0x64748B);
  button(root,"This week",280,70,120,42,viewChange,(void*)2,0x64748B);
  button(root,">",410,70,48,42,viewChange,(void*)4,0x64748B);
  button(root,LV_SYMBOL_REFRESH,730,70,50,42,refreshCalendar,nullptr,0x64748B);
  calendarDate=label(root,"",476,83,246,&lv_font_montserrat_14,INK);
  calendarBody=box(root,0,118,800,332,0xFFFFFF,0);
  statusLabel=label(root,"",16,458,770,&lv_font_montserrat_12,0x5F6368);
  calendarDirty=true;
}
static void agenda(){
  time_t now=time(nullptr),day=core::midnight(now),end=core::addDays(day,1);
  label(calendarBody,dateText(now,"%A, %B %d"),20,8,750,&lv_font_montserrat_24,INK);
  auto*b=box(calendarBody,16,52,768,276,0xFFFFFF,0);lv_obj_add_flag(b,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(b,LV_DIR_VER);
  contentScroller=b;
  int y=0;
  for(size_t i=0;i<snapshot.events.size();++i){const auto&e=snapshot.events[i];if(!core::intersects(e,day,end))continue;
    auto*row=box(b,0,y,754,82,0xF4F7FB,12);lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(row,eventDetails,LV_EVENT_CLICKED,(void*)(uintptr_t)i);
    box(row,120,12,5,58,e.color,2);
    label(row,e.allDay?"All day":dateText(e.start,"%H:%M"),16,26,100,&lv_font_montserrat_20,INK);
    auto*t=label(row,e.title.c_str(),144,12,580,&lv_font_montserrat_20,INK);lv_label_set_long_mode(t,LV_LABEL_LONG_DOT);
    auto*d=label(row,(e.allDay?String("All-day event"):dateText(e.start,"%H:%M")+"-"+dateText(e.end,"%H:%M"))+"  "+e.location.c_str(),144,48,580,&lv_font_montserrat_14,0x5F6368);lv_label_set_long_mode(d,LV_LABEL_LONG_DOT);
    y+=92;
  }
  if(!y)label(b,"No events today",16,26,680,&lv_font_montserrat_24,0x5F6368);
}
static void week(){
  time_t now=time(nullptr),start=core::sunday(now,requestedWeek);
  constexpr int gutter=52,col=105;
  lv_label_set_text(calendarDate,(dateText(start,"%b %d")+" - "+dateText(core::addDays(start,6),"%b %d, %Y")).c_str());
  int allCount[7]={};int maxAll=0;
  for(int d=0;d<7;++d){time_t day=core::addDays(start,d);for(const auto&e:snapshot.events)if(e.allDay && core::intersects(e,day,core::addDays(day,1)))++allCount[d];maxAll=std::max(maxAll,allCount[d]);}
  int allH=maxAll?std::min(maxAll,2)*25+4:24;
  for(int d=0;d<7;++d){time_t day=core::addDays(start,d);bool today=core::midnight(day)==core::midnight(now);
    auto*h=box(calendarBody,gutter+d*col,0,col-1,36,today?0xE8F0FE:0xFFFFFF,0);
    auto*l=label(h,dateText(day,"%a %d"),0,9,col-1,&lv_font_montserrat_16,today?BLUE:INK);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    auto*a=box(calendarBody,gutter+d*col,36,col-1,allH,0xF8F9FA,0);lv_obj_add_flag(a,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(a,LV_DIR_VER);
    int y=1;for(size_t i=0;i<snapshot.events.size();++i){const auto&e=snapshot.events[i];if(!e.allDay || !core::intersects(e,day,core::addDays(day,1)))continue;
      auto*event=box(a,2,y,col-5,23,e.color,4);lv_obj_add_flag(event,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(event,eventDetails,LV_EVENT_CLICKED,(void*)(uintptr_t)i);
      auto*name=label(event,e.title.c_str(),4,3,col-12,&lv_font_montserrat_12,TEXT);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);y+=25;
    }
  }
  label(calendarBody,"All day",2,42,48,&lv_font_montserrat_12,0x5F6368);
  int top=36+allH;
  auto*scroller=box(calendarBody,0,top,800,332-top,0xFFFFFF,0);lv_obj_add_flag(scroller,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(scroller,LV_DIR_VER);lv_obj_set_scrollbar_mode(scroller,LV_SCROLLBAR_MODE_AUTO);
  contentScroller=scroller;
  auto*grid=box(scroller,0,0,790,24*54+1,0xFFFFFF,0);
  for(int hour=0;hour<24;++hour){
    label(grid,(hour<10?String("0"):String(""))+hour+":00",2,hour*54+3,48,&lv_font_montserrat_12,0x5F6368);
    box(grid,gutter,hour*54,735,1,0xDADCE0,0);box(grid,gutter,hour*54+27,735,1,0xF1F3F4,0);
  }
  for(int d=0;d<=7;++d)box(grid,gutter+d*col,0,1,24*54,0xDADCE0,0);
  for(int d=0;d<7;++d){time_t day=core::addDays(start,d);
    auto segments=core::daySegments(snapshot.events,day);
    for(const auto&s:segments){const auto&e=snapshot.events[s.event];int w=(col-4)/s.columns;
      auto*b=box(grid,gutter+d*col+2+s.column*w,(int)(s.from*54/60)+1,w-2,std::max(22,(int)((s.to-s.from)*54/60)-2),e.color,5);
      lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(b,eventDetails,LV_EVENT_CLICKED,(void*)(uintptr_t)s.event);
      auto*l=label(b,e.title.c_str(),3,3,w-8,&lv_font_montserrat_12,TEXT);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
    }
    if(core::midnight(now)==day){int y=core::wallMinute(now)*54/60;box(grid,gutter+d*col,y,col,2,0xEA4335,0);box(grid,gutter+d*col-3,y-3,8,8,0xEA4335,4);}
  }
  lv_obj_scroll_to_y(scroller,8*54,LV_ANIM_OFF);
}
static void calendarUpdate(){
  if(!calendarBody)return;
  // Do not destroy clickable event widgets underneath an open event-details modal.
  if(modal)return;
  time_t now=time(nullptr),want=core::sunday(now,requestedWeek);
  bool preserve=contentScroller && shownWeek==want && renderedWeek==weekView;
  int scroll=preserve?lv_obj_get_scroll_y(contentScroller):0;
  lv_obj_clean(calendarBody);contentScroller=nullptr;shownWeek=want;
  bool matches=snapshot.calendarLoaded && snapshot.calendarWeek==want;
  String status;
  if(now<1700000000){label(calendarBody,"Waiting for time sync",24,60,740,&lv_font_montserrat_24,INK);status="Connect Wi-Fi to set the clock";}
  else if(!matches){label(calendarBody,snapshot.calendarError.isEmpty()?"Waiting for next sync. Tap Refresh to load this week now.":snapshot.calendarError,24,60,740,&lv_font_montserrat_20,INK);status="Swipe left to return to Weather";}
  else{
    if(weekView)week();else {lv_label_set_text(calendarDate,"");agenda();}
    status=(snapshot.calendarError.isEmpty()?String("Updated ")+String((millis()-snapshot.calendarReceived)/60000)+" min ago":String("STALE • ")+snapshot.calendarError);
    if(snapshot.calendarError.isEmpty() && millis()-snapshot.calendarReceived>600000)status="STALE • "+status;
    if(snapshot.truncated)status+=" • Showing first 100 events";
    status+=" • Swipe left for Weather";
  }
  if(preserve && contentScroller)lv_obj_scroll_to_y(contentScroller,scroll,LV_ANIM_OFF);
  renderedWeek=weekView;
  lv_label_set_text(statusLabel,status.c_str());calendarDirty=false;
}
static void build(){
  contentScroller=nullptr;
  root=lv_obj_create(nullptr);lv_obj_set_style_bg_color(root,color(page==0?0xFFFFFF:BG),0);lv_obj_set_style_pad_all(root,0,0);lv_obj_set_style_border_width(root,0,0);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);
  label(root,page==0?"Calendar":page==1?"Weather":"Settings",22,16,540,&lv_font_montserrat_28,page==0?INK:TEXT);
  clockLabel=label(root,"--:--",555,20,123,&lv_font_montserrat_20,page==0?INK:TEXT);
  batteryLabel = batteryIndicator(root, 680, page==0?INK:MUTED);
  if(page==0)calendarScreen();else if(page==1)weatherScreen();else settingsScreen();
  lv_obj_t*old=lv_scr_act();lv_scr_load(root);if(old!=root)lv_obj_del(old);
  redraw=false;lastMinute=0;calendarBody=page==0?calendarBody:nullptr;
}
void uiSetDimming(int percent) {
  if (!dimmer) return;
  if (percent >= 100) {
    lv_obj_add_flag(dimmer, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_set_style_bg_opa(dimmer, (100 - std::max(20, percent)) * 2, 0);
    lv_obj_clear_flag(dimmer, LV_OBJ_FLAG_HIDDEN);
  }
}
void uiBegin(){
  build();dimmer=box(lv_layer_sys(),0,0,800,480,0x000000,0);lv_obj_clear_flag(dimmer,LV_OBJ_FLAG_CLICKABLE);uiSetDimming(settings.brightness);
}
void uiTouch(bool pressed,int x,int y){
  static int sx=0,sy=0,ex=0,ey=0;static uint32_t at=0;
  if(pressed){if(!touchDown){sx=x;sy=y;at=millis();touchDown=true;}ex=x;ey=y;}
  else if(touchDown){touchDown=false;int dx=ex-sx,dy=ey-sy;
    bool editing=modal || (page==2 && sy>=dimmingTop && sy<dimmingTop+settingsRowHeight);
    if(!editing && millis()-at<1200 && abs(dx)>=100 && abs(dx)>abs(dy)*1.6f){int next=page+(dx>0?-1:1);if(next>=0 && next<=2)navigation=dx>0?-1:1;}
  }
}
void uiTick(){
  if(navigation){page+=navigation;navigation=0;redraw=true;}
  const bool rebuilt = redraw;
  if (rebuilt) build();
  const uint32_t now = millis();
  if (!rebuilt && now - lastTick < 1000) return;
  lastTick = now;

  const bool updateCalendar = page == 0 && !modal && !touchDown &&
    (!contentScroller || !lv_obj_is_scrolling(contentScroller));
  const time_t clock = time(nullptr);
  LocalForecast forecast;
  PrintRoom room;
  String todayAgenda;
  bool trendChanged = false;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (page == 1) {
    if (weatherPanel == WeatherPanel::PrintRoom && printRoom.source == settings.printRoomUrl) room = printRoom;
    {
      const bool matches = localForecast.postalCode == settings.postalCode;
      if (matches && (weatherPanel == WeatherPanel::Today || weatherPanel == WeatherPanel::Alerts)) forecast = localForecast;
      const bool hasAlerts = matches && !settings.postalCode.isEmpty() && forecastFresh(localForecast, clock) &&
          std::any_of(localForecast.alerts.begin(), localForecast.alerts.end(),
                      [clock](const auto &alert) { return activeAlert(alert, clock); });
      if (hasAlerts != alertBanner) {
        alertBanner = hasAlerts;
        lv_obj_set_style_bg_color(alertsButton, color(hasAlerts ? 0x9A491E : CARD), 0);
        updateLabel(lv_obj_get_child(alertsButton, 0), hasAlerts ? "Active alerts" : "Weather Alerts");
      }
    }
    if (weatherPanel == WeatherPanel::Today) {
      if (settings.calendarUrl.isEmpty()) todayAgenda = "Connect Google Calendar in Settings.";
      else if (shared.todayWeek != core::sunday(clock, 0)) todayAgenda = "Waiting for today's calendar...";
      else {
        const auto segments = core::daySegments(shared.todayEvents, core::midnight(clock));
        unsigned shown = 0;
        for (const auto &segment : segments) {
          const auto &event = shared.todayEvents[segment.event];
          if (core::endOf(event) <= clock || shown >= 2) continue;
          if (shown++) todayAgenda += "\n";
          todayAgenda += (event.allDay ? String("All day") : dateText(event.start, "%H:%M")) +
                         "   " + event.title.c_str();
        }
        if (!shown) todayAgenda = "No more events today.";
        if (shared.todayReceived && millis() - shared.todayReceived > 11 * 60 * 1000) todayAgenda += "\nCalendar may be out of date";
      }
    }
    if (weatherPanel == WeatherPanel::Trends) {
      const time_t newest = weatherHistory.count ? weatherHistory.at(weatherHistory.count - 1).at : 0;
      if (newest != lastTrendSample) {
        displayedHistory = weatherHistory;
        lastTrendSample = newest;
        trendChanged = true;
      }
    }
    if (shared.weather.loaded != snapshot.weather.loaded ||
        shared.weather.received != snapshot.weather.received) {
      snapshot.weather = shared.weather;
    }
    snapshot.weatherError = shared.weatherError;
  }
  if (updateCalendar) {
    if (shared.calendarLoaded != snapshot.calendarLoaded ||
        shared.calendarWeek != snapshot.calendarWeek ||
        shared.calendarReceived != snapshot.calendarReceived) {
      snapshot.events = shared.events;
    }
    snapshot.calendarLoaded = shared.calendarLoaded;
    snapshot.calendarWeek = shared.calendarWeek;
    snapshot.calendarReceived = shared.calendarReceived;
    snapshot.calendarError = shared.calendarError;
    snapshot.truncated = shared.truncated;
  }
  xSemaphoreGive(stateMutex);
  if (weatherPanel == WeatherPanel::Today) updateToday(forecast, todayAgenda, clock);
  if (weatherPanel == WeatherPanel::Alerts) updateAlerts(forecast, clock);
  if (trendChanged) drawTrend();
  if (weatherPanel == WeatherPanel::PrintRoom) updatePrintRoom(room, now, clock);
  String clockText=clock>1700000000?dateText(clock,"%H:%M"):"--:--";
  clockText+=WiFi.status()==WL_CONNECTED?String("  ")+LV_SYMBOL_WIFI:"  !";updateLabel(clockLabel,clockText.c_str());
  if(page==1)weatherUpdate();
  if(snapshot.calendarError!=lastCalendarError){calendarDirty=true;lastCalendarError=snapshot.calendarError;}
  if(updateCalendar && (calendarDirty || lastCalendar!=snapshot.calendarReceived || lastMinute!=clock/60 || shownWeek!=core::sunday(clock,requestedWeek))){
    calendarUpdate();lastCalendar=snapshot.calendarReceived;lastMinute=clock/60;
  }
}
