# X-Track · ESP-IDF 5.5 / 立创实战派 ESP32-S3

本工程面向**标准版立创·实战派 ESP32-S3，2.0 英寸触摸屏、N16R8**，提供户外/骑行离线地图与轨迹记录固件。原 X-Track 的仪表、地图、状态栏、系统信息页和资源继续复用；硬件驱动、定位解析、轨迹写入和持久化使用原生 ESP-IDF，无 Arduino 运行时依赖。

**完整度结论：软件已实现并通过 ESP-IDF 5.5.5 编译与主机核心测试，尚未完成实机验收。** 标准板可作为此项目的开发平台，必须加外置 GNSS 和 TF 卡；4G 不是离线地图、定位、测速或 GPX 记录的前提。显示触摸、实际定位质量、骑行误差、拔卡行为、扬声器和整机续航须在组装后验证。不能把编译通过等同于可量产或户外可靠性认证。

**功能对应关系**

| 功能 | 本次实现 | 标准板条件/限制 |
|---|---|---|
| 仪表与页面切换 | 原 LVGL 8 页面、速度/均速/里程/运动时间/热量、状态栏 | 触摸操作；热量为估算，单次统计从启动开始，与录制暂停独立 |
| 离线地图 | SD 上的 256×256 XYZ 瓦片、缩放、位置/运动方向箭头、当次轨迹线 | 默认 WGS84，支持原有 GCJ-02 显示变换；无内置地图数据、地址搜索或道路算路 |
| GNSS 定位 | UART 独立任务、NMEA 校验、RMC/GGA 配对、卫星/HDOP/时效检查 | 必须外接模块；默认 9600、8N1、1 Hz，有效时间超过 2.5 秒即失效 |
| 轨迹记录 | 开始/暂停/继续/结束，GPX 1.1，WGS84 经纬度和 UTC 时间 | TF 卡，文件在 `Track`；恢复定位和继续录制时另起轨迹段 |
| 异常记录恢复 | 每点刷新并同步，`.gpx.part` 日志，重启/重新挂卡时补齐完整行并恢复 GPX | 能恢复已写入的完整行；不能保证突然断电时 SD 控制器缓存或 FAT 永不损坏 |
| 时间与配置 | GNSS 首次有效定位校时，显示时区与 GPX UTC 分离，配置/累计统计持久化 | 默认 UTC+8；每 60 秒及正常休眠保存，突然掉电可能损失最近一次累计统计 |
| 触摸/按键/亮度 | FT6336 系列触摸、BOOT 返回/休眠、背光滑条、声音开关 | 页面右下角齿轮为设置，替代原板旋钮交互 |
| 提示音 | ES8311 + I2S + 板载扬声器播放原提示旋律 | 必须实测音量和初始化，未接入语音识别或麦克风功能 |
| 姿态和计步 | QMI8658 加速度/角速度，阈值计步 | 计步为实验算法，骑行振动可误计；不用于距离计算 |
| 静止罗盘 | 可选 QMC5883L 驱动与信息显示 | 标准板无磁力计，默认 `NOT FITTED`；角度未作硬铁/软铁或倾斜校准，不作为可靠指南针 |
| 气压/温湿度 | 可选 BMP280 + SHT3x | 标准板没有；未安装显示 `NOT FITTED`，单项缺失显示 nan |
| 电量/电压 | 可选 MAX17048 | 标准板没有电池充电/电量管理，默认 `--`；没有充电状态检测信号 |
| 低功耗 | 保存记录、关背光和功放、ESP32 深睡眠、BOOT 唤醒 | 不是整板断电；GNSS、USB HUB 等仍可能耗电，默认不自动休眠 |

“全部功能”对应当前 X-Track 应用功能。标准板额外提供的摄像头、语音助手、蓝牙应用不属于本项目功能。历史 GPX 可取卡导出，本次没有增加历史轨迹浏览或导航算路。原硬件目录的自制 PCB 尚未形成完整布局，不作为本标准板接线依据。

**GPS 选型与接线**

建议购买 **MAX-M10S 的 UART 成品转接模块 + 配套天线**，明确选择 3.3 V UART 电平版本。官方模块默认输出 9600 8N1 NMEA，包含 RMC/GGA，可由本固件直接解析；转接板卖家可能修改出厂波特率，购买时核对。也可使用其它输出同一协议的 GPS/北斗多星座模块，固件不绑定厂商二进制协议。[u-blox MAX-M10S 数据手册，§5.3](https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf)

