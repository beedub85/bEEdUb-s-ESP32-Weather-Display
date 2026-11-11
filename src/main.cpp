#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "app.hpp"
using namespace esp_panel::board;
using namespace esp_panel::drivers;
static Board *board;
static lv_disp_draw_buf_t drawBuffer;
static lv_disp_drv_t displayDriver;
static lv_indev_drv_t inputDriver;
static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *pixels) {
  board->getLCD()->drawBitmap(a->x1,a->y1,a->x2-a->x1+1,a->y2-a->y1+1,(const uint8_t*)pixels);
  lv_disp_flush_ready(d); // RGB drawBitmap synchronously copies to its framebuffer.
}
static void touch(lv_indev_drv_t *,lv_indev_data_t *data) {
  TouchPoint p; int count=board->getTouch()->readPoints(&p,1,0);
  if(count>0) {data->point.x=p.x;data->point.y=p.y;data->state=LV_INDEV_STATE_PRESSED;uiTouch(true,p.x,p.y);}
  else {data->state=LV_INDEV_STATE_RELEASED;uiTouch(false,0,0);}
}
void setup() {
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
  displayDriver.flush_cb=flush;displayDriver.draw_buf=&drawBuffer;lv_disp_drv_register(&displayDriver);
  lv_indev_drv_init(&inputDriver);inputDriver.type=LV_INDEV_TYPE_POINTER;inputDriver.read_cb=touch;lv_indev_drv_register(&inputDriver);
  uiBegin();startNetwork();
  Serial.printf("Weather Display 0.1.0 | Flash %u MB | PSRAM %u MB\n",ESP.getFlashChipSize()/1048576,ESP.getPsramSize()/1048576);
}
void loop() {uiTick();lv_timer_handler();delay(5);}
