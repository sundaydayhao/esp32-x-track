#include "xtrack_board.hpp"
#include "HAL.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <algorithm>

namespace xtrack {
i2c_master_bus_handle_t i2c_bus=nullptr;
static i2c_master_dev_handle_t devices[128]{};
static uint8_t expander=0x05;
static esp_lcd_panel_handle_t panel=nullptr;
static lv_disp_drv_t display_driver;
static uint16_t brightness=800;

static i2c_master_dev_handle_t device(uint8_t addr) {
    if(addr>=128 || !i2c_bus) return nullptr;
    if(!devices[addr]) {
        i2c_device_config_t cfg{}; cfg.dev_addr_length=I2C_ADDR_BIT_LEN_7;
        cfg.device_address=addr;cfg.scl_speed_hz=400000;
        if(i2c_master_bus_add_device(i2c_bus,&cfg,&devices[addr])!=ESP_OK) return nullptr;
    }
    return devices[addr];
}
bool i2c_probe(uint8_t addr){return i2c_master_probe(i2c_bus,addr,30)==ESP_OK;}
esp_err_t i2c_read(uint8_t addr,uint8_t reg,void* data,size_t n) {
    auto dev=device(addr);return dev?i2c_master_transmit_receive(dev,&reg,1,static_cast<uint8_t*>(data),n,30):ESP_ERR_NOT_FOUND;
}
esp_err_t i2c_write(uint8_t addr,uint8_t reg,uint8_t value) {
    uint8_t data[]={reg,value};return i2c_send(addr,data,sizeof(data));
}
esp_err_t i2c_send(uint8_t addr,const uint8_t* data,size_t n) {
    auto dev=device(addr);return dev?i2c_master_transmit(dev,data,n,30):ESP_ERR_NOT_FOUND;
}
esp_err_t i2c_receive(uint8_t addr,uint8_t* data,size_t n) {
    auto dev=device(addr);return dev?i2c_master_receive(dev,data,n,30):ESP_ERR_NOT_FOUND;
}
void expander_set(uint8_t mask,bool high) {
    uint8_t next=high?(expander|mask):(expander&~mask);
    if(i2c_write(0x19,1,next)==ESP_OK)expander=next;
    else ESP_LOGE("board","PCA9557 output write failed");
}
void bus_init() {
    i2c_master_bus_config_t cfg{};
    cfg.i2c_port=I2C_NUM_0;cfg.sda_io_num=gpio_num_t(sda);cfg.scl_io_num=gpio_num_t(scl);
    cfg.clk_source=I2C_CLK_SRC_DEFAULT;cfg.glitch_ignore_cnt=7;cfg.flags.enable_internal_pullup=true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg,&i2c_bus));
    ESP_ERROR_CHECK(i2c_write(0x19,1,expander));
    ESP_ERROR_CHECK(i2c_write(0x19,2,0));
    ESP_ERROR_CHECK(i2c_write(0x19,3,0xf8));
    // Keep the expander outputs in the documented idle state. LCD_CS is
    // asserted again immediately after panel reset, before ST7789 init.
}
static bool flushed(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void* ctx) {
    // LVGL explicitly permits flush_ready from the transfer-completion ISR.
    lv_disp_flush_ready(static_cast<lv_disp_drv_t*>(ctx));return false;
}
static void flush(lv_disp_drv_t* drv,const lv_area_t* area,lv_color_t* pixels) {
    esp_err_t err=esp_lcd_panel_draw_bitmap(panel,area->x1,area->y1,area->x2+1,area->y2+1,pixels);
    if(err!=ESP_OK){ESP_LOGE("lcd","draw: %s",esp_err_to_name(err));lv_disp_flush_ready(drv);}
}
static void touch_read(lv_indev_drv_t*,lv_indev_data_t* out) {
    uint8_t raw[5]{};
    out->state=LV_INDEV_STATE_RELEASED;
    if(i2c_read(0x38,0x02,raw,sizeof(raw))!=ESP_OK || !(raw[0]&0xf))return;
    int x=((raw[1]&0xf)<<8)|raw[2],y=((raw[3]&0xf)<<8)|raw[4];
#if CONFIG_XTRACK_TOUCH_SWAP_XY
    std::swap(x,y);
#endif
#if CONFIG_XTRACK_TOUCH_MIRROR_X
    x=width-1-x;
#endif
#if CONFIG_XTRACK_TOUCH_MIRROR_Y
    y=height-1-y;
#endif
    if(x<0 || y<0 || x>=width || y>=height)return;
    out->point.x=x;out->point.y=y;out->state=LV_INDEV_STATE_PRESSED;
    HAL::Power_HandleTimeUpdate();
}
void display_init() {
    HAL::Backlight_Init();
    spi_bus_config_t bus{};
    bus.mosi_io_num=lcd_mosi;bus.miso_io_num=-1;bus.sclk_io_num=lcd_sck;
    bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=width*40*2;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io{};
    io.dc_gpio_num=lcd_dc;io.cs_gpio_num=-1;io.pclk_hz=40000000;io.spi_mode=2;
    io.lcd_cmd_bits=8;io.lcd_param_bits=8;io.trans_queue_depth=2;
    io.on_color_trans_done=flushed;io.user_ctx=&display_driver;
    esp_lcd_panel_io_handle_t panel_io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t(SPI3_HOST),&io,&panel_io));
    esp_lcd_panel_dev_config_t cfg{};
    cfg.reset_gpio_num=-1;cfg.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;cfg.bits_per_pixel=16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io,&cfg,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    expander_set(1,false); // LCD_CS must be low before the ST7789 init commands.
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel,true,false));
    static lv_disp_draw_buf_t draw;
    auto* a=static_cast<lv_color_t*>(heap_caps_malloc(width*40*sizeof(lv_color_t),MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
    auto* b=static_cast<lv_color_t*>(heap_caps_malloc(width*40*sizeof(lv_color_t),MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
    ESP_ERROR_CHECK(a && b?ESP_OK:ESP_ERR_NO_MEM);
    lv_disp_draw_buf_init(&draw,a,b,width*40);
    lv_disp_drv_init(&display_driver);display_driver.hor_res=width;display_driver.ver_res=height;
    display_driver.draw_buf=&draw;display_driver.flush_cb=flush;
    lv_disp_drv_register(&display_driver);
    static lv_indev_drv_t touch;lv_indev_drv_init(&touch);touch.type=LV_INDEV_TYPE_POINTER;touch.read_cb=touch_read;
    lv_indev_drv_register(&touch);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
    HAL::Backlight_SetValue(brightness);
    ESP_LOGI("board","SZP S3: LCD 320x240, FT6336, SDMMC 1-bit; GPS RX=%d TX=%d",CONFIG_XTRACK_GPS_RX,CONFIG_XTRACK_GPS_TX);
}
}
namespace HAL {
void Backlight_Init() {
    ledc_timer_config_t timer{};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.timer_num=LEDC_TIMER_0;
    timer.duty_resolution=LEDC_TIMER_10_BIT;timer.freq_hz=5000;timer.clk_cfg=LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch{};ch.gpio_num=xtrack::lcd_bl;ch.speed_mode=LEDC_LOW_SPEED_MODE;
    ch.channel=LEDC_CHANNEL_0;ch.timer_sel=LEDC_TIMER_0;ch.duty=0;
    // The SZP board's LCD backlight is active-low (GPIO42).  Invert the
    // LEDC output so a larger software brightness value means brighter LCD.
    ch.flags.output_invert=true;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}
uint16_t Backlight_GetValue(){return xtrack::brightness;}
void Backlight_SetValue(int16_t value) {
    xtrack::brightness=std::clamp<int>(value,0,1000);
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,xtrack::brightness*1023/1000);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
}
void Backlight_SetGradual(uint16_t target,uint16_t time) {
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,&xtrack::brightness);
    lv_anim_set_exec_cb(&a,[](void*,int32_t v){Backlight_SetValue(v);});
    lv_anim_set_values(&a,Backlight_GetValue(),std::min<uint16_t>(target,1000));lv_anim_set_time(&a,time);lv_anim_start(&a);
}
void Backlight_ForceLit(bool enable){if(enable)Backlight_SetValue(1000);}
void Display_DumpCrashInfo(const char* info){ESP_LOGE("xtrack","%s",info);}
}
