# ESP32_X-Track
X-Track porting to ESP32 MCU.

Original X-Track Repository: https://github.com/FASTSHIFT/X-TRACK.git

新增原生 **ESP-IDF 5.5.x / 立创·实战派 ESP32-S3 标准版**工程：
[接线、编译、离线地图和验收说明](software/esp-idf/README.md)。

适用场景为户外/骑行离线地图与 GPX 轨迹记录。外接 UART GNSS 使用多功能口
GPIO10（RX）/GPIO11（TX），无需 4G。原 Arduino 工程保留，IDF 工程复用其应用页面与资源。
当前已完成软件实现和编译/主机测试；开发板、GNSS 和续航仍需按说明进行实机验收。

商业版本的屏幕尺寸、主控路线、续航预算与打样计划见：
[商业版本主控与屏幕选型建议](docs/commercial-hardware-selection.zh-CN.md)。
