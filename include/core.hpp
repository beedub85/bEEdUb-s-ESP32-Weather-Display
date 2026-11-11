#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#include <cstdlib>
#endif
namespace core {
#if defined(ARDUINO_ARCH_ESP32)
// Calendar content is kept in external RAM; snapshots must not exhaust Wi-Fi/TLS SRAM.
template<class T> struct ExternalAllocator {
  using value_type=T;
  ExternalAllocator()=default;
  template<class U> ExternalAllocator(const ExternalAllocator<U>&) {}
  T *allocate(size_t n) {void *p=heap_caps_malloc(n*sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!p)abort();return static_cast<T*>(p);}
  void deallocate(T *p,size_t) {heap_caps_free(p);}
  template<class U> bool operator==(const ExternalAllocator<U>&)const{return true;}
  template<class U> bool operator!=(const ExternalAllocator<U>&)const{return false;}
};
using EventText=std::basic_string<char,std::char_traits<char>,ExternalAllocator<char>>;
#else
using EventText=std::string;
#endif
struct Event {
  EventText title, location, description, calendar, startDate, endDate;
  time_t start = 0, end = 0;
  bool allDay = false;
  unsigned color = 0x4285F4;
};
#if defined(ARDUINO_ARCH_ESP32)
using Events=std::vector<Event,ExternalAllocator<Event>>;
#else
using Events=std::vector<Event>;
#endif
inline time_t midnight(time_t t) {
  tm d = *localtime(&t); d.tm_hour = d.tm_min = d.tm_sec = 0; d.tm_isdst = -1;
  return mktime(&d);
}
inline time_t addDays(time_t t, int days) {
  tm d = *localtime(&t); d.tm_mday += days; d.tm_isdst = -1; return mktime(&d);
}
inline time_t monday(time_t now, int offset = 0) {
  tm d = *localtime(&now); return addDays(midnight(now), -((d.tm_wday + 6) % 7) + offset * 7);
}
inline time_t dateEpoch(const EventText &s) {
  int y=0,m=0,d=0;
  if (sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m<1 || m>12 || d<1 || d>31) return 0;
  tm v = {}; v.tm_year=y-1900; v.tm_mon=m-1; v.tm_mday=d; v.tm_isdst=-1; return mktime(&v);
}
inline time_t startOf(const Event &e) { return e.allDay ? dateEpoch(e.startDate) : e.start; }
inline time_t endOf(const Event &e) { return e.allDay ? dateEpoch(e.endDate) : e.end; }
inline bool intersects(const Event &e, time_t a, time_t b) { return startOf(e)<b && endOf(e)>a; }
inline float wallMinute(time_t t) { const tm d=*localtime(&t); return d.tm_hour*60+d.tm_min+d.tm_sec/60.f; }
struct Segment { size_t event; float from,to; int column=0, columns=1; };
inline std::vector<Segment> daySegments(const Events &events, time_t day) {
  std::vector<Segment> out;
  const time_t end=addDays(day,1);
  for(size_t i=0;i<events.size();++i) {
    const auto &e=events[i];
    if(e.allDay || !intersects(e,day,end)) continue;
    out.push_back({i,e.start<=day?0.f:wallMinute(e.start), e.end>=end?1440.f:wallMinute(e.end),0,1});
  }
  std::sort(out.begin(),out.end(),[](const Segment&a,const Segment&b){return a.from==b.from?a.to>b.to:a.from<b.from;});
  size_t a=0;
  while(a<out.size()) {
    size_t b=a+1; float clusterEnd=out[a].to;
    while(b<out.size() && out[b].from<clusterEnd) {clusterEnd=std::max(clusterEnd,out[b].to); ++b;}
    std::vector<float> lanes;
    for(size_t j=a;j<b;++j) {
      int lane=0; while(lane<(int)lanes.size() && lanes[lane]>out[j].from) ++lane;
      if(lane==(int)lanes.size()) lanes.push_back(0);
      lanes[lane]=out[j].to; out[j].column=lane;
    }
    for(size_t j=a;j<b;++j) out[j].columns=lanes.size();
    a=b;
  }
  return out;
}
inline float temp(float c,bool imperial) {return imperial?c*1.8f+32:c;}
inline float wind(float kmh,bool imperial) {return imperial?kmh*0.621371f:kmh;}
inline float rain(float mm,bool imperial) {return imperial?mm/25.4f:mm;}
inline float pressure(float hpa,bool imperial) {return imperial?hpa/33.8638867f:hpa;}
inline const char* compass(float deg) {
  static const char* dirs[]={"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
  if(!std::isfinite(deg)) return "--";
  return dirs[(int)std::floor((std::fmod(deg+360,360)+11.25f)/22.5f)%16];
}
}
