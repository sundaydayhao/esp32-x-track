"""长时间读取 GPS，监控搜星进展"""
import serial
import time

port = 'COM31'
baudrate = 9600
duration = 60

print(f"打开 {port} @ {baudrate}，持续读取 {duration} 秒...")
print("重点监控 GSV（卫星搜索）和 GGA（定位）语句\n")

ser = serial.Serial(port, baudrate, timeout=2)
start = time.time()
buffer = ''
gsv_seen = set()
last_fix_sats = 0
fix_time = None

while time.time() - start < duration:
    raw = ser.read(ser.in_waiting or 1)
    if not raw:
        continue
    buffer += raw.decode('ascii', errors='ignore')

    while '\r\n' in buffer:
        line, buffer = buffer.split('\r\n', 1)
        line = line.strip()
        if not line.startswith('$'):
            continue

        if '*' not in line:
            continue
        body = line[1:line.rindex('*')]
        fields = body.split(',')
        sent_type = fields[0][-3:]

        if sent_type == 'GSV':
            # GSV: $GPGSV,numMsg,msgNum,totalSats,PRN,elev,azim,SNR,...
            try:
                total_sats = int(fields[3]) if fields[3] else 0
                msg_num = int(fields[2]) if fields[2] else 0
                num_msgs = int(fields[1]) if fields[1] else 0
                # 只在第一条 GSV 或卫星数变化时打印
                key = (msg_num, num_msgs, total_sats)
                if key not in gsv_seen or total_sats != last_fix_sats:
                    sats_info = []
                    # 解析卫星信息（每4个字段一颗卫星）
                    for i in range(4, len(fields), 4):
                        if i + 3 < len(fields) and fields[i]:
                            prn = fields[i]
                            elev = fields[i+1]
                            azim = fields[i+2]
                            snr = fields[i+3]
                            sats_info.append(f"PRN{prn}(仰角{elev}°/方位{azim}°/SNR{snr}dB)")
                    elapsed = int(time.time() - start)
                    print(f"[{elapsed:3d}s] GSV: 可见{total_sats}颗 (消息{msg_num}/{num_msgs})"
                          + (f"  {' | '.join(sats_info)}" if sats_info else ""))
                    gsv_seen.add(key)
            except (IndexError, ValueError):
                pass

        elif sent_type == 'GGA':
            try:
                fix_q = int(fields[6]) if fields[6] else 0
                sats = int(fields[7]) if fields[7] else 0
                if sats > last_fix_sats:
                    last_fix_sats = sats
                if fix_q > 0 and fix_time is None:
                    fix_time = time.time() - start
                    lat_raw = fields[2]
                    lon_raw = fields[4]
                    if lat_raw and lon_raw:
                        lat = int(lat_raw[:2]) + float(lat_raw[2:]) / 60
                        lon = int(lon_raw[:3]) + float(lon_raw[3:]) / 60
                        print(f"\n>>> [{int(fix_time)}s] *** 定位成功! ***")
                        print(f">>> 纬度: {lat:.6f}°  经度: {lon:.6f}°")
                        print(f">>> 卫星: {sats}颗  海拔: {fields[9]}m  质量: {fix_q}")
                elif sats > 0 and sats != last_fix_sats:
                    elapsed = int(time.time() - start)
                    print(f"[{elapsed:3d}s] GGA: 搜到{sats}颗卫星，尚未定位")
            except (IndexError, ValueError):
                pass

        elif sent_type == 'RMC':
            try:
                status = fields[2]
                if status == 'A':
                    lat_raw = fields[3]
                    lon_raw = fields[5]
                    if lat_raw and lon_raw:
                        lat = int(lat_raw[:2]) + float(lat_raw[2:]) / 60
                        lon = int(lon_raw[:3]) + float(lon_raw[3:]) / 60
                        elapsed = int(time.time() - start)
                        print(f"\n>>> [{elapsed}s] *** RMC 有效定位! ***")
                        print(f">>> 纬度: {lat:.6f}°  经度: {lon:.6f}°")
                        if fields[7]:
                            print(f">>> 速度: {float(fields[7])*1.852:.1f} km/h")
                        if fields[8]:
                            print(f">>> 航向: {fields[8]}°")
            except (IndexError, ValueError):
                pass

ser.close()
elapsed = time.time() - start
print(f"\n{'='*50}")
print(f"  读取结束 ({elapsed:.0f}秒)")
if fix_time:
    print(f"  首次定位耗时: {fix_time:.0f} 秒")
else:
    print(f"  未能定位。建议：")
    print(f"  1. 确保天线朝向天空，无遮挡")
    print(f"  2. 冷启动可能需要 2-5 分钟")
    print(f"  3. 尝试启用多星座加速搜星")
