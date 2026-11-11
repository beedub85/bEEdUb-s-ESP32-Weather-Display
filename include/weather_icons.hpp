#pragma once
#include <lvgl.h>
#include <cmath>
#include "core.hpp"
namespace icons {
enum class Kind {Unknown,Sunny,PartlyCloudy,Cloudy,Rain,LowLight,Humidity,Wind,Gauge,Light,Air,DryRain};
struct State {Kind kind=Kind::Unknown;uint32_t accent=0x22D1BD;};
inline Kind condition(bool fresh,bool raining,float lux){
  if(!fresh)return Kind::Unknown;
  if(raining)return Kind::Rain;
  if(!std::isfinite(lux))return Kind::Unknown;
  if(lux>=20000)return Kind::Sunny;
  if(lux>=5000)return Kind::PartlyCloudy;
  if(lux>=500)return Kind::Cloudy;
  return Kind::LowLight;
}
inline const char* caption(Kind k){switch(k){
 case Kind::Sunny:return "Sunny (est.)";case Kind::PartlyCloudy:return "Partly cloudy (est.)";
 case Kind::Cloudy:return "Cloudy (est.)";case Kind::Rain:return "Rain detected";
 case Kind::LowLight:return "Low light";default:return "Sky unavailable";
}}
// The icons are native LVGL vector drawings: no bitmap downloads or font/emoji dependencies.
class Painter {
 lv_draw_ctx_t* ctx;lv_area_t a;float scale;
 lv_point_t pt(float x,float y){return {(lv_coord_t)std::lround(a.x1+x*scale),(lv_coord_t)std::lround(a.y1+y*scale)};}
public:
 Painter(lv_obj_t*o,lv_draw_ctx_t*c):ctx(c){lv_obj_get_coords(o,&a);scale=lv_obj_get_width(o)/64.f;}
 void line(float x,float y,float x2,float y2,uint32_t color,float width=3){
  lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=lv_color_hex(color);d.width=std::max(1,(int)std::lround(width*scale));d.round_start=d.round_end=1;
  auto p=pt(x,y),q=pt(x2,y2);lv_draw_line(ctx,&d,&p,&q);
 }
 void arc(float x,float y,float r,int from,int to,uint32_t color,float width=3){
  lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.color=lv_color_hex(color);d.width=std::max(1,(int)std::lround(width*scale));d.rounded=1;
  auto p=pt(x,y);lv_draw_arc(ctx,&d,&p,(lv_coord_t)std::lround(r*scale),from,to);
 }
 void circle(float x,float y,float r,uint32_t color){arc(x,y,r,0,360,color);}
 void dot(float x,float y,float r,uint32_t color){
  auto p=pt(x-r,y-r),q=pt(x+r,y+r);lv_area_t v={p.x,p.y,q.x,q.y};lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=LV_RADIUS_CIRCLE;lv_draw_rect(ctx,&d,&v);
 }
 void sun(float x,float y,float r){
  circle(x,y,r,0xFFC85A);
  for(int i=0;i<8;++i){float t=i*3.14159265f/4;line(x+std::cos(t)*(r+5),y+std::sin(t)*(r+5),x+std::cos(t)*(r+10),y+std::sin(t)*(r+10),0xFFC85A,2.5);}
 }
 void cloud(uint32_t c){
  arc(27,30,14,180,320,c);line(37.7,21,38.5,25.5,c);
  arc(44,35,11,240,450,c);line(44,46,16,46,c);
  arc(16,37,9,90,270,c);line(16,28,13,30,c);
 }
 void drop(float x,float y,float r,uint32_t c){
  line(x,y-r*1.7f,x-r*.85f,y,c);arc(x,y,r,0,180,c);line(x+r*.85f,y,x,y-r*1.7f,c);
 }
 void draw(const State&s){
  const uint32_t c=s.accent,cloudColor=0xD7E6F2;
  switch(s.kind){
   case Kind::Sunny:sun(32,32,13);break;
   case Kind::PartlyCloudy:sun(42,20,9);cloud(cloudColor);break;
   case Kind::Cloudy:cloud(cloudColor);line(17,54,46,54,0x7897AA,2);break;
   case Kind::Rain:cloud(cloudColor);for(int x=20;x<=44;x+=12)line(x,52,x-4,60,0x55BCFF,3);break;
   case Kind::DryRain:cloud(0x7897AA);drop(33,55,4,0x7897AA);break;
   case Kind::LowLight:arc(32,30,19,45,315,0xA7C5FF);arc(43,21,17,80,205,0xA7C5FF);dot(49,12,2,0xFFC85A);break;
   case Kind::Humidity:drop(32,36,15,c);line(23,38,29,32,c,2);line(35,44,41,38,c,2);break;
   case Kind::Wind:line(7,23,42,23,c);arc(43,16,8,185,450,c);line(5,34,50,34,c);arc(50,41,8,270,540,c);line(12,46,31,46,c);break;
   case Kind::Gauge:arc(32,35,23,150,390,c);line(32,35,44,21,c);dot(32,35,3,c);line(13,25,17,28,c,2);line(29,12,29,17,c,2);line(48,27,52,24,c,2);line(20,54,44,54,c);break;
   case Kind::Light:sun(32,32,12);break;
   case Kind::Air:line(8,20,40,20,c);arc(42,15,6,180,450,c);line(6,32,50,32,c);arc(50,39,8,270,540,c);line(12,46,33,46,c);dot(50,13,3,c);dot(16,57,2,c);break;
   default:circle(32,32,23,0x7897AA);arc(32,25,8,180,420,0xA9BDCA);line(36,32,32,39,0xA9BDCA);dot(32,47,1.5,0xA9BDCA);break;
  }
 }
};
inline void drawEvent(lv_event_t*e){
 auto*o=lv_event_get_target(e);auto*state=static_cast<State*>(lv_event_get_user_data(e));Painter(o,lv_event_get_draw_ctx(e)).draw(*state);
}
inline lv_obj_t* create(lv_obj_t*parent,int x,int y,int size,State*state){
 auto*o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,size,size);
 lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_event_cb(o,drawEvent,LV_EVENT_DRAW_MAIN,state);return o;
}
}
