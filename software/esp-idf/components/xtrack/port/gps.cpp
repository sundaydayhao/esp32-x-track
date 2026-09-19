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
// Keep a bounded sample for diagnosing a receiver that talks but has no fix.
// These sentences never bypass the navigation parser's validity checks.
static char rx_line[160]{}, last_rmc[160]{}, last_gga[160]{}, last_gp_gsv[160]{}, last_bd_gsv[160]{};
static size_t rx_size=0;
static void capture_sentence(char c) {
    if(c=='$'){rx_size=0;rx_line[rx_size++]=c;return;}
    if(!rx_size || c=='\r')return;
    if(c=='\n') {
        rx_line[rx_size]=0;
        if(rx_size>=7 && rx_line[6]==',') {
            char* target=nullptr;
            if(!std::strncmp(rx_line+3,"RMC",3))target=last_rmc;
            else if(!std::strncmp(rx_line+3,"GGA",3))target=last_gga;
            else if(!std::strncmp(rx_line+1,"GPGSV",5))target=last_gp_gsv;
            else if(!std::strncmp(rx_line+1,"BDGSV",5) || !std::strncmp(rx_line+1,"GBGSV",5))target=last_bd_gsv;
            if(target)std::memcpy(target,rx_line,rx_size+1);
        }
        rx_size=0;return;
    }
    if(rx_size+1>=sizeof(rx_line)){rx_size=0;return;}
    rx_line[rx_size++]=(c>=32 && c<=126)?c:'?';
}
static bool request_casic_mode() {
    // CASIC protocol section 1.6.5. These include the NMEA XOR and CR/LF.
    static constexpr const char* commands[]={"", "$PCAS04,1*18\r\n", "$PCAS04,2*1B\r\n", "$PCAS04,3*1A\r\n"};
    const char* command=commands[CONFIG_XTRACK_GPS_CASIC_MODE];
    const size_t length=std::strlen(command);
    const int written=uart_write_bytes(UART_NUM_1,command,length);
    if(written!=int(length) || uart_wait_tx_done(UART_NUM_1,pdMS_TO_TICKS(500))!=ESP_OK) {
        ESP_LOGE("gps","CASIC mode request failed to transmit");return false;
    }
    ESP_LOGI("gps","CASIC mode %d requested via GPIO%d: %.*s; verify receiver output",
        CONFIG_XTRACK_GPS_CASIC_MODE,CONFIG_XTRACK_GPS_TX,int(length-2),command);
    return true; // UART completion is not a receiver acknowledgement.
}
Fix gps_fix() {
    if(!mutex)return {};
    xSemaphoreTake(mutex,portMAX_DELAY);
    auto f=parser.snapshot(esp_timer_get_time()/1000,CONFIG_XTRACK_GPS_MAX_AGE_MS);
    xSemaphoreGive(mutex);return f;
}
static void gps_task(void*) {
    uint8_t buffer[256];uint64_t report=0,bytes=0;
    const uint64_t started=esp_timer_get_time()/1000;
    bool mode_requested=CONFIG_XTRACK_GPS_CASIC_MODE==0;
    for(;;) {
        int n=uart_read_bytes(UART_NUM_1,buffer,sizeof(buffer),pdMS_TO_TICKS(100));
        uint64_t now=esp_timer_get_time()/1000;
        xSemaphoreTake(mutex,portMAX_DELAY);
        for(int i=0;i<n;++i){capture_sentence(char(buffer[i]));parser.feed(char(buffer[i]),now);}
        if(n>0)bytes+=n;
        auto f=parser.snapshot(now,CONFIG_XTRACK_GPS_MAX_AGE_MS);
        auto ok=parser.accepted(),bad=parser.rejected();
        xSemaphoreGive(mutex);
        if(!mode_requested && now-started>=3000 && ok>0) {
            mode_requested=true;
            if(request_casic_mode()) {
                xSemaphoreTake(mutex,portMAX_DELAY);
                parser=NmeaParser{}; // A fix from the previous mode is no longer usable.
                xSemaphoreGive(mutex);
                rx_size=0;last_rmc[0]=last_gga[0]=last_gp_gsv[0]=last_bd_gsv[0]=0;
                bytes=0;report=now;
                ESP_LOGI("gps","Cleared pre-switch samples; waiting for new NMEA");
                continue;
            }
        }
        if(now-report>=10000) {
            report=now;
            ESP_LOGI("gps","RX=%llu bytes NMEA=%lu rejected=%lu fix=%d sats=%d hdop=%.1f",(unsigned long long)bytes,(unsigned long)ok,(unsigned long)bad,f.valid,f.satellites,f.hdop);
            if(!f.valid) {
                if(*last_rmc)ESP_LOGI("gps","last RMC: %s",last_rmc);
                if(*last_gga)ESP_LOGI("gps","last GGA: %s",last_gga);
                if(*last_gp_gsv)ESP_LOGI("gps","last GPS GSV: %s",last_gp_gsv);
                if(*last_bd_gsv)ESP_LOGI("gps","last BDS GSV: %s",last_bd_gsv);
            }
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
    ESP_LOGI("gps","UART1 RX=GPIO%d TX=GPIO%d baud=%d (8N1)",CONFIG_XTRACK_GPS_RX,CONFIG_XTRACK_GPS_TX,CONFIG_XTRACK_GPS_BAUD);
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
