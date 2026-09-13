#include "DataProc.h"
#include "HAL.h"
#include "xtrack_board.hpp"
#include "navigation.hpp"
#include "gpx_writer.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace xtrack {
static GpxWriter writer;
static uint64_t last_recorded=0;
static bool interrupted=false;
static DataProc::Recorder_Info_t recording{DataProc::RECORDER_CMD_STOP,1000};
static std::string rec_error;
bool recorder_active(){return writer.opened();}
const char* recorder_error(){return rec_error.c_str();}
void recorder_recover() {
    DIR* d=opendir("/sdcard/Track");if(!d)return;
    while(auto* item=readdir(d)) {
        std::string name=item->d_name;
        if(name.size()>9 && name.substr(name.size()-9)==".gpx.part") {
            bool ok=GpxWriter::recover("/sdcard/Track/"+name);
            ESP_LOGI("recorder","Recover %s: %s",name.c_str(),ok?"OK":"retained for inspection");
        }
    }
    closedir(d);
}
static void record_error(const char* message) {
    rec_error=message;ESP_LOGE("recorder","%s",message);
    if(!writer.opened()) {
        recording.cmd=DataProc::RECORDER_CMD_STOP;
        DataProc::TrackFilter_Info_t filter{};filter.cmd=DataProc::TRACK_FILTER_CMD_STOP;
        DataProc::Center()->AccountMain.Notify("TrackFilter",&filter,sizeof(filter));
    }
    DataProc::StatusBar_Info_t status{};status.cmd=DataProc::STATUS_BAR_CMD_SET_LABEL_REC;
    status.param.labelRec.show=true;status.param.labelRec.str="ERROR";
    DataProc::Center()->AccountMain.Notify("StatusBar",&status,sizeof(status));
}
static int record_command(Account* account,DataProc::Recorder_Info_t* command) {
    using namespace DataProc;
    bool ok=false;
    switch(command->cmd) {
    case RECORDER_CMD_START: {
        auto f=gps_fix();
        if(!f.valid || !storage_ready()){record_error("Valid GPS and mounted SD required");return Account::RES_PARAM_ERROR;}
        char name[100];
        for(unsigned i=0;i<1000;++i) {
            std::snprintf(name,sizeof(name),"/sdcard/Track/TRK_%04d%02d%02d_%02d%02d%02d_%03u.gpx",f.year,f.month,f.day,f.hour,f.minute,f.second,i);
            if(access(name,F_OK)!=0 && access((std::string(name)+".part").c_str(),F_OK)!=0)break;
        }
        ok=writer.start(name);last_recorded=0;interrupted=false;
        break;
    }
    case RECORDER_CMD_PAUSE:ok=writer.pause();break;
    case RECORDER_CMD_CONTINUE:ok=storage_ready() && writer.resume();break;
    case RECORDER_CMD_STOP:ok=writer.stop();break;
    }
    if(!ok){record_error(writer.error().empty()?"Recorder command failed":writer.error().c_str());return Account::RES_PARAM_ERROR;}
    rec_error.clear();recording=*command;
    TrackFilter_Info_t filter{};filter.cmd=static_cast<TrackFilter_Cmd_t>(command->cmd);
    account->Notify("TrackFilter",&filter,sizeof(filter));
    return Account::RES_OK;
}
static int record_event(Account* account,Account::EventParam_t* e) {
    if(e->event==Account::EVENT_NOTIFY && e->size==sizeof(DataProc::Recorder_Info_t))
        return record_command(account,static_cast<DataProc::Recorder_Info_t*>(e->data_p));
    if(e->event==Account::EVENT_SUB_PULL && e->size==sizeof(recording)) {
        std::memcpy(e->data_p,&recording,sizeof(recording));return Account::RES_OK;
    }
    if(e->event==Account::EVENT_TIMER) {
        if(recording.cmd!=DataProc::RECORDER_CMD_START && recording.cmd!=DataProc::RECORDER_CMD_CONTINUE)return Account::RES_OK;
        auto f=gps_fix();
        if(!f.valid){interrupted=true;return Account::RES_OK;}
        if(f.sequence==last_recorded)return Account::RES_OK;
        bool ok=true;
        if(interrupted){ok=writer.pause() && writer.resume();interrupted=false;}
        if(ok)ok=writer.point(f);
        if(!ok){record_error(writer.error().empty()?"GPX write failed":writer.error().c_str());storage_failed();}
        else last_recorded=f.sequence;
        return Account::RES_OK;
    }
    return Account::RES_UNSUPPORTED_REQUEST;
}
void save_settings() {
    auto& main=DataProc::Center()->AccountMain;
    DataProc::SysConfig_Info_t sys{};sys.cmd=DataProc::SYSCONFIG_CMD_SAVE;main.Notify("SysConfig",&sys,sizeof(sys));
    DataProc::Storage_Info_t storage{};storage.cmd=DataProc::STORAGE_CMD_SAVE;main.Notify("Storage",&storage,sizeof(storage));
}
}
DATA_PROC_INIT_DEF(Recorder) {
    account->Subscribe("TrackFilter");account->SetEventCallback(xtrack::record_event);account->SetTimerPeriod(250);
}

