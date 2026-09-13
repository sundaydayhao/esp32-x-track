#include "HAL.h"
#include "App.h"
#include "xtrack_board.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main()
{
    lv_init();
    HAL::HAL_Init();
    xtrack::filesystem_init();
    xtrack::display_init();
    App_Init();
    HAL::Power_SetEventCallback(App_Uninit);
    xtrack::controls_init();
    int64_t last = esp_timer_get_time();
    for (;;) {
        const int64_t now = esp_timer_get_time();
        const uint32_t elapsed = (now - last) / 1000;
        lv_tick_inc(elapsed);
        last += int64_t(elapsed) * 1000;
        HAL::HAL_Update();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
