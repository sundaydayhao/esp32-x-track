#include "HAL.h"
#include "xtrack_board.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace {
HAL::CommitFunc_t imu_cb=nullptr,mag_cb=nullptr,pht_cb=nullptr,gy_cb=nullptr;
void *imu_ctx=nullptr,*mag_ctx=nullptr,*pht_ctx=nullptr,*gy_ctx=nullptr;
HAL::IMU_Info_t imu{};
HAL::MAG_Info_t mag{};
HAL::PHT_Info_t pht{NAN,NAN,NAN};
bool imu_ok=false,mag_ok=false,bmp_ok=false,sht_ok=false,battery_ok=false;
uint8_t bmp_addr=0x76;
uint16_t coeff_u[12]{};
HAL::Power_Info_t power{};
uint32_t last_sensor=0,last_slow=0,sht_started=0,step_tick=0;
bool sht_pending=false;
bool step_high=false;
int16_t s16(const uint8_t* p){return int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);}
}
namespace xtrack {
bool battery_available(){return battery_ok;}
bool imu_available(){return imu_ok;}
bool mag_available(){return mag_ok;}
bool pht_available(){return bmp_ok || sht_ok;}
void sensors_init() {
    uint8_t id=0;
    imu_ok=i2c_read(0x6a,0,&id,1)==ESP_OK && id==5;
    if(imu_ok){
        imu_ok=i2c_write(0x6a,0x60,0xb0)==ESP_OK;vTaskDelay(pdMS_TO_TICKS(20));
        imu_ok=imu_ok && i2c_write(0x6a,2,0x40)==ESP_OK && i2c_write(0x6a,3,0x95)==ESP_OK
            && i2c_write(0x6a,4,0xd5)==ESP_OK && i2c_write(0x6a,8,3)==ESP_OK;
    }
    ESP_LOGI("sensors","QMI8658 %s",imu_ok?"ready":"unavailable");
#if CONFIG_XTRACK_ENV_SENSORS
    mag_ok=i2c_probe(0x0d) && i2c_write(0x0d,0x0b,1)==ESP_OK && i2c_write(0x0d,0x09,0x1d)==ESP_OK;
    for(uint8_t addr:{uint8_t(0x76),uint8_t(0x77)}) {
        if(i2c_read(addr,0xd0,&id,1)==ESP_OK && id==0x58){bmp_addr=addr;bmp_ok=true;break;}
    }
    if(bmp_ok){uint8_t raw[24];bmp_ok=i2c_read(bmp_addr,0x88,raw,24)==ESP_OK;
        if(bmp_ok)for(int i=0;i<12;++i)coeff_u[i]=uint16_t(raw[2*i])|uint16_t(raw[2*i+1])<<8;
        bmp_ok=bmp_ok && coeff_u[3]!=0 && i2c_write(bmp_addr,0xf5,0x90)==ESP_OK && i2c_write(bmp_addr,0xf4,0x57)==ESP_OK;}
    sht_ok=i2c_probe(0x44); // AHT10/20's fixed 0x38 would conflict with onboard touch.
#endif
#if CONFIG_XTRACK_MAX17048
    uint8_t version[2];battery_ok=i2c_read(0x36,8,version,2)==ESP_OK;
#endif
}
static void bmp_read() {
    uint8_t raw[6];if(i2c_read(bmp_addr,0xf7,raw,6)!=ESP_OK){pht.pressure=NAN;return;}
    int32_t adc_p=(int32_t(raw[0])<<12)|(raw[1]<<4)|(raw[2]>>4),adc_t=(int32_t(raw[3])<<12)|(raw[4]<<4)|(raw[5]>>4);
    auto s=[](int i){return double(int16_t(coeff_u[i]));};
    double v1=(adc_t/16384.0-coeff_u[0]/1024.0)*s(1);
    double v2=(adc_t/131072.0-coeff_u[0]/8192.0);v2=v2*v2*s(2);
    double fine=v1+v2;pht.temperature=fine/5120.0;
    v1=fine/2-64000;v2=v1*v1*s(8)/32768;v2+=v1*s(7)*2;v2=v2/4+s(6)*65536;
    v1=(s(5)*v1*v1/524288+s(4)*v1)/524288;v1=(1+v1/32768)*coeff_u[3];
    if(std::abs(v1)<1e-9){pht.pressure=NAN;return;}
    double p=(1048576.0-adc_p-v2/4096)*6250/v1;
    p+=(s(11)*p*p/2147483648+p*s(10)/32768+s(9))/16;
    pht.pressure=p; // Pa; the view converts to hPa.
}
void sensors_update() {
    if(lv_tick_elaps(last_sensor)<20)return;
    last_sensor=lv_tick_get();
    if(imu_ok){uint8_t raw[12];
        if(i2c_read(0x6a,0x35,raw,12)==ESP_OK){
            imu.ax=s16(raw)/8192.0f;imu.ay=s16(raw+2)/8192.0f;imu.az=s16(raw+4)/8192.0f;
            imu.gx=s16(raw+6)/64.0f;imu.gy=s16(raw+8)/64.0f;imu.gz=s16(raw+10)/64.0f;
            float magnitude=std::sqrt(imu.ax*imu.ax+imu.ay*imu.ay+imu.az*imu.az);
            if(magnitude>1.2f && !step_high && lv_tick_elaps(step_tick)>300){imu.steps+=1;step_tick=lv_tick_get();step_high=true;}
            if(magnitude<1.05f)step_high=false;
            if(imu_cb)imu_cb(&imu,imu_ctx);
        }
    }
    if(sht_pending && lv_tick_elaps(sht_started)>=20) {
        uint8_t data[6];sht_pending=false;
        auto crc=[](const uint8_t* p){uint8_t c=0xff;for(int i=0;i<2;++i){c^=p[i];for(int b=0;b<8;++b)c=(c&0x80)?(c<<1)^0x31:c<<1;}return c;};
        if(i2c_receive(0x44,data,6)==ESP_OK && crc(data)==data[2] && crc(data+3)==data[5]) {
            pht.temperature=-45+175.0*(uint16_t(data[0])<<8|data[1])/65535;
            pht.humidity=100.0*(uint16_t(data[3])<<8|data[4])/65535;
        } else {pht.humidity=NAN;}
        if(pht_cb)pht_cb(&pht,pht_ctx);
    }
    if(lv_tick_elaps(last_slow)<1000)return;
    last_slow=lv_tick_get();
    if(mag_ok){uint8_t raw[6];if(i2c_read(0x0d,0,raw,6)==ESP_OK){
        mag.x=s16(raw);mag.y=s16(raw+2);mag.z=s16(raw+4);double angle=std::atan2(double(mag.y),double(mag.x))*180/3.141592653589793;
        mag.dir=int(angle<0?angle+360:angle);if(mag_cb)mag_cb(&mag,mag_ctx);}}
    if(bmp_ok)bmp_read();
    if(sht_ok){const uint8_t cmd[]={0x24,0x00};if(i2c_send(0x44,cmd,2)==ESP_OK){sht_pending=true;sht_started=lv_tick_get();}}
    if(pht_cb)pht_cb(&pht,pht_ctx);
    if(battery_ok){uint8_t raw[4];if(i2c_read(0x36,2,raw,4)==ESP_OK){
        power.voltage=uint16_t(((uint16_t(raw[0])<<8|raw[1])>>4)*1.25);
        power.usage=std::min<int>(raw[2],100);power.isCharging=false;
    }else battery_ok=false;}
    if(gy_cb){HAL::GY91_Info_t all{imu,mag,pht};gy_cb(&all,gy_ctx);}
}
}
namespace HAL {
bool IMU_Init(){return imu_ok;}
void IMU_SetCommitCallback(CommitFunc_t cb,void* ctx){imu_cb=cb;imu_ctx=ctx;}
void IMU_Update(){} // All sensor commits happen in the application task.
bool MAG_Init(){return mag_ok;}
void MAG_SetCommitCallback(CommitFunc_t cb,void* ctx){mag_cb=cb;mag_ctx=ctx;}
void MAG_Update(){}
bool PHT_Init(){return xtrack::pht_available();}
void PHT_SetCommitCallback(CommitFunc_t cb,void* ctx){pht_cb=cb;pht_ctx=ctx;}
void PHT_Update(){}
bool GY91_Init(){return imu_ok && mag_ok && bmp_ok;}
void GY91_SetCommitCallback(CommitFunc_t cb,void* ctx){gy_cb=cb;gy_ctx=ctx;}
void GY91_Update(){}
void Power_GetInfo(Power_Info_t* out){*out=battery_ok?power:Power_Info_t{};}
}
