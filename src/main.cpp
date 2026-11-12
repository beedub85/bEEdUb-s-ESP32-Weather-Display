#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "app.hpp"
#include "screen_idle.hpp"
using namespace esp_panel::board;
using namespace esp_panel::drivers;
static Board *board;
static lv_disp_draw_buf_t drawBuffer;
static lv_disp_drv_t displayDriver;
static lv_indev_drv_t inputDriver;
static lv_disp_t *display;
static ScreenIdle screenIdle;
static bool backlightOff = false;
static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *pixels) {
  board->getLCD()->drawBitmap(a->x1,a->y1,a->x2-a->x1+1,a->y2-a->y1+1,(const uint8_t*)pixels);
  lv_disp_flush_ready(d); // RGB drawBitmap synchronously copies to its framebuffer.
}
static void touch(lv_indev_drv_t *,lv_indev_data_t *data) {
  TouchPoint point;
  const int count = board->getTouch()->readPoints(&point, 1, 0);
  const bool accepted = screenIdle.acceptsTouch(count > 0, millis());
  if (accepted) {
    data->point.x = point.x;
    data->point.y = point.y;
    data->state = LV_INDEV_STATE_PRESSED;
    uiTouch(true, point.x, point.y);
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
    uiTouch(false, 0, 0);
  }
}

static void updateScreenPower() {
  const uint32_t now = millis();
  screenIdle.update(now);
  const bool turnOff = screenIdle.isSleeping();
  if (turnOff == backlightOff) return;

  static bool retryPending = false;
  static uint32_t lastAttempt = 0;
  if (retryPending && now - lastAttempt < 1000) return;
  lastAttempt = now;
  auto *backlight = board->getBacklight();
  const bool changed = turnOff ? backlight->off() : backlight->on();
  if (!changed) {
    Serial.println("Backlight switch failed");
    retryPending = true;
    return;
  }

  retryPending = false;
  backlightOff = turnOff;
  lv_disp_enable_invalidation(display, !turnOff);
  auto *refreshTimer = _lv_disp_get_refr_timer(display);
  if (turnOff) {
    lv_timer_pause(refreshTimer);
  } else {
    lv_obj_invalidate(lv_scr_act());
    lv_timer_resume(refreshTimer);
    lv_timer_ready(refreshTimer);
  }
}
void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(115200);
  Serial.printf("Weather Display: flash=%u MB, PSRAM=%u MB\n",ESP.getFlashChipSize()/1048576,ESP.getPsramSize()/1048576);
  assert(ESP.getPsramSize()>=4*1024*1024);
  stateMutex=xSemaphoreCreateMutex(); assert(stateMutex);
  loadSettings();
  board=new Board(); assert(board->init()); assert(board->begin());
  lv_init();
  auto *buffer=(lv_color_t*)heap_caps_malloc(800*24*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  assert(buffer);
  lv_disp_draw_buf_init(&drawBuffer,buffer,nullptr,800*24);
  lv_disp_drv_init(&displayDriver);displayDriver.hor_res=800;displayDriver.ver_res=480;
  displayDriver.flush_cb=flush;displayDriver.draw_buf=&drawBuffer;display=lv_disp_drv_register(&displayDriver);
  lv_indev_drv_init(&inputDriver);inputDriver.type=LV_INDEV_TYPE_POINTER;inputDriver.read_cb=touch;lv_indev_drv_register(&inputDriver);
  uiBegin();startNetwork();
  screenIdle.begin(millis());
  Serial.printf("Weather Display 0.1.0 | Flash %u MB | PSRAM %u MB\n",ESP.getFlashChipSize()/1048576,ESP.getPsramSize()/1048576);
}
void loop() {
  if (!backlightOff) uiTick();
  lv_timer_handler();
  updateScreenPower();
  uiCalendarUsbExpire();
  for (int remaining = 128; remaining > 0 && Serial.available(); --remaining) {
    const auto result = uiCalendarUsbReceive(Serial.read());
    if (result == CalendarUsbResult::Saved) Serial.println("CALENDAR SAVED");
    if (result == CalendarUsbResult::Invalid) Serial.println("CALENDAR INVALID");
  }
  delay(5);
}
