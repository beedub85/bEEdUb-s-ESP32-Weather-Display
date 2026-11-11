#pragma once
#define WL_CONNECTED 3
struct FakeWiFi {int status(){return WL_CONNECTED;}};
extern FakeWiFi WiFi;
