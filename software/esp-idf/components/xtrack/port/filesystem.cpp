#include "xtrack_board.hpp"
#include "HAL.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace xtrack {
static sdmmc_card_t* card=nullptr;
static bool ready=false;
static uint32_t retry_tick=0;
static HAL::SD_CallbackFunction_t sd_callback=nullptr;
bool storage_ready(){return ready;}
void storage_failed(){ready=false;}
std::string storage_path(const char* path) {
    if(!path || std::strstr(path,"..") || std::strchr(path,'\\')) return {};
    while(*path=='/')++path;
    // Settings survive a missing SD card; map tiles and rides always live on SD.
    const bool settings=std::strstr(path,"SystemSave")!=nullptr;
    return std::string(settings?"/config/":"/sdcard/")+path;
}
struct Directory {DIR* dir;std::string path;};
static void* fs_open(lv_fs_drv_t*,const char* path,lv_fs_mode_t mode) {
    std::string name=storage_path(path);if(name.empty())return nullptr;
    if(name.rfind("/sdcard/",0)==0 && !ready)return nullptr;
    const char* flags=(mode&LV_FS_MODE_WR)?((mode&LV_FS_MODE_RD)?"w+b":"wb"):"rb";
    return std::fopen(name.c_str(),flags);
}
static lv_fs_res_t fs_close(lv_fs_drv_t*,void* file){return file && std::fclose(static_cast<FILE*>(file))==0?LV_FS_RES_OK:LV_FS_RES_FS_ERR;}
static lv_fs_res_t fs_read(lv_fs_drv_t*,void* file,void* buf,uint32_t len,uint32_t* read) {
    size_t n=std::fread(buf,1,len,static_cast<FILE*>(file));if(read)*read=n;
    return std::ferror(static_cast<FILE*>(file))?LV_FS_RES_FS_ERR:LV_FS_RES_OK;
}
static lv_fs_res_t fs_write(lv_fs_drv_t*,void* file,const void* buf,uint32_t len,uint32_t* written) {
    size_t n=std::fwrite(buf,1,len,static_cast<FILE*>(file));if(written)*written=n;
    return n==len?LV_FS_RES_OK:LV_FS_RES_FS_ERR;
}
static lv_fs_res_t fs_seek(lv_fs_drv_t*,void* file,uint32_t pos,lv_fs_whence_t whence) {
    int origin=whence==LV_FS_SEEK_SET?SEEK_SET:whence==LV_FS_SEEK_CUR?SEEK_CUR:SEEK_END;
    return std::fseek(static_cast<FILE*>(file),pos,origin)==0?LV_FS_RES_OK:LV_FS_RES_FS_ERR;
}
static lv_fs_res_t fs_tell(lv_fs_drv_t*,void* file,uint32_t* pos) {
    long n=std::ftell(static_cast<FILE*>(file));if(n<0)return LV_FS_RES_FS_ERR;*pos=n;return LV_FS_RES_OK;
}
static void* dir_open(lv_fs_drv_t*,const char* path) {
    auto name=storage_path(path);DIR* dir=opendir(name.c_str());return dir?new Directory{dir,name}:nullptr;
}
static lv_fs_res_t dir_read(lv_fs_drv_t*,void* ctx,char* name) {
    auto* d=static_cast<Directory*>(ctx);name[0]=0;
    while(auto* ent=readdir(d->dir)) {
        if(ent->d_name[0]=='.')continue;
        struct stat s{};std::string path=d->path+"/"+ent->d_name;
        if(stat(path.c_str(),&s)!=0)continue;
        // Legacy map range discovery supplies a 128-byte buffer.
        std::snprintf(name,128,"%s%.126s",S_ISDIR(s.st_mode)?"/":"",ent->d_name);break;
    }
    return LV_FS_RES_OK;
}
static lv_fs_res_t dir_close(lv_fs_drv_t*,void* ctx){auto* d=static_cast<Directory*>(ctx);closedir(d->dir);delete d;return LV_FS_RES_OK;}
void filesystem_init() {
    esp_vfs_fat_mount_config_t config{};config.format_if_mount_failed=true;config.max_files=5;config.allocation_unit_size=4096;
    static wl_handle_t wear=WL_INVALID_HANDLE;
    ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl("/config","storage",&config,&wear));
    static lv_fs_drv_t fs;lv_fs_drv_init(&fs);fs.letter='/';
    fs.open_cb=fs_open;fs.close_cb=fs_close;fs.read_cb=fs_read;fs.write_cb=fs_write;
    fs.seek_cb=fs_seek;fs.tell_cb=fs_tell;fs.dir_open_cb=dir_open;fs.dir_read_cb=dir_read;fs.dir_close_cb=dir_close;
    lv_fs_drv_register(&fs);
    HAL::SD_Init();
}
}
namespace HAL {
bool SD_Init() {
    if(xtrack::ready)return true;
    if(xtrack::card){esp_vfs_fat_sdcard_unmount(xtrack::sd_mount,xtrack::card);xtrack::card=nullptr;}
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;
    slot.clk=gpio_num_t(xtrack::sd_clk);slot.cmd=gpio_num_t(xtrack::sd_cmd);slot.d0=gpio_num_t(xtrack::sd_d0);
    slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_mount_config_t mount{};mount.format_if_mount_failed=false;mount.max_files=16;mount.allocation_unit_size=16384;
    esp_err_t err=esp_vfs_fat_sdmmc_mount(xtrack::sd_mount,&host,&slot,&mount,&xtrack::card);
    xtrack::ready=err==ESP_OK;
    if(xtrack::ready){mkdir("/sdcard/MAP",0775);mkdir("/sdcard/Track",0775);xtrack::recorder_recover();}
    else ESP_LOGW("sd","Mount: %s (FAT32 card required; no auto-format)",esp_err_to_name(err));
    return xtrack::ready;
}
void SD_Update() {
    if(lv_tick_elaps(xtrack::retry_tick)<5000)return;
    xtrack::retry_tick=lv_tick_get();
    if(!xtrack::ready) {if(SD_Init() && xtrack::sd_callback)xtrack::sd_callback(true);}
    else if(!xtrack::recorder_active() && sdmmc_get_status(xtrack::card)!=ESP_OK) {
        xtrack::ready=false;if(xtrack::sd_callback)xtrack::sd_callback(false);
    }
}
bool SD_GetReady(){return xtrack::ready;}
float SD_GetCardSizeMB(){return xtrack::ready?double(xtrack::card->csd.capacity)*xtrack::card->csd.sector_size/1048576.0:0;}
const char* SD_GetTypeName(){return xtrack::ready?"SDMMC FAT32":"NOT MOUNTED";}
void SD_SetEventCallback(SD_CallbackFunction_t cb){xtrack::sd_callback=cb;}
}
