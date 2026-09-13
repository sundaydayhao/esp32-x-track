#include "HAL.h"
#include "xtrack_board.hpp"
#include "navigation.hpp"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstring>
namespace xtrack {
static NmeaParser parser;
static SemaphoreHandle_t mutex;
Fix gps_fix() {
    if(!mutex)return {};
    xSemaphoreTake(mutex,portMAX_DELAY);
    auto f=parser.snapshot(esp_timer_get_time()/1000,CONFIG_XTRACK_GPS_MAX_AGE_MS);
    xSemaphoreGive(mutex);return f;
}
static void gps_task(void*) {
    uint8_t buffer[256];uint64_t report=0;
    for(;;) {
        int n=uart_read_bytes(UART_NUM_1,buffer,sizeof(buffer),pdMS_TO_TICKS(100));
        uint64_t now=esp_timer_get_time()/1000;
        xSemaphoreTake(mutex,portMAX_DELAY);
        for(int i=0;i<n;++i)parser.feed(char(buffer[i]),now);
        auto f=parser.snapshot(now,CONFIG_XTRACK_GPS_MAX_AGE_MS);
        auto ok=parser.accepted(),bad=parser.rejected();
        xSemaphoreGive(mutex);
        if(now-report>=10000) {
            report=now;
            ESP_LOGI("gps","NMEA=%lu rejected=%lu fix=%d sats=%d hdop=%.1f",(unsigned long)ok,(unsigned long)bad,f.valid,f.satellites,f.hdop);
        }
    }
}
void gps_start() {
    static_assert(CONFIG_XTRACK_GPS_RX!=CONFIG_XTRACK_GPS_TX,"GPS RX and TX must use different pins");
    mutex=xSemaphoreCreateMutex();ESP_ERROR_CHECK(mutex?ESP_OK:ESP_ERR_NO_MEM);
    uart_config_t cfg{};cfg.baud_rate=CONFIG_XTRACK_GPS_BAUD;cfg.data_bits=UART_DATA_8_BITS;
    cfg.parity=UART_PARITY_DISABLE;cfg.stop_bits=UART_STOP_BITS_1;cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;cfg.source_clk=UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1,4096,0,0,nullptr,0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1,&cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1,CONFIG_XTRACK_GPS_TX,CONFIG_XTRACK_GPS_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(xTaskCreate(gps_task,"gps_rx",4096,nullptr,5,nullptr)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
}
namespace HAL {
void GPS_Init(){xtrack::gps_start();}
void GPS_Update(){} // UART is serviced independently of the GUI and SD writes.
bool GPS_GetInfo(GPS_Info_t* info) {
    const auto f=xtrack::gps_fix();*info={};
    info->isVaild=f.valid;info->latitude=f.latitude;info->longitude=f.longitude;
    info->altitude=f.altitude;info->speed=f.speed_kmh;info->course=f.course;info->satellites=f.satellites;
    info->clock.year=f.year;info->clock.month=f.month;info->clock.day=f.day;
    info->clock.hour=f.hour;info->clock.minute=f.minute;info->clock.second=f.second;
    return f.valid;
}
bool GPS_LocationIsValid(){return xtrack::gps_fix().valid;}
double GPS_GetDistanceOffset(GPS_Info_t* f,double lng,double lat){return xtrack::distance_m(f->latitude,f->longitude,lat,lng);}
}
