#include "app.hpp"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "google_roots.h"
#include "weather_roots.h"
Settings settings;
State shared;
LocalForecast localForecast;
PrintRoom printRoom;
std::atomic<bool> printRoomOpen{false};
localWeather::History weatherHistory;
SemaphoreHandle_t stateMutex;
volatile int requestedWeek=0;
std::atomic<bool> calendarRefreshRequested{false}, todayOverviewOpen{false};
static void lock(){xSemaphoreTake(stateMutex,portMAX_DELAY);}
static void unlock(){xSemaphoreGive(stateMutex);}
void loadSettings(){
  Preferences p;p.begin("wx-display",true);
  settings.ssid=p.getString("ssid","");settings.password=p.getString("password","");
  settings.station=p.getString("station","http://192.168.2.149");
  settings.calendarUrl=p.getString("calurl","");settings.calendarToken=p.getString("caltoken","");
  const String savedTimezone = p.getString("tz", core::winnipegTimeZone);
  settings.timezone = core::normalizeTimeZone(savedTimezone.c_str());
  settings.imperial=p.getBool("imperial",false);settings.brightness=p.getInt("dim",100);
  settings.screenTimeoutSeconds = ScreenIdle::normalizeTimeout(p.getUInt("screenTimeout", ScreenIdle::defaultTimeoutSeconds));
  settings.postalCode = p.getString("postal", "");
  settings.printRoomUrl = p.getString("printRoom", "");
  settings.printRoomKey = p.getString("printRoomKey", "");
  settings.rotateScreen = p.getBool("rotateScreen", false);
  p.end();
  setenv("TZ",settings.timezone.c_str(),1);tzset();
}
void saveSettings(){
  lock();Settings c=settings;unlock();
  Preferences p;p.begin("wx-display",false);
  p.putString("ssid",c.ssid);p.putString("password",c.password);p.putString("station",c.station);
  p.putString("calurl",c.calendarUrl);p.putString("caltoken",c.calendarToken);p.putString("tz",c.timezone);
  p.putBool("imperial",c.imperial);p.putInt("dim",c.brightness);
  p.putUInt("screenTimeout", c.screenTimeoutSeconds);
  p.putString("postal", c.postalCode);
  p.putString("printRoom", c.printRoomUrl);
  p.putString("printRoomKey", c.printRoomKey);
  p.putBool("rotateScreen", c.rotateScreen);
  p.end();
}
class ExternalJsonAllocator:public ArduinoJson::Allocator {
public:
  void* allocate(size_t n)override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
  void deallocate(void*p)override{heap_caps_free(p);}
  void* reallocate(void*p,size_t n)override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
};
static float number(JsonVariantConst v){return v.isNull() || (!v.is<float>() && !v.is<int>())?NAN:v.as<float>();}
static bool parseWeather(const char *body,size_t size,Weather &w){
  ExternalJsonAllocator allocator;JsonDocument d(&allocator);if(deserializeJson(d,body,size) || !d["wind_speed"].is<float>() || !d["has_bmp280"].is<bool>())return false;
  // Firmware sends canonical metric data even when its dashboard's use_imperial flag is true.
  w.temp=number(d["temp"]);w.hum=number(d["hum"]);w.dew=number(d["dew_point"]);
  w.min=number(d["temp_min"]);w.max=number(d["temp_max"]);w.press=number(d["press"]);
  w.wind=number(d["wind_speed"]);w.gust=number(d["wind_gust"]);w.gust10=number(d["wind_gust_10m"]);w.dir=number(d["wind_dir"]);
  w.rain=number(d["daily_rain"]);w.hour=number(d["hourly_rain"]);w.total=number(d["total_rain"]);
  w.lux=number(d["lux"]);w.solar=number(d["solar_radiation"]);w.aqi=number(d["aqi"]);w.eco2=number(d["eco2"]);w.tvoc=number(d["tvoc"]);w.vcc=number(d["vcc"]);
  w.tips=d["tips"]|0;w.freeHeap=d["heap"]|0;w.raining=d["is_raining"]|false;
  w.sensors=String("AHT20: ")+(d["has_aht20"].as<bool>()?"online":"offline")+
    " | BMP280: "+(d["has_bmp280"].as<bool>()?"online":"offline")+
    " | ENS160: "+(d["has_ens160"].as<bool>()?"online":"offline")+
    " | AS5600: "+(d["has_as5600"].as<bool>()?"online":"offline")+
    " | BH1750: "+(d["has_bh1750"].as<bool>()?"online":"offline");w.firmware=d["fw_version"]|"Unknown";w.uptime=d["uptime"]|"";
  w.ip=d["ip"]|"";w.ssid=d["ssid"]|"";w.rssi=d["rssi"]|"";w.stationTime=d["time"]|"";
  w.loaded=true;w.received=millis();return true;
}
// HTTPClient decodes chunked bodies into this bounded external-RAM sink.
// This avoids assuming Google's redirected response has Content-Length.
class BoundedResponse:public Stream {
  size_t limit;
public:
  core::EventText data;
  explicit BoundedResponse(size_t cap):limit(cap){}
  int available()override{return 0;}
  int read()override{return -1;}
  int peek()override{return -1;}
  void flush()override{}
  size_t write(uint8_t value)override{return write(&value,1);}
  size_t write(const uint8_t *p,size_t n)override{
    if(data.size()+n>limit)return 0;
    data.append(reinterpret_cast<const char*>(p),n);return n;
  }
};
static bool fetchWeather(const Settings &c,Weather &w,String &error){
  WiFiClient client;HTTPClient http;String url=c.station;
  while(url.endsWith("/"))url.remove(url.length()-1);
  if(!url.startsWith("http://")){error="Use http:// followed by the station IP";return false;}
  if(!http.begin(client,url+"/api/status")){error="Invalid station address";return false;}
  http.setConnectTimeout(2500);http.setTimeout(2500);http.useHTTP10(true);
  int code=http.GET();bool ok=false;
  if(code==200 && (http.getSize()<0 || http.getSize()<=12288)){
    BoundedResponse body(12288);
    int read=http.writeToStream(&body);
    ok=read>=0 && parseWeather(body.data.data(),body.data.size(),w);
    if(!ok)error="Station returned unexpected data";
  }else error=code>0?"Station HTTP "+String(code):"Station not reachable";
  http.end();return ok;
}
static bool publicJson(const String &url, JsonDocument &result, size_t cap, JsonDocument *filter, String &error) {
  WiFiClientSecure tls;
  tls.setCACert(WEATHER_ROOTS);
  tls.setHandshakeTimeout(5);
  HTTPClient http;
  if (!http.begin(tls, url)) { error = "invalid URL"; return false; }
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  http.useHTTP10(true);
  const int code = http.GET();
  if (code != 200) {
    error = code > 0 ? String("HTTP ") + String(code) : String("connection error ") + String(code);
    return false;
  }
  BoundedResponse body(cap);
  if (http.getSize() > static_cast<int>(cap) || http.writeToStream(&body) < 0) {
    error = "response could not be read";
    return false;
  }
  const auto status = deserializeJson(result, body.data.data(), body.data.size(),
                                      DeserializationOption::Filter(*filter),
                                      DeserializationOption::NestingLimit(32));
  if (status) error = String("invalid response: ") + status.c_str();
  return !status;
}

