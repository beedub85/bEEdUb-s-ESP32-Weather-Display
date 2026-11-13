#include "app.hpp"
#include <WiFi.h>
#include "weather_icons.hpp"
static constexpr uint32_t BG=0x081A28,CARD=0x142E40,TEXT=0xF3F7FA,MUTED=0xA9BDCA,TEAL=0x22D1BD,BLUE=0x1A73E8,INK=0x202124;
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
static void closeModal(){if(calendarUsbStatus){calendarUsbStatus=nullptr;clearCalendarUsbLine();}if(modal){lv_obj_del(modal);modal=nullptr;lv_obj_invalidate(lv_scr_act());}keyboard=nullptr;}
static void dismiss(lv_event_t*){closeModal();}
static lv_obj_t* newModal(const String &title){
  closeModal();modal=box(lv_layer_top(),0,0,800,480,0x07131F,0);
  label(modal,title,22,18,580,&lv_font_montserrat_24);
  button(modal,"Close",684,10,96,44,dismiss,nullptr,CARD);return modal;
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
  statusLabel=label(root,"",20,424,760,&lv_font_montserrat_16,MUTED);
  label(root,"Swipe right for Calendar     •     Swipe left for Settings",20,454,760,&lv_font_montserrat_12,MUTED);
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
  if(editKind==2 || editKind==3){shared.events.clear();shared.calendarLoaded=false;shared.calendarError="Loading calendar...";}
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
  xSemaphoreTake(stateMutex,portMAX_DELAY);settings.timezone=tzCodes[i];shared.calendarLoaded=false;shared.events.clear();shared.calendarError="Loading calendar...";xSemaphoreGive(stateMutex);
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
  const char*titles[]={"Wi-Fi setup","Weather station address","Google Calendar setup","Time zone (POSIX)"};
  auto*m=newModal(titles[editKind]);button(m,"Save",570,10,96,44,editorSave);
  if(editKind==2)button(m,"Set up over USB",396,10,158,44,startCalendarUsb,nullptr,CARD);
  form=box(m,24,72,752,164,CARD);lv_obj_add_flag(form,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(form,LV_DIR_VER);
  keyboard=lv_keyboard_create(m);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(keyboard,0,252);lv_obj_set_size(keyboard,800,228);
  if(editKind==0){fields[0]=input(form,"Network name (2.4 GHz)",draft.ssid,8,false,32);fields[1]=input(form,"Wi-Fi password",draft.password,96,true,63);}
  if(editKind==1)fields[0]=input(form,"Base URL, for example http://192.168.2.149",draft.station,8,false,200);
  if(editKind==2){fields[0]=input(form,"Apps Script web app URL ending in /exec",draft.calendarUrl,8,false,400);fields[1]=input(form,"Bridge token (from your own Apps Script setup)",draft.calendarToken,96,true,128);}
  if(editKind==3)fields[0]=input(form,"Manitoba: MBT5  |  Eastern: EST5EDT,M3.2.0,M11.1.0",draft.timezone,8,false,100);
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

static void settingsScreen(){
  const char*names[]={"Wi-Fi","Weather station","Google Calendar","Display dimming","Imperial units","Time zone","Screen timeout"};
  String values[]={settings.ssid.isEmpty()?"Tap to connect":settings.ssid,settings.station,settings.calendarUrl.isEmpty()?"Tap to link":"Configured", "", "°F / mph / in", timezoneName(), screenTimeoutLabel(settings.screenTimeoutSeconds)};
  int kinds[]={0,1,2,-1,-1,3,-1};
  for(int i=0;i<7;++i){auto*b=box(root,20,70+i*53,760,48,CARD);
    label(b,names[i],16,14,220,&lv_font_montserrat_18);
    if(i==3){auto*s=lv_slider_create(b);lv_obj_set_pos(s,300,20);lv_obj_set_size(s,416,10);lv_slider_set_range(s,20,100);lv_slider_set_value(s,settings.brightness,LV_ANIM_OFF);lv_obj_add_event_cb(s,brightnessChanged,LV_EVENT_VALUE_CHANGED,nullptr);lv_obj_add_event_cb(s,brightnessChanged,LV_EVENT_RELEASED,nullptr);}
    else if(i==4){label(b,values[i],300,15,320,&lv_font_montserrat_14,MUTED);auto*s=lv_switch_create(b);lv_obj_set_pos(s,680,10);lv_obj_set_size(s,60,30);if(settings.imperial)lv_obj_add_state(s,LV_STATE_CHECKED);lv_obj_add_event_cb(s,unitsChanged,LV_EVENT_VALUE_CHANGED,nullptr);}
    else{auto*l=label(b,values[i]+"  >",250,16,490,&lv_font_montserrat_14,TEAL);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(b,i==6?openScreenTimeout:edit,LV_EVENT_CLICKED,(void*)(intptr_t)kinds[i]);}
  }
  label(root,"Swipe right to return to Weather • Dimming changes image intensity",24,450,750,&lv_font_montserrat_12,MUTED);
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
  clockLabel=label(root,"--:--",626,20,156,&lv_font_montserrat_20,page==0?INK:TEXT);
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
    bool editing=modal || (page==2 && sy>=229 && sy<277);
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
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (page == 1) {
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
  time_t clock=time(nullptr);String clockText=clock>1700000000?dateText(clock,"%H:%M"):"--:--";
  clockText+=WiFi.status()==WL_CONNECTED?String("  ")+LV_SYMBOL_WIFI:"  !";updateLabel(clockLabel,clockText.c_str());
  if(page==1)weatherUpdate();
  if(snapshot.calendarError!=lastCalendarError){calendarDirty=true;lastCalendarError=snapshot.calendarError;}
  if(updateCalendar && (calendarDirty || lastCalendar!=snapshot.calendarReceived || lastMinute!=clock/60 || shownWeek!=core::sunday(clock,requestedWeek))){
    calendarUpdate();lastCalendar=snapshot.calendarReceived;lastMinute=clock/60;
  }
}
