#include "HAL.h"
#include "App.h"
#include "xtrack_board.hpp"
#include "navigation.hpp"
#include "driver/gpio.h"
#include "nvs.h"
#include <cstdio>
#include <cstring>
namespace xtrack {
static lv_obj_t *settings=nullptr,*health=nullptr,*detail=nullptr;
static uint32_t key_changed=0,key_pressed=0,last_health=0,last_save=0;
static bool key_raw=false,key_stable=false,key_long=false,sound=true;
static void save_preferences() {
    nvs_handle_t h;if(nvs_open("xtrack_ui",NVS_READWRITE,&h)!=ESP_OK)return;
    nvs_set_u16(h,"brightness",HAL::Backlight_GetValue());nvs_set_u8(h,"sound",sound);
    nvs_commit(h);nvs_close(h);save_settings();
}
static void label(lv_obj_t* parent,const char* text,int x,int y) {
    auto* l=lv_label_create(parent);lv_label_set_text(l,text);lv_obj_set_pos(l,x,y);
}
static void button(lv_obj_t* parent,const char* text,int x,int y,lv_event_cb_t cb) {
    auto* b=lv_btn_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,80,28);
    auto* l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
}
static void close_settings(lv_event_t*){lv_obj_add_flag(settings,LV_OBJ_FLAG_HIDDEN);save_preferences();}
static void show_settings() {
    auto f=gps_fix();
    lv_label_set_text_fmt(detail,"GPS %s  satellites %d\nSD %s  IMU %s\nBattery gauge %s",f.valid?"FIX":"WAIT",f.satellites,storage_ready()?"OK":"MISSING",imu_available()?"OK":"MISSING",battery_available()?"OK":"NOT FITTED");
    lv_obj_clear_flag(settings,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(settings);
}
void controls_init() {
    gpio_config_t cfg{};cfg.pin_bit_mask=1ULL<<boot_key;cfg.mode=GPIO_MODE_INPUT;cfg.pull_up_en=GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&cfg));
    nvs_handle_t h;
    if(nvs_open("xtrack_ui",NVS_READONLY,&h)==ESP_OK){uint16_t b=800;uint8_t s=1;nvs_get_u16(h,"brightness",&b);nvs_get_u8(h,"sound",&s);nvs_close(h);HAL::Backlight_SetValue(b);sound=s!=0;}
    HAL::Buzz_SetEnable(sound);
    auto* gear=lv_btn_create(lv_layer_top());lv_obj_set_size(gear,26,30);lv_obj_align(gear,LV_ALIGN_BOTTOM_RIGHT,-3,-5);
    auto* icon=lv_label_create(gear);lv_label_set_text(icon,LV_SYMBOL_SETTINGS);lv_obj_center(icon);
    lv_obj_add_event_cb(gear,[](lv_event_t*){show_settings();},LV_EVENT_CLICKED,nullptr);
    health=lv_label_create(lv_layer_top());lv_obj_set_pos(health,4,27);lv_obj_set_style_text_color(health,lv_color_hex(0xffbe55),0);
    lv_obj_set_style_bg_color(health,lv_color_black(),0);lv_obj_set_style_bg_opa(health,LV_OPA_80,0);
    lv_obj_set_width(health,312);lv_label_set_long_mode(health,LV_LABEL_LONG_CLIP);lv_obj_add_flag(health,LV_OBJ_FLAG_HIDDEN);
    settings=lv_obj_create(lv_layer_top());lv_obj_set_size(settings,306,218);lv_obj_center(settings);
    lv_obj_set_style_pad_all(settings,8,0);lv_obj_clear_flag(settings,LV_OBJ_FLAG_SCROLLABLE);
    label(settings,"X-Track settings",0,0);
    detail=lv_label_create(settings);lv_obj_set_pos(detail,0,23);lv_obj_set_style_text_font(detail,&lv_font_montserrat_14,0);
    label(settings,"Brightness",0,84);
    auto* slider=lv_slider_create(settings);lv_obj_set_pos(slider,90,90);lv_obj_set_size(slider,180,10);
    lv_slider_set_range(slider,50,1000);lv_slider_set_value(slider,HAL::Backlight_GetValue(),LV_ANIM_OFF);
    lv_obj_add_event_cb(slider,[](lv_event_t* e){HAL::Backlight_SetValue(lv_slider_get_value(lv_event_get_target(e)));},LV_EVENT_VALUE_CHANGED,nullptr);
    label(settings,"Sound",0,113);auto* sw=lv_switch_create(settings);lv_obj_set_pos(sw,90,111);
    if(sound)lv_obj_add_state(sw,LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw,[](lv_event_t* e){sound=lv_obj_has_state(lv_event_get_target(e),LV_STATE_CHECKED);HAL::Buzz_SetEnable(sound);},LV_EVENT_VALUE_CHANGED,nullptr);
    button(settings,"Back",0,156,[](lv_event_t* e){App_Back();close_settings(e);});
    button(settings,"Sleep",99,156,[](lv_event_t* e){close_settings(e);HAL::Power_Shutdown();});
    button(settings,"Close",198,156,close_settings);
    lv_obj_add_flag(settings,LV_OBJ_FLAG_HIDDEN);
}
void controls_update() {
    if(!settings)return;
    bool pressed=gpio_get_level(gpio_num_t(boot_key))==0;
    if(pressed!=key_raw){key_raw=pressed;key_changed=lv_tick_get();}
    if(lv_tick_elaps(key_changed)>=30 && pressed!=key_stable) {
        key_stable=pressed;HAL::Power_HandleTimeUpdate();
        if(pressed){key_pressed=lv_tick_get();key_long=false;}
        else if(!key_long){if(!lv_obj_has_flag(settings,LV_OBJ_FLAG_HIDDEN))close_settings(nullptr);else if(!App_Back())show_settings();}
    }
    if(key_stable && !key_long && lv_tick_elaps(key_pressed)>=5000){key_long=true;HAL::Power_Shutdown();}
    if(lv_tick_elaps(last_save)>=60000){last_save=lv_tick_get();save_settings();}
    if(lv_tick_elaps(last_health)<500)return;
    last_health=lv_tick_get();
    const char* error=recorder_error();auto f=gps_fix();
    if(*error)lv_label_set_text_fmt(health,"REC ERROR: %.60s",error);
    else if(!storage_ready())lv_label_set_text(health,"Insert FAT32 SD for maps and recording");
    else if(!f.valid)lv_label_set_text(health,"NO GPS FIX - map uses saved position");
    else {lv_obj_add_flag(health,LV_OBJ_FLAG_HIDDEN);return;}
    lv_obj_clear_flag(health,LV_OBJ_FLAG_HIDDEN);
}
}