static constexpr char cityPages[] =
    "https://api.weather.gc.ca/collections/citypageweather-realtime/items";

static String cityForPostalCode(const String &postal, String &error) {
  ExternalJsonAllocator allocator;
  JsonDocument lookup(&allocator), placesFilter;
  placesFilter["places"][0]["latitude"] = true;
  placesFilter["places"][0]["longitude"] = true;
  if (!publicJson(String("https://api.zippopotam.us/CA/") + postal, lookup, 16000, &placesFilter, error)) {
    error = String("Postal lookup: ") + error;
    return "";
  }
  double lat = 0, lon = 0;
  unsigned count = 0;
  for (JsonObject place : lookup["places"].as<JsonArray>()) {
    const double y = atof(place["latitude"] | "0");
    const double x = atof(place["longitude"] | "0");
    if (!(y >= 40 && y <= 84 && x >= -142 && x <= -50)) continue;
    lat += y;
    lon += x;
    ++count;
  }
  if (!count) { error = "Postal area has no usable coordinates"; return ""; }
  lat /= count;
  lon /= count;
  const double lonScale = cos(lat * 0.01745329252);
  for (double radius : {0.5, 1.5, 3.0}) {
    JsonDocument cities(&allocator), filter;
    filter["features"][0]["id"] = true;
    filter["features"][0]["geometry"]["coordinates"] = true;
    // Use the standard GeoMet bbox query without a collection-specific field projection.
    const String url = String(cityPages) + "?f=json&limit=5&bbox=" +
        String(lon - radius / lonScale, 4) + "," + String(lat - radius, 4) + "," +
        String(lon + radius / lonScale, 4) + "," + String(lat + radius, 4);
    filter["numberMatched"] = true;
    double best = INFINITY;
    String id;
    // Full city features include forecasts. Read small batches and retain only coordinates and IDs.
    for (unsigned offset = 0; ; offset += 5) {
      if (!publicJson(url + "&offset=" + String(offset), cities, 800000, &filter, error)) {
        error = String("Environment Canada location search: ") + error;
        return "";
      }
      const auto features = cities["features"].as<JsonArray>();
      for (JsonObject city : features) {
        const char *candidate = city["id"] | "";
        if (strlen(candidate) > 20 || !*candidate ||
            strspn(candidate, "abcdefghijklmnopqrstuvwxyz0123456789-") != strlen(candidate)) continue;
        auto coordinates = city["geometry"]["coordinates"].as<JsonArray>();
        if (coordinates.size() != 2) continue;
        const double dx = (coordinates[0].as<double>() - lon) * lonScale;
        const double dy = coordinates[1].as<double>() - lat;
        const double distance = dx * dx + dy * dy;
        if (distance < best && distance <= radius * radius) {
          best = distance;
          id = candidate;
        }
      }
      if (features.size() < 5 || offset + 5 >= (cities["numberMatched"] | 100u)) break;
    }
    if (id.length()) return id;
  }
  error = "No nearby Environment Canada forecast location found";
  return "";
}