| 实战派多功能口信号 | GNSS 模块端 | 用途 |
|---|---|---|
| GPIO10 | TX/TXD | ESP32 UART1 接收 NMEA，必接 |
| GPIO11 | RX/RXD | 预留 ESP32 发送，接入可用；当前固件不发送厂商配置命令 |
| GND | GND | 共地，必接 |
| 3V3 **或** 5V | 模块电源输入 | 只选一路，严格按成品模块供电规格 |

MAX-M10S 裸模块不能直接接 5 V。转接板即使接受 5 V 供电，其 UART 仍须为 3.3 V；不要接 RS-232 或 5 V TTL 信号。仅接 TX/GND/适合的电源即可接收定位；无需 PPS。按丝印/原理图识别 GH 5P 端子的信号，表格不是连接器从左到右的物理针序。先断电接线，再上电，在室外开阔处测试天线。GPIO10/11 引出事实及外拓供电见[立创标准板电路介绍](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/introduction.html)。

首次搭建所需：现有标准板、UART GNSS 转接模块及天线、GH 5P 转接线、FAT32 TF 卡、稳定的 USB 电源/充电宝。先使用充电宝供电；独立锂电版本需要另做充电、保护、稳压和功耗测试。只有增加无手机远程上传、实时共享或远程管理等需求时，再评估 4G 的电源、天线、SIM 和接口资源。

**固定硬件分配**

| 外设 | 信号分配 |
|---|---|
| 公共 I2C0 | SDA1 / SCL2；PCA9557 `0x19`，触摸 `0x38`，QMI8658 `0x6A`，ES8311 `0x18` |
| ST7789 / SPI3 | MOSI40 / CLK41 / DC39 / BL42；CS 为 PCA9557 bit0；320×240，SPI mode2 |
| SDMMC 1-bit | CLK47 / CMD48 / D0=21 |
| I2S 扬声器 | MCLK38 / BCLK14 / WS13 / DOUT45，PA_EN 为 PCA9557 bit1 |
| BOOT | GPIO0，低有效；长按 5 秒后松开休眠 |
| 外接 GNSS / UART1 | 默认 RX10 / TX11，可在 menuconfig 中交换 |
| 可选 I2C 扩展 | QMC5883L `0x0D`，BMP280 `0x76` 或 `0x77`，SHT3x `0x44`，MAX17048 `0x36` |

AHT10/20 固定地址 `0x38` 与板载触摸冲突，因此温湿度驱动改用 SHT3x。外接传感器走专用 I2C 口，不能把板载共用 I2C 改作 GPS UART。QMI8658 是六轴器件，不能提供静止绝对航向。

**编译与烧录**

已在 Windows 的 ESP-IDF **v5.5.5** 上编译，工程约束为 `>=5.5,<5.6`。使用已安装的 ESP-IDF 5.5.x，首次构建需联网获取锁定的 LVGL 8.3.11 与 esp_codec_dev 1.5.4。

```powershell
cd C:\projects\ESP32_X-Track\software\esp-idf
.\tools\build.ps1 -IdfPath C:\esp\v5.5.5\esp-idf
```

或者在 ESP-IDF 5.5 已激活的终端中：

```text
idf.py build
idf.py menuconfig
idf.py -p COM实际端口 flash monitor
```

`COM实际端口` 必须替换成实际连接的开发板串口（CH340 或原生 USB Serial/JTAG）；Linux 使用对应串口设备。烧录会替换板载固件。默认采用 UART0 日志，并启用 USB Serial/JTAG 辅助输出；GPS 独占 UART1。监视器每 10 秒报告接收字节数、NMEA accepted/rejected、fix、卫星数和 HDOP；未定位时额外输出最近的 RMC、GGA、GSV 报文样本。

遇到 `NO GPS FIX` 时先查看日志：

- `RX` 与 `NMEA` 持续增长：模块正在发送固件能解析的报文，不等于已经定位。
- RMC 状态 `V`、GGA 定位质量 `0`：模块自己报告未定位。检查天线连接，在室外无遮挡处持续通电搜星。
- 接收字节不增长：检查供电、共地和模块 TX → GPIO10；有字节但 NMEA 不增长或 rejected 持续增长，再检查波特率、信号电平和协议。
- RMC 为 `A`、GGA 质量非零仍不能定位：对照两条报文的 UTC、卫星数、HDOP 和新鲜度检查，保留原始样本再排查。

