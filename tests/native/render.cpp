#include "app.hpp"
#include "WiFi.h"
#include <cassert>
#include <fstream>
#include <iostream>
uint32_t testMillis=10000;
Settings settings;State shared;LocalForecast localForecast;PrintRoom printRoom;std::atomic<bool> printRoomOpen{false};localWeather::History weatherHistory;SemaphoreHandle_t stateMutex=nullptr;
volatile int requestedWeek=0;std::atomic<bool> calendarRefreshRequested{false}, todayOverviewOpen{false};FakeWiFi WiFi;
void saveSettings(){}void loadSettings(){}void startNetwork(){}
static lv_color_t fb[800*480],buf[800*32];
static void flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*p){for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)fb[y*800+x]=*p++;lv_disp_flush_ready(d);}
static void frames(){for(int i=0;i<5;++i){testMillis+=30;lv_tick_inc(30);uiTick();lv_timer_handler();}}
static void capture(const char*name){lv_obj_invalidate(lv_scr_act());lv_obj_invalidate(lv_layer_top());testMillis+=1100;lv_tick_inc(1100);frames();std::ofstream f(name,std::ios::binary);f<<"P6\n800 480\n255\n";for(auto c:fb){auto c32=lv_color_to32(c);char rgb[3]={(char)((c32>>16)&255),(char)((c32>>8)&255),(char)(c32&255)};f.write(rgb,3);}}
static void swipe(bool right){uiTouch(true,400,170);uiTouch(true,right?600:200,170);uiTouch(false,0,0);frames();}
static lv_obj_t *findButton(lv_obj_t*p,const char*text){for(int i=0;i<lv_obj_get_child_cnt(p);++i){auto*c=lv_obj_get_child(p,i);if(lv_obj_check_type(c,&lv_label_class) && std::string(lv_label_get_text(c))==text && lv_obj_check_type(p,&lv_btn_class))return p;auto*r=findButton(c,text);if(r)return r;}return nullptr;}
static lv_obj_t *findLabel(lv_obj_t*p,const char*text){
 for(int i=0;i<lv_obj_get_child_cnt(p);++i){auto*c=lv_obj_get_child(p,i);if(lv_obj_check_type(c,&lv_label_class) && std::string(lv_label_get_text(c))==text)return c;auto*r=findLabel(c,text);if(r)return r;}return nullptr;
}
int main(int argc,char**argv){
 setenv("TZ",settings.timezone.c_str(),1);tzset();lv_init();
 static lv_disp_draw_buf_t b;lv_disp_draw_buf_init(&b,buf,nullptr,800*32);
 static lv_disp_drv_t d;lv_disp_drv_init(&d);d.hor_res=800;d.ver_res=480;d.draw_buf=&b;d.flush_cb=flush;lv_disp_drv_register(&d);
 auto&w=shared.weather;w.loaded=true;w.received=10000;w.temp=18.6;w.hum=62;w.dew=11.2;w.min=12.4;w.max=20.1;w.wind=12;w.gust=19;w.dir=45;w.press=1013;w.raining=true;w.rain=2.4;w.hour=0.8;w.lux=8420;w.solar=75;w.aqi=2;w.eco2=620;w.tvoc=84;shared.weatherError="";
 time_t now=time(nullptr);time_t week=core::sunday(now);shared.calendarLoaded=true;shared.calendarWeek=week;shared.calendarReceived=10000;shared.calendarError="";
 time_t day=core::midnight(now);
 for(int i=0;i<5;++i){core::Event e;e.title=i%2?"Project planning":"Team meeting";e.location="Office";e.calendar="Personal";e.start=day+9*3600+i*90*60;e.end=e.start+3600;e.color=i%2?0x34A853:0x4285F4;shared.events.push_back(e);}
 core::Event all;all.title="All-day reminder";all.allDay=true;char dates[16];tm t=*localtime(&day);strftime(dates,16,"%Y-%m-%d",&t);all.startDate=dates;day=core::addDays(day,1);t=*localtime(&day);strftime(dates,16,"%Y-%m-%d",&t);all.endDate=dates;shared.events.push_back(all);
 uiBegin();frames();
 assert(findLabel(lv_scr_act(), LV_SYMBOL_BATTERY_FULL " N/C"));
 uiSetBatteryPercent(74);
 assert(findLabel(lv_scr_act(), LV_SYMBOL_BATTERY_FULL " 74%"));
 capture("weather.ppm");
 // Tap each card through its LVGL event and verify that its details are scoped.
 struct CardCase { const char *title; const char *reading; };
 const CardCase cards[] = {
   {"TEMPERATURE", "Temperature"}, {"HUMIDITY", "Relative humidity"},
   {"WIND", "Wind speed"}, {"PRESSURE", "Atmospheric pressure"},
   {"RAINFALL", "Rain (rolling 24 hours)"}, {"LIGHT", "Light"},
   {"AIR QUALITY", "ENS160 AQI"}
 };
 for (const auto &card : cards) {
   auto *title = findLabel(lv_scr_act(), card.title);
   assert(title);
   lv_event_send(lv_obj_get_parent(title), LV_EVENT_CLICKED, nullptr);
   auto *heading = findLabel(lv_layer_top(), card.title);
   assert(heading);
   auto *body = lv_obj_get_child(lv_obj_get_parent(heading), 3);
   const std::string readings = lv_label_get_text(lv_obj_get_child(body, 0));
   for (const auto &other : cards) {
     const bool containsReading = readings.find(other.reading) != std::string::npos;
     assert(containsReading == (&card == &other));
   }
   lv_event_send(findButton(lv_layer_top(), "Close"), LV_EVENT_CLICKED, nullptr);
 }

 // Render real condition changes through the same published weather snapshot.
 struct ConditionCase{const char*file;float lux;bool raining;const char*caption;};
 for(const auto&c:std::vector<ConditionCase>{{"weather-sunny.ppm",35000,false,"Sunny (est.)"},{"weather-partly-cloudy.ppm",8000,false,"Partly cloudy (est.)"},{"weather-cloudy.ppm",2000,false,"Cloudy (est.)"},{"weather-low-light.ppm",5,false,"Low light"},{"weather-raining.ppm",35000,true,"Rain detected"},{"weather-unavailable.ppm",NAN,false,"Sky unavailable"}}){
   w.lux=c.lux;w.raining=c.raining;w.received=testMillis;testMillis+=1100;lv_tick_inc(1100);frames();
   assert(findLabel(lv_scr_act(),c.caption));capture(c.file);
 }
 w.lux=8420;w.raining=true;w.received=testMillis;testMillis+=weatherStaleAfterMs+1;lv_tick_inc(weatherStaleAfterMs+1);frames();assert(findLabel(lv_scr_act(),"Sky unavailable"));
 w.received=testMillis;testMillis+=1100;lv_tick_inc(1100);frames();swipe(false);capture("calendar-today.ppm");
 auto*weekButton=findButton(lv_scr_act(),"Week");assert(weekButton);lv_event_send(weekButton,LV_EVENT_CLICKED,nullptr);frames();capture("calendar-week.ppm");
 assert(!calendarRefreshRequested.load());
 auto *refreshButton = findButton(lv_scr_act(), LV_SYMBOL_REFRESH);
 assert(refreshButton);
 lv_event_send(refreshButton, LV_EVENT_CLICKED, nullptr);
 assert(calendarRefreshRequested.exchange(false));
 auto*hour=findLabel(lv_scr_act(),"09:00");assert(hour);
 auto*scroller=lv_obj_get_parent(lv_obj_get_parent(hour));lv_obj_scroll_to_y(scroller,14*54,LV_ANIM_OFF);
 testMillis+=61000;lv_tick_inc(61000);frames();
 hour=findLabel(lv_scr_act(),"09:00");assert(hour);
 assert(lv_obj_get_scroll_y(lv_obj_get_parent(lv_obj_get_parent(hour)))==14*54);
 swipe(true);swipe(true);capture("settings.ppm");
 // An outward swipe is consumed even though Settings has no neighbor on that side.
 uiTouch(true, 400, 80);
 uiTouch(true, 600, 80);
 assert(uiTouch(false, 0, 0));
 frames();
 assert(findLabel(lv_scr_act(), "Settings"));
 uiTouch(true, 400, 80);
 assert(!uiTouch(false, 0, 0));
 // A swipe along the dimming row must not navigate away from Settings.
 uiTouch(true,350,204);uiTouch(true,650,204);uiTouch(false,0,0);frames();assert(std::string(lv_label_get_text(lv_obj_get_child(lv_scr_act(),0)))=="Settings");
 assert(!findButton(lv_scr_act(),"Calendar") && !findButton(lv_scr_act(),"Weather") && !findButton(lv_scr_act(),"Settings"));
 auto *rotation = findLabel(lv_scr_act(), "Rotate screen 180°");
 assert(rotation);
 auto *rotationSwitch = lv_obj_get_child(lv_obj_get_parent(rotation), 1);
 assert(lv_obj_check_type(rotationSwitch, &lv_switch_class));
 lv_obj_add_state(rotationSwitch, LV_STATE_CHECKED);
 lv_event_send(rotationSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
 assert(settings.rotateScreen);
 lv_obj_clear_state(rotationSwitch, LV_STATE_CHECKED);
 lv_event_send(rotationSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
 assert(!settings.rotateScreen);
 auto *timeout = findLabel(lv_scr_act(), "Screen timeout");
 assert(timeout && findLabel(lv_scr_act(), "1 minute  >"));
 lv_event_send(lv_obj_get_parent(timeout), LV_EVENT_CLICKED, nullptr);
 auto *twoMinutes = findButton(lv_layer_top(), "2 minutes");
 assert(twoMinutes);
 lv_event_send(twoMinutes, LV_EVENT_CLICKED, nullptr);
 frames();
 assert(settings.screenTimeoutSeconds == 120 && findLabel(lv_scr_act(), "2 minutes  >"));
 auto*wifi=findLabel(lv_scr_act(),"Wi-Fi");assert(wifi);lv_event_send(lv_obj_get_parent(wifi),LV_EVENT_CLICKED,nullptr);frames();capture("wifi-editor.ppm");
 swipe(true);assert(findButton(lv_layer_top(),"Close"));
 lv_event_send(findButton(lv_layer_top(),"Close"),LV_EVENT_CLICKED,nullptr);frames();
 auto*zone=findLabel(lv_scr_act(),"Time zone");assert(zone);lv_event_send(lv_obj_get_parent(zone),LV_EVENT_CLICKED,nullptr);frames();capture("timezone-picker.ppm");
 auto*close=findButton(lv_layer_top(),"Close");assert(close);lv_area_t area;lv_obj_get_coords(close,&area);assert(area.x1>=0 && area.y1>=0 && area.x2<800 && area.y2<480);
 assert(findButton(lv_layer_top(),"Eastern (Toronto)"));
 lv_event_send(findButton(lv_layer_top(),"Eastern (Toronto)"),LV_EVENT_CLICKED,nullptr);frames();
 assert(settings.timezone=="EST5EDT,M3.2.0,M11.1.0");
 auto sendCalendarLine = [](const std::string &line) {
   for (char byte : line) assert(uiCalendarUsbReceive(byte) == CalendarUsbResult::Pending);
   return uiCalendarUsbReceive('\n');
 };
 auto openCalendarUsb = [] {
   auto *calendar = findLabel(lv_scr_act(), "Google Calendar");
   assert(calendar);
   lv_event_send(lv_obj_get_parent(calendar), LV_EVENT_CLICKED, nullptr);
   auto *usb = findButton(lv_layer_top(), "Set up over USB");
   assert(usb);
   lv_event_send(usb, LV_EVENT_CLICKED, nullptr);
 };
 const std::string url = "https://script.google.com/macros/s/" + std::string(360, 'A') + "/exec";
 const std::string token(128, 'a');
 const std::string command = "CALENDAR " + url + " " + token;
 assert(sendCalendarLine(command) == CalendarUsbResult::Pending);
 openCalendarUsb();
 assert(sendCalendarLine(command + "extra") == CalendarUsbResult::Invalid);
 assert(settings.calendarUrl.isEmpty());
 assert(sendCalendarLine(command) == CalendarUsbResult::Saved);
 assert(settings.calendarUrl == url.c_str() && settings.calendarToken == token.c_str());
 assert(calendarRefreshRequested.exchange(false));
 frames();
 openCalendarUsb();
 testMillis += 120000;
 uiCalendarUsbExpire();
 assert(sendCalendarLine(command) == CalendarUsbResult::Pending);
 lv_event_send(findButton(lv_layer_top(), "Close"), LV_EVENT_CLICKED, nullptr);
 openCalendarUsb();
 lv_event_send(findButton(lv_layer_top(), "Close"), LV_EVENT_CLICKED, nullptr);
 assert(sendCalendarLine(command) == CalendarUsbResult::Pending);
 std::cout<<"PASS: calendar USB setup accepts valid credentials only while enabled\n";
 swipe(false);
 auto openPanel = [](const char *name) {
   auto *panel = findButton(lv_scr_act(), name);
   if (!panel) std::cerr << "Missing dashboard button: " << name << "\n";
   assert(panel);
   lv_event_send(panel, LV_EVENT_CLICKED, nullptr);
   frames();
 };
 auto closePanel = [] {
   lv_event_send(findButton(lv_layer_top(), "Close"), LV_EVENT_CLICKED, nullptr);
   frames();
 };
 settings.postalCode = "R0J";
 localForecast.postalCode = settings.postalCode;
 localForecast.location = "Minnedosa";
 localForecast.region = "Rural Manitoba";
 localForecast.checked = time(nullptr);
 const time_t nextHour = localForecast.checked - localForecast.checked % 3600 + 3600;
 for (int i = 0; i < 4; ++i)
   localForecast.hours.push_back({nextHour + i * 3600, float(-4 - i), 20 + i * 10, "Partly cloudy"});
 localForecast.alerts.push_back({"Sample snowfall warning", localForecast.checked + 14400});
 shared.todayWeek = core::sunday(localForecast.checked, 0);
 auto appointment = core::Event{};
 appointment.title = "Check enclosure fit";
 appointment.start = localForecast.checked + 60;
 appointment.end = appointment.start + 900;
 shared.todayEvents.push_back(appointment);
 for (int i = 0; i < 140; ++i)
   weatherHistory.add({localForecast.checked - (139 - i) * 300,
                       {4.0f + std::sin(i / 30.0f) * 5, 1014, 15, 3}});
 testMillis += 1000;
 frames();
 localForecast.accumulationSummary = "Today: Rainfall amount 5 mm.\nTonight: Snowfall amount 2 to 4 cm.";
 openPanel("Today at a Glance");
 assert(findLabel(lv_layer_top(), localForecast.accumulationSummary.c_str()));
 capture("today-at-a-glance.ppm");
 closePanel();
 openPanel("Weather Trends");capture("weather-trends.ppm");closePanel();
 openPanel("Active alerts");capture("weather-alerts.ppm");closePanel();
 settings.printRoomUrl = "http://192.168.1.10:8787/status";
 settings.printRoomKey = "0123456789abcdef0123456789abcdef";
 printRoom.source = settings.printRoomUrl;
 printRoom.printer = "Bambu Lab X2D";
 printRoom.status = "Printing";
 printRoom.job = "Weather display enclosure";
 printRoom.progress = 68;
 printRoom.remainingMinutes = 42;
 printRoom.layer = 142;
 printRoom.layers = 210;
 printRoom.nozzle = 225;
 printRoom.bed = 65;
 printRoom.chamber = 34;
 printRoom.roomTemp = 22.4;
 printRoom.humidity = 41;
 printRoom.pm25 = 6;
 printRoom.finishAt = time(nullptr) + 2520;
 printRoom.loaded = true;
 printRoom.received = testMillis;
 openPanel("Print Room");
 assert(printRoomOpen.load());
 assert(findLabel(lv_layer_top(), LV_SYMBOL_BATTERY_FULL " 74%"));
 assert(findLabel(lv_layer_top(), "68%"));
 capture("print-room.ppm");
 printRoom.status = "";
 testMillis += 1000;
 frames();
 assert(findLabel(lv_layer_top(), "Offline / unavailable"));
 assert(findLabel(lv_layer_top(), "41%"));
 closePanel();
 assert(!printRoomOpen.load());
 swipe(true);
 auto *postal = findLabel(lv_scr_act(), "Postal code");
 assert(postal);
 lv_event_send(lv_obj_get_parent(postal), LV_EVENT_CLICKED, nullptr);
 capture("postal-code.ppm");
 auto *editor = findButton(lv_layer_top(), "Save");
 assert(editor);
 lv_event_send(editor, LV_EVENT_CLICKED, nullptr);
 frames();
 assert(settings.postalCode == "R0J");
 // No three-page navigation buttons exist in the UI; screen title is a label only.
 std::cout<<"PASS: rendered weather, today, week, settings; swipe navigation, slider guard, scroll preservation, modal guard, time-zone selection, condition icon changes and stale fallback\n";
}
