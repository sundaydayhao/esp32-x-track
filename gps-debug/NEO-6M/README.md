# 野火 GPS 开发板调试记录

## 硬件信息

| 项目 | 信息 |
|------|------|
| 开发板 | 野火 GPS 开发板 |
| GPS 模块 | u-blox NEO-M8N-0-001 |
| 串口 | COM31（USB 串行设备） |
| 波特率 | 9600 |
| 数据格式 | NMEA 0183（$GP 前缀，GPS 单星座） |
| 天线 | 有源陶瓷天线（IPEX/U.FL 接口） |

## 调试过程

### 1. 串口识别

- COM20（Prolific USB-to-Serial）：输出二进制数据，非 NMEA，可能是 STM32 调试口
- COM31（USB 串行设备）：输出 NMEA 语句，是 GPS 数据口

### 2. 波特率问题

串口工具默认 115200 会显示乱码，正确波特率为 **9600**。

### 3. 搜星与定位

| 环境 | 可见卫星 | 定位状态 |
|------|----------|----------|
| 室内 | 0 颗 | 未定位 |
| 窗边 | 1 颗（SNR 28dB） | 未定位 |
| 室外 | 5-6 颗（SNR 31-41dB） | **7 秒热启动定位** |

### 4. 定位结果

- 纬度：22.512995° N
- 经度：113.938359° E
- 海拔：29.1 m
- 逆地理编码：深圳市南山区粤海街道登良路（蔚蓝社区）

### 5. UBX 多星座配置

尝试通过 UBX-CFG-GNSS 启用 GPS+BeiDou+GLONASS，但模块未返回 ACK，
可能板载 MCU 将 NEO-M8N 配置为 NMEA-only 模式。

## 脚本说明

| 脚本 | 用途 |
|------|------|
| `gps_reader.py` | 基础读取：自动探测 COM 口，解析 GGA/RMC/GSA/VTG/GLL，打印定位摘要 |
| `gps_raw_reader.py` | 原始数据：读取所有 NMEA 语句并统计类型分布 |
| `gps_long_reader.py` | 长时间监控：60 秒读取，重点跟踪 GSV 卫星搜索和 GGA 定位进展 |
| `gps_multiconstellation.py` | 多星座配置：通过 UBX 命令启用 GPS+BeiDou+GLONASS + 冷启动 |
| `gps_ubx_config.py` | UBX 调试：查询当前 GNSS 配置 + 写入多星座 + 保存 + 热启动 |

## 使用方法

```bash
# 基础读取（自动探测 COM 口，20 秒）
python gps_debug/gps_reader.py

# 长时间监控搜星（60 秒）
python gps_debug/gps_long_reader.py

# UBX 多星座配置 + 读取
python gps_debug/gps_ubx_config.py
```

## NMEA 语句参考

| 语句 | 内容 |
|------|------|
| GGA | 核心定位数据（时间、经纬度、卫星数、HDOP、海拔） |
| RMC | 推荐最小数据（时间、日期、经纬度、速度、航向） |
| GSA | DOP 精度因子和活跃卫星 PRN |
| GSV | 可见卫星详情（PRN、仰角、方位角、SNR） |
| VTG | 地面速度和航向 |
| GLL | 地理位置（经纬度、时间） |

## 注意事项

1. GPS 需要直射天空信号，室内/窗边通常无法定位
2. 冷启动首次定位需 30-120 秒，热启动约 5-10 秒
3. 至少需要 4 颗卫星才能 3D 定位
4. 串口工具波特率必须设为 9600
5. 天线 IPEX 接头需插紧，天线面朝上