static String english(JsonVariantConst value) {
  if (value.is<const char *>()) return value.as<const char *>();
  return value["en"] | "";
}

static bool fetchLocalForecast(const String &id, LocalForecast &result) {
  ExternalJsonAllocator allocator;
  JsonDocument data(&allocator), filter;
  JsonObject properties = filter["properties"].to<JsonObject>();
  properties["name"]["en"] = true;
  properties["lastUpdated"] = true;
  properties["region"]["en"] = true;
  properties["warnings"] = true;
  JsonObject hourly = properties["hourlyForecastGroup"]["hourlyForecasts"][0].to<JsonObject>();
  hourly["timestamp"] = true;
  hourly["temperature"]["value"]["en"] = true;
  hourly["lop"]["value"]["en"] = true;
  hourly["condition"]["en"] = true;
  if (!publicJson(String(cityPages) + "/" + id + "?f=json", data, 160000, &filter, result.error)) {
    result.error = String("Environment Canada feed: ") + result.error;
    return false;
  }
  auto city = data["properties"];
  result.location = english(city["name"]);
  result.region = english(city["region"]);
  if (result.location.isEmpty()) return false;
  for (JsonObject row : city["hourlyForecastGroup"]["hourlyForecasts"].as<JsonArray>()) {
    const time_t at = localWeather::utcTimestamp(row["timestamp"] | "");
    if (!at || result.hours.size() >= 48) continue;
    result.hours.push_back({at, number(row["temperature"]["value"]["en"]),
                            row["lop"]["value"]["en"] | -1, english(row["condition"])});
  }
  JsonVariantConst warnings = city["warnings"];
  JsonArrayConst events = warnings.is<JsonArrayConst>() ? warnings.as<JsonArrayConst>() :
                          warnings["events"].as<JsonArrayConst>();
  result.alertsKnown = !events.isNull() ||
      (warnings.is<JsonObjectConst>() && warnings.size() == 0);
  for (JsonObjectConst row : events) {
    String title = english(row["description"]);
    if (title.isEmpty()) title = english(row["type"]);
    String expires = english(row["expiryTime"]);
    if (expires.isEmpty()) expires = english(row["expiry"]);
    if (title.length() && result.alerts.size() < 24)
      result.alerts.push_back({title, localWeather::utcTimestamp(expires.c_str())});
    else if (title.isEmpty()) result.alertsKnown = false;
  }
  result.checked = time(nullptr);
  const auto published = localWeather::utcTimestamp(city["lastUpdated"] | "");
  if (published && result.checked - published > 2 * 60 * 60)
    result.error = "Environment Canada data is out of date";
  return true;
}

