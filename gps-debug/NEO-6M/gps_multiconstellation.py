"""通过 UBX 命令启用多星座 + 冷启动，然后读取 GPS"""
import serial
import struct
import time

port = 'COM31'
baudrate = 9600

def ubx_checksum(cls, id_, payload):
    """计算 UBX 校验和 (CK_A, CK_B)"""
    ck_a = ck_b = 0
    for b in [cls, id_]:
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    for b in payload:
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return bytes([ck_a, ck_b])

def send_ubx(ser, cls, id_, payload, desc):
    """发送 UBX 命令"""
    length = struct.pack('<H', len(payload))
    frame = b'\xb5\x62' + bytes([cls, id_]) + length + payload
    ck = ubx_checksum(cls, id_, payload)
    frame += ck
    ser.write(frame)
    ser.flush()
    time.sleep(0.5)
    # 读取 ACK
    resp = ser.read(ser.in_waiting or 0)
    if b'\xb5\x62\x05\x01' in resp:  # UBX-ACK-ACK
        print(f"  ✓ {desc} - 已确认")
    elif b'\xb5\x62\x05\x00' in resp:  # UBX-ACK-NAK
        print(f"  ✗ {desc} - 被拒绝")
    else:
        print(f"  ? {desc} - 无响应 (resp={resp[:20].hex()})")
    return resp

print("=" * 50)
print("  NEO-M8N 多星座配置 + 冷启动")
print("=" * 50)

ser = serial.Serial(port, baudrate, timeout=2)

# 1. UBX-CFG-RST: 冷启动复位 (navBbrMask=0xFFFF, resetMode=0x02=hardware)
print("\n[1] 发送冷启动复位...")
reset_payload = struct.pack('<HBB', 0xFFFF, 0x00, 0x02)
send_ubx(ser, 0x06, 0x04, reset_payload, "冷启动复位 (CFG-RST)")

# 等待模块重启
print("  等待模块重启 (5秒)...")
time.sleep(5)

# 2. UBX-CFG-GNSS: 启用多星座
# NEO-M8N GNSS 配置 (msgVer=0, numTrkChHw=32, numTrkChUse=16, numConfigBlocks=3)
# Block: gps(0), glonass(6), beidou(3)
print("\n[2] 启用多星座 (GPS + GLONASS + BeiDou)...")

# 构建 GNSS 配置块
# 每个块 8 字节: gnssId, resTrkCh, maxTrkCh, reserved, flags
gnss_blocks = b''

# GPS (gnssId=0): 启用, 8通道跟踪, 16最大
gnss_blocks += struct.pack('<BBBB', 0, 8, 16, 0)  # gnssId, resTrkCh, maxTrkCh, reserved
gnss_blocks += struct.pack('<I', 0x00010001)  # flags: enable + L1CA

# GLONASS (gnssId=6): 启用
gnss_blocks += struct.pack('<BBBB', 6, 8, 14, 0)
gnss_blocks += struct.pack('<I', 0x00010001)

# BeiDou (gnssId=3): 启用
gnss_blocks += struct.pack('<BBBB', 3, 16, 32, 0)
gnss_blocks += struct.pack('<I', 0x00010001)

gnss_payload = struct.pack('<BBBB', 0, 32, 16, 3) + gnss_blocks
send_ubx(ser, 0x06, 0x3E, gnss_payload, "多星座配置 (CFG-GNSS)")

# 3. 再发一次冷启动让新配置生效
print("\n[3] 再次冷启动使配置生效...")
send_ubx(ser, 0x06, 0x04, reset_payload, "冷启动复位 (CFG-RST)")
print("  等待模块重启并搜星 (10秒)...")
time.sleep(10)

# 4. 读取 GPS 数据 60 秒
print("\n[4] 开始读取 GPS 数据 (60秒)...\n")
start = time.time()
buffer = ''
fix_time = None
best_sats = 0

while time.time() - start < 60:
    raw = ser.read(ser.in_waiting or 1)
    if not raw:
        continue
    buffer += raw.decode('ascii', errors='ignore')

    while '\r\n' in buffer:
        line, buffer = buffer.split('\r\n', 1)
        line = line.strip()
        if not line.startswith('$') or '*' not in line:
            continue

        body = line[1:line.rindex('*')]
        fields = body.split(',')
        sent_type = fields[0][-3:]
        elapsed = int(time.time() - start)

        if sent_type == 'GSV':
            try:
                total_sats = int(fields[3]) if fields[3] else 0
                msg_num = int(fields[2]) if fields[2] else 0
                num_msgs = int(fields[1]) if fields[1] else 0
                if msg_num == 1 and total_sats > 0:
                    sats_info = []
                    for i in range(4, len(fields), 4):
                        if i + 3 < len(fields) and fields[i]:
                            sats_info.append(f"PRN{fields[i]}(SNR{fields[i+3]}dB)")
                    print(f"[{elapsed:3d}s] 可见{total_sats}颗: {' '.join(sats_info[:6])}")
            except (IndexError, ValueError):
                pass

        elif sent_type == 'GGA':
            try:
                fix_q = int(fields[6]) if fields[6] else 0
                sats = int(fields[7]) if fields[7] else 0
                if sats > best_sats:
                    best_sats = sats
                if fix_q > 0 and fix_time is None:
                    fix_time = time.time() - start
                    lat_raw, lon_raw = fields[2], fields[4]
                    lat = int(lat_raw[:2]) + float(lat_raw[2:]) / 60
                    lon = int(lon_raw[:3]) + float(lon_raw[3:]) / 60
                    print(f"\n>>> [{elapsed}s] *** 定位成功! ***")
                    print(f">>> 纬度: {lat:.6f}°  经度: {lon:.6f}°")
                    print(f">>> 卫星: {sats}颗  海拔: {fields[9]}m")
                    print(f">>> 百度地图: https://api.map.baidu.com/marker?location={lat:.6f},{lon:.6f}&output=html")
                elif sats > 0 and elapsed % 10 == 0:
                    print(f"[{elapsed:3d}s] GGA: 搜到{sats}颗，尚未定位")
            except (IndexError, ValueError):
                pass

        elif sent_type == 'RMC':
            try:
                if fields[2] == 'A':
                    lat_raw, lon_raw = fields[3], fields[5]
                    lat = int(lat_raw[:2]) + float(lat_raw[2:]) / 60
                    lon = int(lon_raw[:3]) + float(lon_raw[3:]) / 60
                    if fix_time is None:
                        fix_time = time.time() - start
                        print(f"\n>>> [{elapsed}s] *** RMC 有效定位! ***")
                        print(f">>> 纬度: {lat:.6f}°  经度: {lon:.6f}°")
            except (IndexError, ValueError):
                pass

ser.close()
elapsed = time.time() - start
print(f"\n{'='*50}")
print(f"  读取结束 ({elapsed:.0f}秒)")
print(f"  最多搜到: {best_sats} 颗卫星")
if fix_time:
    print(f"  首次定位耗时: {fix_time:.0f} 秒")
else:
    print(f"  仍未定位。可能原因：")
    print(f"  - 窗边仍有遮挡（钢筋混凝土墙体）")
    print(f"  - 天线未正确连接或损坏")
    print(f"  - 建议移到室外开阔地测试")
