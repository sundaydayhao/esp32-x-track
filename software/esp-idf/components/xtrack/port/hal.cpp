#include "HAL.h"
#include "xtrack_board.hpp"
#include "navigation.hpp"
#include "Config/Config.h"
#include "Common/DataProc/DataProc.h"
#include "Utils/TonePlayer/TonePlayer.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <ctime>
#include <sys/time.h>
#include <cstring>
#include <algorithm>

namespace xtrack {void bus_init();}
static TonePlayer player;
#include "Common/Music/MusicCode.h"
static bool sound_enabled=true,shutdown_requested=false,auto_sleep=false;
static uint32_t active_tick=0,buzz_end=0;
static uint16_t timeout_seconds=300;
static HAL::Power_CallbackFunction_t shutdown_callback=nullptr;
namespace HAL {
void HAL_Init() {
    ESP_LOGI("xtrack","Native ESP-IDF %s / LCKFB SZP ESP32-S3",esp_get_idf_version());
    ESP_ERROR_CHECK(nvs_flash_init());
    xtrack::bus_init();Clock_Init();Power_Init();GPS_Init();xtrack::sensors_init();Audio_Init();
    Memory_DumpInfo();
}
void HAL_Update() {
    Power_Update();Power_EventMonitor();xtrack::controls_update();Audio_Update();SD_Update();xtrack::sensors_update();
    if(buzz_end && int32_t(lv_tick_get()-buzz_end)>=0){buzz_end=0;xtrack::audio_frequency(0);}
}
void Clock_Init(){setenv("TZ","UTC0",1);tzset();}
void Clock_GetInfo(Clock_Info_t* info) {
    timeval tv{};gettimeofday(&tv,nullptr);
    DataProc::SysConfig_Info_t config{};
    int tz=CONFIG_SYSTEM_TIME_ZONE_DEFAULT;
    auto* account=DataProc::Center()->SearchAccount("SysConfig");
    if(account && DataProc::Center()->AccountMain.Pull("SysConfig",&config,sizeof(config))==Account::RES_OK)tz=config.timeZone;
    time_t local=tv.tv_sec+tz*3600;
    tm value{};gmtime_r(&local,&value);*info={};
    info->year=value.tm_year+1900;info->month=value.tm_mon+1;info->day=value.tm_mday;
    info->hour=value.tm_hour;info->minute=value.tm_min;info->second=value.tm_sec;info->week=value.tm_wday;info->millisecond=tv.tv_usec/1000;
}
void Clock_SetInfo(const Clock_Info_t* info) {
    if(!xtrack::valid_date(info->year,info->month,info->day))return;
    // Input is UTC. Offset belongs only to display, never to stored system time/GPX.
    tm value{};value.tm_year=info->year-1900;value.tm_mon=info->month-1;value.tm_mday=info->day;
    value.tm_hour=info->hour;value.tm_min=info->minute;value.tm_sec=info->second;
    timeval tv{mktime(&value),0};settimeofday(&tv,nullptr);
}
const char* Clock_GetWeekString(uint8_t d){static const char* s[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};return s[d%7];}
void Power_Init(){Power_HandleTimeUpdate();}
void Power_HandleTimeUpdate(){active_tick=lv_tick_get();if(Backlight_GetValue()==0)Backlight_SetValue(800);}
void Power_SetAutoLowPowerTimeout(uint16_t sec){timeout_seconds=sec;}
uint16_t Power_GetAutoLowPowerTimeout(){return timeout_seconds;}
void Power_SetAutoLowPowerEnable(bool enable){auto_sleep=enable;active_tick=lv_tick_get();}
void Power_Shutdown(){shutdown_requested=true;}
void Power_SetEventCallback(Power_CallbackFunction_t cb){shutdown_callback=cb;}
void Power_Update(){if(auto_sleep && timeout_seconds && lv_tick_elaps(active_tick)>timeout_seconds*1000u && !xtrack::recorder_active())Power_Shutdown();}
void Power_EventMonitor() {
    if(!shutdown_requested)return;
    // A held BOOT pin would immediately wake ext0; wait for release without blocking GUI.
    if(gpio_get_level(gpio_num_t(xtrack::boot_key))==0)return;
    shutdown_requested=false;
    if(shutdown_callback)shutdown_callback();
    if(*xtrack::recorder_error()) {ESP_LOGE("power","Sleep cancelled: track save failed");return;}
    Backlight_SetValue(0);xtrack::audio_frequency(0);xtrack::expander_set(2,false);
    esp_sleep_enable_ext0_wakeup(gpio_num_t(xtrack::boot_key),0);
    esp_deep_sleep_start();
}
void Audio_Init(){xtrack::audio_start();player.SetCallback([](uint32_t f,uint16_t){Buzz_Tone(f);});}
void Audio_Update(){player.Update(lv_tick_get());}
bool Audio_PlayMusic(const char* name) {
    for(const auto& m:MusicList)if(!std::strcmp(m.name,name)){player.Play(m.mc,m.length);return true;}
    return false;
}
void Buzz_SetEnable(bool enable){sound_enabled=enable;if(!enable)xtrack::audio_frequency(0);}
void Buzz_Tone(uint32_t frequency,int32_t duration){xtrack::audio_frequency(sound_enabled?frequency:0);buzz_end=duration>0?lv_tick_get()+duration:0;}
void Buzz_init(){}
void Memory_DumpInfo(){ESP_LOGI("memory","free internal=%u PSRAM=%u",unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));}
void FaultHandle_Init(){}
void Encoder_Init(){}
void Encoder_Update(){}
int32_t Encoder_GetDiff(){return 0;}
bool Encoder_GetIsPush(){return gpio_get_level(gpio_num_t(xtrack::boot_key))==0;}
void Encoder_SetEnable(bool){}
int I2C_Scan(){int found=0;for(uint8_t a=8;a<120;++a)if(xtrack::i2c_probe(a)){++found;ESP_LOGI("i2c","0x%02x",a);}return found;}
}