static HAL::SportStatus_Info_t sport{};
static xtrack::Trip trip;
static double accounted_m=0,accounted_s=0;
static int sport_event(Account* account,Account::EventParam_t* e) {
    if(e->event==Account::EVENT_SUB_PULL && e->size==sizeof(sport)) {
        std::memcpy(e->data_p,&sport,sizeof(sport));return Account::RES_OK;
    }
    if(e->event!=Account::EVENT_TIMER)return Account::RES_UNSUPPORTED_REQUEST;
    auto fix=xtrack::gps_fix();trip.update(fix,esp_timer_get_time()/1000);
    const double dm=trip.meters-accounted_m,ds=trip.moving_seconds-accounted_s;
    accounted_m=trip.meters;accounted_s=trip.moving_seconds;
    sport.totalDistance+=dm;sport.totalTime+=uint64_t(ds*1000);
    sport.singleDistance=trip.meters;sport.singleTime=uint64_t(trip.moving_seconds*1000);
    sport.speedKph=fix.valid?fix.speed_kmh:0;
    if(trip.max_kmh>sport.speedMaxKph)sport.speedMaxKph=trip.max_kmh;
    sport.speedAvgKph=trip.moving_seconds>0?trip.meters/trip.moving_seconds*3.6:0;
    sport.singleCalorie+=sport.speedKph*sport.weight*0.5*ds/3600;
    sport.lastTick=lv_tick_get();account->Commit(&sport,sizeof(sport));account->Publish();
    return Account::RES_OK;
}
DATA_PROC_INIT_DEF(SportStatus) {
    using namespace DataProc;
    sport.weight=66;account->Subscribe("Storage");
    STORAGE_VALUE_REG(account,sport.totalDistance,STORAGE_TYPE_FLOAT);
    STORAGE_VALUE_REG(account,sport.totalTimeUINT32[0],STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account,sport.totalTimeUINT32[1],STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account,sport.speedMaxKph,STORAGE_TYPE_FLOAT);
    STORAGE_VALUE_REG(account,sport.weight,STORAGE_TYPE_FLOAT);
    account->SetEventCallback(sport_event);account->SetTimerPeriod(250);
}

static int timezone_event(Account*,Account::EventParam_t* e) {
    if(e->event!=Account::EVENT_SUB_PULL || e->size!=sizeof(HAL::Clock_Info_t))return Account::RES_PARAM_ERROR;
    HAL::GPS_Info_t gps{};if(!HAL::GPS_GetInfo(&gps))return Account::RES_PARAM_ERROR;
    // DP_Clock calibrates the system UTC clock. Clock_GetInfo applies UI time zone.
    std::memcpy(e->data_p,&gps.clock,sizeof(gps.clock));return Account::RES_OK;
}
DATA_PROC_INIT_DEF(TzConv){account->SetEventCallback(timezone_event);}
