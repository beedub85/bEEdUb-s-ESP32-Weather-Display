#include "app.hpp"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "google_roots.h"
Settings settings;
State shared;
SemaphoreHandle_t stateMutex;
volatile int requestedWeek=0;
std::atomic<bool> calendarRefreshRequested{false};
static void lock(){xSemaphoreTake(stateMutex,portMAX_DELAY);}
static void unlock(){xSemaphoreGive(stateMutex);}
void loadSettings(){
  Preferences p;p.begin("wx-display",true);
  settings.ssid=p.getString("ssid","");settings.password=p.getString("password","");
  settings.station=p.getString("station","http://192.168.2.149");
  settings.calendarUrl=p.getString("calurl","");settings.calendarToken=p.getString("caltoken","");
  const String savedTimezone = p.getString("tz", core::winnipegTimeZone);
  settings.timezone = core::normalizeTimeZone(savedTimezone.c_str());
  settings.imperial=p.getBool("imperial",false);settings.brightness=p.getInt("dim",100);p.end();
  setenv("TZ",settings.timezone.c_str(),1);tzset();
}
void saveSettings(){
  lock();Settings c=settings;unlock();
  Preferences p;p.begin("wx-display",false);
  p.putString("ssid",c.ssid);p.putString("password",c.password);p.putString("station",c.station);
  p.putString("calurl",c.calendarUrl);p.putString("caltoken",c.calendarToken);p.putString("tz",c.timezone);
  p.putBool("imperial",c.imperial);p.putInt("dim",c.brightness);p.end();
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
  String lastSSID,lastPassword,lastTZ,lastStation;
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
      lock();shared.weatherError=c.ssid.isEmpty()?"Swipe left to set up Wi-Fi":"Connecting to Wi-Fi...";if(shared.calendarLoaded)shared.calendarError="Wi-Fi disconnected";unlock();
      if(c.ssid.length() && millis()-lastReconnect>20000){WiFi.reconnect();lastReconnect=millis();}
      vTaskDelay(pdMS_TO_TICKS(300));continue;
    }
    bool justConnected = !wasConnected;
    wasConnected = true;
    uint32_t now = millis();
    if (justConnected || c.station != lastStation || now - weatherAt >= weatherPollIntervalMs) {
      lastStation = c.station;
      Weather w;String error;bool ok=fetchWeather(c,w,error);
      lock();if(c.station==settings.station){if(ok){shared.weather=std::move(w);shared.weatherError="";}else shared.weatherError=error;}unlock();weatherAt=millis();
    }
    const time_t clock = time(nullptr);
    if (clock > 1700000000) {
      const bool manualRefresh = calendarRefreshRequested.exchange(false);
      if (manualRefresh || !calendarAttempted || millis() - calendarAt >= calendarPollIntervalMs) {
        lock();
        c = settings;
        unlock();
        const time_t week = core::sunday(clock, requestedWeek);
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
            shared.events = std::move(events);
            shared.calendarWeek = week;
            shared.calendarLoaded = true;
            shared.truncated = truncated;
            shared.calendarReceived = millis();
            shared.calendarError = "";
          } else {
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
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}
void startNetwork(){assert(xTaskCreatePinnedToCore(networkTask,"network",16384,nullptr,1,nullptr,0)==pdPASS);}
