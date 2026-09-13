#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

namespace xtrack {
struct Fix;
Fix gps_fix();
constexpr int sda=1, scl=2, lcd_mosi=40, lcd_sck=41, lcd_dc=39, lcd_bl=42;
constexpr int sd_clk=47, sd_cmd=48, sd_d0=21, boot_key=0;
constexpr int width=320, height=240;
constexpr const char* sd_mount="/sdcard";
extern i2c_master_bus_handle_t i2c_bus;
esp_err_t i2c_read(uint8_t addr, uint8_t reg, void* data, size_t len);
esp_err_t i2c_write(uint8_t addr, uint8_t reg, uint8_t value);
esp_err_t i2c_send(uint8_t addr, const uint8_t* data, size_t len);
esp_err_t i2c_receive(uint8_t addr, uint8_t* data, size_t len);
bool i2c_probe(uint8_t addr);
void expander_set(uint8_t mask, bool high);
void display_init();
void filesystem_init();
void controls_init();
void controls_update();
void audio_start();
void audio_frequency(uint32_t frequency);
void sensors_init();
void sensors_update();
bool battery_available();
bool imu_available();
bool mag_available();
bool pht_available();
std::string storage_path(const char* path);
bool storage_ready();
void storage_failed();
void recorder_recover();
bool recorder_active();
const char* recorder_error();
void gps_start();
void save_settings();
}
