set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../arduino-platformio-vscode/lib/App")
file(GLOB_RECURSE APP_SOURCES "${APP_ROOT}/*.cpp" "${APP_ROOT}/*.c")
# Reuse the original application/pages/resources; replace hardware and Arduino-only services.
list(FILTER APP_SOURCES EXCLUDE REGEX "/Utils/(GPX|GPX_Parser|Stream|Time|new|lv_img_png|StorageService)/")
list(FILTER APP_SOURCES EXCLUDE REGEX "/Common/DataProc/DP_(Recorder|SportStatus|TzConv)\\.cpp$")
file(GLOB PORT_SOURCES "port/*.cpp" "core/*.cpp")
idf_component_register(SRCS ${APP_SOURCES} ${PORT_SOURCES}
    INCLUDE_DIRS "include" "core" "${APP_ROOT}" "${APP_ROOT}/Common/DataProc"
    REQUIRES lvgl esp_codec_dev esp_driver_uart esp_driver_i2c esp_driver_spi
        esp_driver_i2s esp_driver_ledc esp_driver_gpio esp_driver_sdmmc esp_lcd
        esp_timer esp_pm esp_hw_support fatfs sdmmc nvs_flash json)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format -Wno-missing-field-initializers)
target_compile_definitions(${COMPONENT_LIB} PUBLIC XTRACK_IDF=1)
