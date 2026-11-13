#include "core.hpp"
#include "screen_rotation.hpp"
#include "screen_idle.hpp"
#include "local_weather.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace core;
static time_t at(int year,int month,int day,int hour=0,int min=0){tm t={};t.tm_year=year-1900;t.tm_mon=month-1;t.tm_mday=day;t.tm_hour=hour;t.tm_min=min;t.tm_isdst=-1;return mktime(&t);}
static Event timed(time_t start,time_t end){Event e;e.start=start;e.end=end;return e;}
int main(){
 static_assert(screenRotation::flipX(0) == 799 && screenRotation::flipY(0) == 479);
 static_assert(screenRotation::flipX(799) == 0 && screenRotation::flipY(479) == 0);
 const int tile[] = {1, 2, 3, 4, 5, 6};
 int flipped[6]{};
 screenRotation::copyTile(tile, flipped, 6);
 for (int i = 0; i < 6; ++i) assert(flipped[i] == 6 - i && tile[i] == i + 1);

 setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
 tzset();
 assert(localWeather::utcTimestamp("2024-02-29T00:00:00Z") == 1709164800);
 assert(localWeather::utcTimestamp("20240229000000") == 1709164800);
 assert(localWeather::utcTimestamp("2023-02-29T00:00:00Z") == 0);
 assert(localWeather::utcTimestamp("2024-02-29T00:00:00X") == 0);
 assert(localWeather::utcTimestamp("1970-01-02T00:00:01Z") == 86401);
 char prefix[4];
 assert(localWeather::postalPrefix("r0j 1e0", prefix) && std::string_view(prefix) == "R0J");
 assert(localWeather::postalPrefix("B3H", prefix) && std::string_view(prefix) == "B3H");
 assert(localWeather::postalPrefix(" ", prefix) && !*prefix);
 assert(!localWeather::postalPrefix("R0J1", prefix));
 assert(!localWeather::postalPrefix("D0J", prefix));
 localWeather::History history;
 for (unsigned i = 1; i <= localWeather::sampleCount + 5; ++i) history.add({time_t(i) * 300});
 assert(history.count == localWeather::sampleCount && history.at(0).at == 1800);
 const auto newest = history.at(history.count - 1);
 history.add(newest);
 assert(history.count == localWeather::sampleCount && history.at(0).at == 1800);
 history.add({300});
 assert(history.count == 1 && history.at(0).at == 300);
 ScreenIdle screen;
 static_assert(ScreenIdle::defaultTimeoutSeconds == 60);
 static_assert(ScreenIdle::normalizeTimeout(120) == 120);
 static_assert(ScreenIdle::normalizeTimeout(0) == 60);
 constexpr uint32_t seconds = ScreenIdle::defaultTimeoutSeconds;
 constexpr uint32_t timeoutMs = seconds * 1000;
 const uint32_t start = UINT32_MAX - 1000;
 screen.begin(start);
 screen.update(start + timeoutMs - 1, seconds);
 assert(!screen.isSleeping());
 screen.update(start + timeoutMs, seconds);
 assert(screen.isSleeping());
 const uint32_t wake = start + timeoutMs + 1;
 assert(!screen.acceptsTouch(true, wake));
 assert(!screen.isSleeping());
 assert(!screen.acceptsTouch(true, wake + 20));
 assert(!screen.acceptsTouch(false, wake + 30));
 assert(screen.acceptsTouch(true, wake + 40));
 screen.update(wake + 40 + timeoutMs, seconds);
 assert(!screen.isSleeping()); // A held touch keeps the screen on.
 screen.acceptsTouch(false, wake + 40);
 screen.update(wake + 40 + 29'999, 30);
 assert(!screen.isSleeping());
 screen.update(wake + 40 + 30'000, 30);
 assert(screen.isSleeping());
 assert(std::string_view(normalizeTimeZone("CST6CDT,M3.2.0,M11.1.0")) == "MBT5");
 assert(std::string_view(normalizeTimeZone("CST6")) == "CST6");
 setenv("TZ", winnipegTimeZone, 1);
 tzset();
 assert(at(2026,11,12,18,28) == 1794526080);
 assert(addDays(at(2026,11,1),1) - at(2026,11,1) == 86400);
 assert(addDays(at(2027,3,14),1) - at(2027,3,14) == 86400);
 setenv("TZ","CST6CDT,M3.2.0,M11.1.0",1);tzset();
 assert(sunday(at(2026,11,10,12))==at(2026,11,8));
 assert(sunday(at(2026,11,14,23,59))==at(2026,11,8));
 assert(sunday(at(2026,11,15))==at(2026,11,15));
 assert(sunday(at(2026,11,16),-1)==at(2026,11,8));
 assert(sunday(at(2026,3,8),1)==at(2026,3,15));
 assert(addDays(at(2026,3,8),1)-at(2026,3,8)==23*3600); // spring DST day
 assert(addDays(at(2026,11,1),1)-at(2026,11,1)==25*3600); // fall DST day
 assert(dateEpoch("2026-11-10")==at(2026,11,10));
 Event all;all.allDay=true;all.startDate="2026-11-10";all.endDate="2026-11-12";
 assert(intersects(all,at(2026,11,10),at(2026,11,11)));
 assert(intersects(all,at(2026,11,11),at(2026,11,12)));
 assert(!intersects(all,at(2026,11,12),at(2026,11,13))); // exclusive all-day end
 std::vector<Event> events={timed(at(2026,11,10,9),at(2026,11,10,10)),timed(at(2026,11,10,9,30),at(2026,11,10,11)),timed(at(2026,11,10,10,30),at(2026,11,10,12)),timed(at(2026,11,10,12),at(2026,11,10,13)),all};
 auto s=daySegments(events,at(2026,11,10));assert(s.size()==4);
 assert(s[0].columns==2 && s[1].columns==2 && s[2].columns==2);
 assert(s[0].column!=s[1].column && s[1].column!=s[2].column);
 assert(s[3].columns==1); // adjacent events do not overlap
 events={timed(at(2026,11,9,23),at(2026,11,11,1))};
 s=daySegments(events,at(2026,11,10));assert(s.size()==1 && s[0].from==0 && s[0].to==1440);
 s=daySegments(events,at(2026,11,11));assert(s.size()==1 && s[0].from==0 && s[0].to==60);
 assert(std::abs(temp(0,true)-32)<0.001);assert(std::abs(rain(25.4,true)-1)<0.001);
 assert(std::abs(wind(100,true)-62.1371)<0.001);assert(std::abs(pressure(1013.25,true)-29.9213)<0.001);
 assert(std::string(compass(0))=="N" && std::string(compass(359))=="N" && std::string(compass(90))=="E");
 assert(std::string(compass(NAN))=="--");
 std::cout<<"PASS: dates, DST, all-day ends, overlap layout, midnight splits, units, direction\n";
}