`last` 报文是最近一次收到的样本，断线后可能保留旧值，需结合递增计数与报文 UTC 判断。汇总 HDOP 来自最后完整定位结果，尚无完整定位时默认 `99.0`，不代表原始 GGA 字段的值。2026-09-19 的 ATGM336H 接板实测见 [VALIDATION.md](VALIDATION.md)。

ATGM336H/CASIC 接收机可在 `idf.py menuconfig` → `X-Track / LCKFB SZP ESP32-S3` 设置 `CASIC constellation mode`（`CONFIG_XTRACK_GPS_CASIC_MODE`），重新编译烧录后生效：

| 值 | 模式 | 发送指令（末尾含 CR/LF） |
|---|---|---|
| 0（默认） | 保持模块原有配置，不发送命令 | 无 |
| 1 | GPS 单模 | `$PCAS04,1*18` |
| 2 | 北斗单模 | `$PCAS04,2*1B` |
| 3 | GPS + 北斗双模 | `$PCAS04,3*1A` |

协议依据为[中科微 CASIC 协议 §1.6.5](https://www.icofchina.com/d/file/xiazai/2017-05-02/ea0cdd3d81eeebcc657b5dbca80925ee.pdf)。只对兼容 CASIC 的模块启用，u-blox 等模块保持 0。必须连接 **GPIO11 → 模块 RX**；仅接模块 TX 可以读取定位，但无法切换模式。ESP32 启动至少 3 秒且收到可解析 NMEA 后发送一次，不发送保存配置命令；若模块单独重新上电，需要重启 ESP32 才会再次发送。

日志 `CASIC mode ... requested` 只证明 UART 发送完成，须检查后续 RMC/GGA/GSV 是否改变来确认模块执行。切换时清除旧定位和报文样本，GPS 与北斗的 GSV 分别记录。切回双模应设置为 3；设置 0 只停止发送命令，不能主动恢复双模。切换单模成功也不等于定位成功，仍需 RMC/GGA 的有效状态、时效和定位质量满足要求。

输出为 `build/xtrack_szp.bin`、`build/bootloader/bootloader.bin`、`build/partition_table/partition-table.bin`；优先用 `idf.py flash`，单独应用 bin 不能覆盖完整首次烧录流程。分区为 NVS、PHY、6 MiB factory app 与余下内部 FAT 配置区，没有 OTA 双分区。16 MB Flash + 8 MB octal PSRAM 参数已写入默认配置。内部 FAT 首次挂载失败会格式化该配置分区；SD 卡始终不会自动格式化。

可选传感器在 `menuconfig → X-Track / LCKFB SZP ESP32-S3` 启用。无这些模块时保持关闭。独立编译全部可选驱动的 PowerShell 命令：

```powershell
idf.py -B build-sensors -D "SDKCONFIG=$PWD/build-sensors/sdkconfig" -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.sensors' build
```

**离线地图与配置**

准备可合法离线使用的 Web Mercator XYZ 瓦片，目录为 `z/x/y.png` 或 `z/x/y.jpg`、单图 256×256、zoom 0..19。TMS 瓦片需要先反转 y，MBTiles 数据库需要先导出为 XYZ。建议准备连续的缩放层级并覆盖计划骑行区域；未提供的区域/层级会缺图。工具不联网下载地图。

在用于地图转换的 Python 环境安装 Pillow，然后转换：

```text
python -m pip install Pillow
python tools/prepare_maps.py --input D:/tiles --output E:/MAP --coordinates wgs84
```

`E:` 是示例 TF 卡盘符，必须替换成实际路径。目标已存在会报错，确实需要更新时加 `--overwrite`。输出是 LVGL 8 的 16-bit RGB565 大端像素文件，与固件的 `LV_COLOR_16_SWAP` 匹配，单瓦片 131076 字节。保留地图提供方要求的署名资料。工具的 `--coordinates` 只声明坐标系，不对图像做纠偏。

SD 卡目录示例：

```text
MAP/16/54870/26922.bin
MAP/manifest.json
Track/                         # 固件自动创建，GPX 导出目录
XTrackConfig.json               # 可选配置导入
```

转换工具生成 `MAP/XTrackConfig.example.json`。复制至 TF 根目录并命名为 `XTrackConfig.json` 后，在启动/重新挂卡时导入；未知键忽略，只允许修改 sysConfig 配置，不导入累计里程。示例：

```json
{
  "sysConfig.mapDirPath": "/MAP",
  "sysConfig.mapExtName": "bin",
  "sysConfig.mapWGS84": true,
  "sysConfig.timeZone": 8,
  "sysConfig.longitude": 121.5,
  "sysConfig.latitude": 31.2
}
```

WGS84/通常的 OSM XYZ 设 `true`；使用 GCJ-02 底图设 `false`。GPX 始终保存 WGS84，不随底图切换。PNG 也可直接使用：保留 XYZ PNG 并把扩展名设为 `png`，但运行时解码更占内存。默认地点仅用于无定位时预览，不能作为当前位置；界面会显示 `NO GPS FIX`。时区支持整数 -12..14，影响显示，不影响 GPX 的 `Z` 时间。

内部配置自动保存且备份；导入文件保留在卡上时，每次挂卡都会重新应用其中的值。设置生效并正常保存后可移走该文件。地图坐标系与目录变更后退出再进入地图页；建议重启确认。不要在录制期间拔卡。

**操作**

- 仪表底部：地图、录制、系统信息。地图通过缩放滑条调整级别；系统信息可上下滑动。短按 BOOT 返回；首页短按 BOOT 打开设置。
- 右下角齿轮：亮度、声音、Back、Sleep、Close；Close 保存设置。
- 等待 SD 就绪和 GPS 有效定位，**长按录制**开始；短按暂停，再短按继续。
- 暂停后长按进入 STOP 待结束状态，再长按完成并关闭 GPX；待结束状态短按可继续。这保留了原 X-Track 的操作状态机。
- BOOT 长按 5 秒，松开后保存并休眠；或设置页点 Sleep。保存失败会取消休眠并显示录制错误。BOOT 再次唤醒，单次运动统计重新开始。
- 录制失败显示 `REC ERROR`，停止写入后按钮复位；存储恢复后可重新长按开始。保留的 `.part` 会在下次挂卡时尝试恢复。若卡无法恢复，请先取卡备份文件，再处理文件系统。

**结构与验证**

本次构建大小、39 项核心检查、固件 SHA256 及尚未完成的硬件验证见 [VALIDATION.md](VALIDATION.md)。

`components/xtrack/port` 是板级、SD/LVGL 文件系统、服务和触摸操作适配；`core` 是可在 PC 测试的 NMEA、里程和 GPX 实现。CMake 直接复用相邻 Arduino 工程的 App 页面/资源，排除 Arduino 专用服务，避免复制整个 UI。全部 LVGL/页面消息在主任务执行；GPS 接收和 PCM 播放有独立任务。

```text
python tools/test_core.py --cxx g++
python tests/test_maps.py
```

Windows 可用 `--cxx C:/cygwin64/bin/g++.exe`。测试涵盖校验和、跨 talker、经纬度、RMC/GGA 时刻、断线失效、冻结时间戳去重、错误数据、距离跳点、暂停/恢复、断电日志修复；另用 XML 解析器验证坐标顺序/UTC 和分段。地图测试验证 256×256、LVGL 头、红绿蓝字节序与透明像素。

实机验收顺序：

1. 不接 GPS：启动进入界面，四角触摸方向正确，SD 容量可读，未定位提示正常；开关声音并调整亮度。
2. 接 GPS 到室外：确认日志接收到正确 NMEA，fix 有效、时间/位置正确；拔掉 GPS 后 2.5 秒左右失效且速度归零。
3. 加载实际区域地图：检查坐标系、缩放、连续骑行时地图刷新；记录固定已知路线并对照里程。
4. 开始→暂停→继续→结束，取卡用 GPX 软件检查 UTC、坐标和轨迹段；在备份/测试卡上进行掉电恢复及卡满测试。
5. 记录过程中触摸缩放和播放提示音，确认 UART 没有丢数据、界面无明显卡顿；长时运行观察堆内存。
6. 测试保存/休眠/唤醒，再测整板连同 GPS 的平均电流和充电宝自动关断，才可估算续航。外壳、防雨、阳光可读性和骑行固定方式另行验收。

驱动依据：[立创 LCD](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lcd-display.html)、[LVGL/触摸](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lvgl.html)、[SD 卡](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/sd-card.html)、[ES8311](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-output-es8311.html)、[QMI8658](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/attitude-sensor.html)、[Sensirion SHT3x 官方驱动](https://github.com/Sensirion/embedded-i2c-sht3x/blob/master/sht3x_i2c.c)。
