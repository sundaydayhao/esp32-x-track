#include "xtrack_board.hpp"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cmath>
namespace xtrack {
static std::atomic<uint32_t> tone{0};
static esp_codec_dev_handle_t codec=nullptr;
void audio_frequency(uint32_t frequency){tone.store(frequency<8000?frequency:0);}
static void audio_task(void*) {
    int16_t pcm[320];double phase=0;
    for(;;) {
        uint32_t frequency=tone.load();
        for(int i=0;i<160;++i){phase+=frequency*6.283185307179586/16000;if(phase>6.283185307179586)phase-=6.283185307179586;
            pcm[i*2]=pcm[i*2+1]=frequency?int16_t(std::sin(phase)*3500):0;}
        if(esp_codec_dev_write(codec,pcm,sizeof(pcm))!=ESP_CODEC_DEV_OK){ESP_LOGE("audio","PCM write failed");vTaskDelay(pdMS_TO_TICKS(100));}
    }
}
void audio_start() {
    if(!i2c_probe(0x18)){ESP_LOGW("audio","ES8311 not detected");return;}
    i2s_chan_handle_t tx=nullptr;
    i2s_chan_config_t ch=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    ch.auto_clear=true;
    ESP_ERROR_CHECK(i2s_new_channel(&ch,&tx,nullptr));
    i2s_std_config_t cfg{};cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000);
    cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk=GPIO_NUM_38;cfg.gpio_cfg.bclk=GPIO_NUM_14;cfg.gpio_cfg.ws=GPIO_NUM_13;
    cfg.gpio_cfg.dout=GPIO_NUM_45;cfg.gpio_cfg.din=GPIO_NUM_NC;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx,&cfg));
    audio_codec_i2c_cfg_t bus{};bus.port=0;bus.addr=ES8311_CODEC_DEFAULT_ADDR;bus.bus_handle=i2c_bus;
    const auto* ctrl=audio_codec_new_i2c_ctrl(&bus);
    es8311_codec_cfg_t chip{};chip.ctrl_if=ctrl;chip.gpio_if=audio_codec_new_gpio();
    chip.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC;chip.pa_pin=-1;chip.use_mclk=true;
    chip.hw_gain.pa_voltage=3.3;chip.hw_gain.codec_dac_voltage=3.3;
    const auto* codec_if=es8311_codec_new(&chip);
    audio_codec_i2s_cfg_t data{};data.port=0;data.tx_handle=tx;
    esp_codec_dev_cfg_t dev{};dev.dev_type=ESP_CODEC_DEV_TYPE_OUT;dev.codec_if=codec_if;dev.data_if=audio_codec_new_i2s_data(&data);
    codec=esp_codec_dev_new(&dev);if(!codec){ESP_LOGE("audio","Codec allocation failed");return;}
    esp_codec_dev_sample_info_t info{};info.bits_per_sample=16;info.channel=2;info.sample_rate=16000;info.mclk_multiple=256;
    if(esp_codec_dev_open(codec,&info)!=ESP_CODEC_DEV_OK){ESP_LOGE("audio","Codec open failed");return;}
    esp_codec_dev_set_out_vol(codec,55);expander_set(2,true);
    ESP_ERROR_CHECK(xTaskCreate(audio_task,"audio",3072,nullptr,4,nullptr)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
}