static bool trustedCalendarURL(const String &url){
  return url.startsWith("https://script.google.com/macros/s/") && url.endsWith("/exec");
}
static bool fetchCalendar(const Settings &c,time_t week,core::Events &events,bool &truncated,String &error){
  if(!trustedCalendarURL(c.calendarUrl)){error="Enter the Google Apps Script /exec URL in Settings";return false;}
  if(c.calendarToken.length()<32){error="Enter your calendar bridge token in Settings";return false;}
  // Token generated by setup() is hex with no escaping needed. Reject anything else.
  for(size_t i=0;i<c.calendarToken.length();++i)if(!isxdigit(c.calendarToken[i])){error="Calendar token must contain hex characters only";return false;}
  WiFiClientSecure tls;tls.setCACert(GOOGLE_ROOTS);tls.setHandshakeTimeout(12);
  HTTPClient http;
  String url=c.calendarUrl+"?token="+c.calendarToken+"&from="+String((uint32_t)week)+"&to="+String((uint32_t)core::addDays(week,7));
  if(!http.begin(tls,url)){error="Invalid calendar URL";return false;}
  http.setConnectTimeout(8000);http.setTimeout(12000);http.useHTTP10(true);
  // Google ContentService redirects to a one-time HTTPS script.googleusercontent.com URL.
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);http.setRedirectLimit(3);
  int code=http.GET();bool ok=false;
  if(code==200 && (http.getSize()<0 || http.getSize()<=100000)){
    ExternalJsonAllocator allocator;JsonDocument d(&allocator);
    BoundedResponse body(100000);int received=http.writeToStream(&body);
    DeserializationError e=deserializeJson(d,body.data.data(),body.data.size());
    if(received<0 || e || !d["events"].is<JsonArray>())error=d["error"].is<const char*>()?String(d["error"].as<const char*>()):"Calendar response invalid; check deployment access";
    else{
      for(JsonObject o:d["events"].as<JsonArray>()){
        if(events.size()>=100)break;
        core::Event ev;ev.title=o["title"]|"(Untitled)";ev.location=o["location"]|"";
        ev.description=o["description"]|"";ev.calendar=o["calendar"]|"";ev.allDay=o["allDay"]|false;
        ev.start=o["start"]|0;ev.end=o["end"]|0;ev.startDate=o["startDate"]|"";ev.endDate=o["endDate"]|"";
        const char *color=o["color"]|"#4285f4";ev.color=strtoul(color[0]=='#'?color+1:color,nullptr,16)&0xffffff;
        if(core::endOf(ev)>core::startOf(ev))events.push_back(std::move(ev));
      }
      truncated=d["truncated"]|false;ok=true;
    }
  }else error=code>0?"Calendar HTTP "+String(code):"Calendar connection failed; check Wi-Fi and clock";
  http.end();return ok;
}
static void networkTask(void*){
  WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(true);
  String lastSSID,lastPassword,lastTZ,lastStation,lastPostal,cityId;
  uint32_t localAt = 0;
  bool localAttempted = false;
  constexpr uint32_t calendarPollIntervalMs = 5 * 60 * 1000;
  uint32_t weatherAt = 0, calendarAt = 0, lastReconnect = 0;
  bool calendarAttempted = false;
  bool first = true, wasConnected = false;
  for(;;){
    lock();Settings c=settings;unlock();
    if(c.timezone!=lastTZ){configTzTime(c.timezone.c_str(),"time.google.com","pool.ntp.org");lastTZ=c.timezone;}
    if(c.ssid!=lastSSID || c.password!=lastPassword || first){
      WiFi.disconnect();if(c.ssid.length())WiFi.begin(c.ssid.c_str(),c.password.c_str());
      lastSSID=c.ssid;lastPassword=c.password;first=false;lastReconnect=millis();
    }
    if(WiFi.status()!=WL_CONNECTED){
      wasConnected=false;
      lock();if(localForecast.postalCode==c.postalCode)localForecast.error="Wi-Fi disconnected";shared.weatherError=c.ssid.isEmpty()?"Swipe right to set up Wi-Fi":"Connecting to Wi-Fi...";if(shared.calendarLoaded)shared.calendarError="Wi-Fi disconnected";unlock();
      if(c.ssid.length() && millis()-lastReconnect>20000){WiFi.reconnect();lastReconnect=millis();}
      vTaskDelay(pdMS_TO_TICKS(300));continue;
    }
    bool justConnected = !wasConnected;
    wasConnected = true;
    uint32_t now = millis();
    if (justConnected || c.station != lastStation || now - weatherAt >= weatherPollIntervalMs) {
      if (lastStation != c.station) {
        lock();weatherHistory.next = weatherHistory.count = 0;unlock();
      }
      lastStation = c.station;
      Weather w;String error;bool ok=fetchWeather(c,w,error);
      lock();if(c.station==settings.station){if(ok){const time_t sampled = time(nullptr);
        if (sampled > 1700000000) weatherHistory.add({sampled, {w.temp, w.press, w.gust10, w.rain}});
        shared.weather=std::move(w);shared.weatherError="";}else shared.weatherError=error;}unlock();weatherAt=millis();
    }
    const time_t clock = time(nullptr);
    if (clock > 1700000000) {
      const bool manualRefresh = calendarRefreshRequested.exchange(false);
      if (manualRefresh || !calendarAttempted || millis() - calendarAt >= calendarPollIntervalMs) {
        lock();
        c = settings;
        unlock();
        const time_t selectedWeek = core::sunday(clock, requestedWeek);
        const time_t week = !manualRefresh && todayOverviewOpen.load() ?
            core::sunday(clock, 0) : selectedWeek;
        core::Events events;
        String error;
        bool truncated = false;
        const bool ok = fetchCalendar(c, week, events, truncated, error);
        calendarAt = millis();
        calendarAttempted = true;

        lock();
        if (c.calendarUrl == settings.calendarUrl && c.calendarToken == settings.calendarToken &&
            c.timezone == settings.timezone) {
          if (ok) {
            if (week == core::sunday(clock, 0)) {
              shared.todayEvents = events;
              shared.todayWeek = week;
              shared.todayReceived = millis();
            }
            if (week == selectedWeek) {
              shared.events = std::move(events);
              shared.calendarWeek = week;
              shared.calendarLoaded = true;
              shared.truncated = truncated;
              shared.calendarReceived = millis();
              shared.calendarError = "";
            }
          } else if (week == selectedWeek) {
            shared.calendarError = error;
          }
        }
        unlock();
      }
    } else {
      lock();
      shared.calendarError = "Waiting for internet time sync";
      unlock();
    }
    if (c.postalCode.isEmpty()) {
      lastPostal = "";
      cityId = "";
      localAttempted = false;
    }
    if (clock > 1700000000 && !c.postalCode.isEmpty() &&
        (justConnected || !localAttempted || c.postalCode != lastPostal || millis() - localAt >= weatherPollIntervalMs)) {
      if (c.postalCode != lastPostal) cityId = "";
      lastPostal = c.postalCode;
      LocalForecast result;
      if (cityId.isEmpty()) cityId = cityForPostalCode(c.postalCode, result.error);
      result.postalCode = c.postalCode;
      const bool ok = !cityId.isEmpty() && fetchLocalForecast(cityId, result);
      lock();
      if (c.postalCode == settings.postalCode) {
        if (ok) localForecast = std::move(result);
        else {
          if (localForecast.postalCode != c.postalCode) localForecast = LocalForecast{};
          localForecast.postalCode = c.postalCode;
          localForecast.error = result.error.isEmpty() ? String("Environment Canada feed unavailable") : result.error;
        }
      }
      unlock();
      localAt = millis();
      localAttempted = true;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}
static bool fetchPrintRoom(const String &url, const String &key, PrintRoom &result) {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", String("Bearer ") + key);
  http.setConnectTimeout(2000);
  http.setTimeout(2500);
  http.useHTTP10(true);
  if (http.GET() != 200 || http.getSize() > 4096) return false;
  BoundedResponse body(4096);
  if (http.writeToStream(&body) < 0) return false;
  JsonDocument data;
  if (deserializeJson(data, body.data.data(), body.data.size()) ||
      data["schema"] != 1 || !data["readings"].is<JsonObject>()) return false;
  JsonObjectConst readings = data["readings"].as<JsonObjectConst>();
  result.source = url;
  result.printer = data["printer"] | "Bambu Lab X2D";
  result.status = readings["status"] | "";
  result.job = readings["job"] | "";
  result.stage = readings["stage"] | "";
  result.progress = number(readings["progress"]);
  result.remainingMinutes = number(readings["remaining_minutes"]);
  result.layer = number(readings["layer"]);
  result.layers = number(readings["layers"]);
  result.nozzle = number(readings["nozzle"]);
  result.bed = number(readings["bed"]);
  result.chamber = number(readings["chamber"]);
  result.roomTemp = number(readings["room_temp"]);
  result.humidity = number(readings["humidity"]);
  result.pm25 = number(readings["pm25"]);
  result.finishAt = readings["finish_at"] | time_t{0};
  result.received = millis();
  result.loaded = true;
  return true;
}

static void printRoomTask(void *) {
  uint32_t lastPoll = 0;
  bool wasOpen = false;
  for (;;) {
    const bool open = printRoomOpen.load();
    if (open && (!wasOpen || millis() - lastPoll >= 3000)) {
      lock();
      String url = settings.printRoomUrl;
      String key = settings.printRoomKey;
      unlock();
      if (validPrintRoomUrl(url) && !url.isEmpty() && validPrintRoomKey(key)) {
        PrintRoom result;
        if (WiFi.status() != WL_CONNECTED) result.error = "Wi-Fi disconnected";
        else if (!fetchPrintRoom(url, key, result)) result.error = "Print Room bridge unavailable";
        lock();
        if (url == settings.printRoomUrl && key == settings.printRoomKey) {
          if (result.loaded) printRoom = std::move(result);
          else {
            if (printRoom.source != url) printRoom = PrintRoom{};
            printRoom.source = url;
            printRoom.error = result.error;
          }
        }
        unlock();
      }
      lastPoll = millis();
    }
    wasOpen = open;
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void startNetwork() {
  assert(xTaskCreatePinnedToCore(networkTask, "network", 16384, nullptr, 1, nullptr, 0) == pdPASS);
  assert(xTaskCreatePinnedToCore(printRoomTask, "printRoom", 8192, nullptr, 1, nullptr, 0) == pdPASS);
}
