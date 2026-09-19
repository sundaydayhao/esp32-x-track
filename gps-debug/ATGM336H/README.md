# ATGM336H 串口定位测试

参考相邻 `../NEO-6M/` 的读取方式，单独实现 ATGM336H 的只读测试。
本目录的脚本不发送 UBX、PCAS、复位或保存配置命令。

## 运行

依赖 Python 3 和 `pyserial`（本机已有）。从项目根目录运行：

```powershell
python .\gps-debug\ATGM336H\gps_test.py --list-ports
python -u .\gps-debug\ATGM336H\gps_test.py --port COM33 --baudrate 9600 --duration 300 --environment indoors
```

COM33 是本次实际枚举的 Prolific USB-to-Serial 端口，不作为永久默认值。
不传 `--port` 时，只有系统恰好存在一个串口才会自动选择。
`--duration` 单位为秒，默认 300 秒；每 10 秒输出一次搜星状态。

## 判定与日志

- 校验 NMEA 帧和 XOR 校验和；按语句后缀解析，兼容 BD/GB/GP/GN 等 talker。
- GGA 为真实定位质量 1–5、使用卫星数大于 0，RMC 状态为 A，排除估算/模拟模式。
- 同一 UTC 时刻的 GGA/RMC 都必须有合法经纬度；两者每个坐标的差值不超过 0.0001 度，接收间隔不超过 2.5 秒。
- 默认需要连续 3 个不同 UTC 时刻的匹配结果，时刻和接收时间间隔均不超过 2.5 秒。此规则面向本次约 1 Hz 的输出；低频输出需要调整规则。
- GSA 的定位类型取正确的第 2 号字段（从语句名为 0 开始），GSV 按 talker 分别记录可见卫星数，避免跨星座重复相加。
- `PASS` 表示本次曾确认连续定位；是否在结束时仍有定位，可查看 JSON 的最后有效定位时间及 `latest` 消息接收时间。

每次运行在 `logs/<本地时间>/` 生成：

| 文件 | 内容 |
| --- | --- |
| `raw.bin` | 串口收到的完整原始字节，包括残帧和非 NMEA 数据 |
| `nmea.log` | 按行保存接收到的数据，供人工检查 |
| `summary.json` | 测试环境、设备标识、统计、最新状态、定位证据和最终结论 |

退出码：`0` 定位通过（或列出端口完成），`1` 收到合法 NMEA 但未确认定位，`2` 串口/日志错误或无合法 NMEA，`130` 手动中断。
首次定位耗时从脚本打开串口开始计时，不是从模块上电计时，不等同于冷/热启动 TTFF。

解析器检查：

```powershell
python -m unittest discover -s .\gps-debug\ATGM336H -p "test_*.py" -v
```

这些合成数据测试只验证解析和判定逻辑，实机结论以串口日志为准。

## 本次实机记录

- 模块型号由用户指定为 ATGM336H，具体后缀尚未确认；串口数据本身不证明芯片型号。
- 端口：COM33，Prolific USB-to-Serial，USB VID:PID `067B:2303`。
- 串口参数：9600、8N1。
- 测试条件：用户确认目前只能室内测试。
- 初始读数：`BDGGA` 质量 0、使用卫星 00；`BDRMC` 状态 V；`BDGSA` 类型 1；`BDGSV` 可见卫星 00；`GPTXT` 输出 `ANTENNA OK`。
- 300 秒持续测试进行中，完成后补充最终统计。

`ANTENNA OK` 是模块输出的天线检测提示，不是卫星信号或定位成功的证据。
室内未定位不能据此认定模块损坏；需要在天空无遮挡条件下复测才能评价该条件下的定位能力。
