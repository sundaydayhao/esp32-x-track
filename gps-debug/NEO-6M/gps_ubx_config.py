"""
NEO-M8N 多星座配置脚本
通过 UBX 协议启用 GPS + BeiDou + GLONASS，然后冷启动并读取
"""
import serial
import struct
import time

PORT = 'COM31'
BAUD = 9600


# ── UBX 帧构建 ──────────────────────────────────────────────
def ubx_frame(cls: int, msg_id: int, payload: bytes) -> bytes:
    """构建完整 UBX 帧（含校验和）"""
    length = struct.pack('<H', len(payload))
    frame = b'\xb5\x62' + bytes([cls, msg_id]) + length + payload
    ck_a = ck_b = 0
    for b in bytes([cls, msg_id]) + payload:
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return frame + bytes([ck_a, ck_b])


def send_and_ack(ser: serial.Serial, cls: int, msg_id: int,
                 payload: bytes, desc: str, wait: float = 1.0) -> bool:
    """发送 UBX 命令并等待 ACK/NAK"""
    ser.reset_input_buffer()
    ser.write(ubx_frame(cls, msg_id, payload))
    ser.flush()
    time.sleep(wait)
    resp = ser.read(ser.in_waiting or 0)

    if b'\xb5\x62\x05\x01' in resp:
        print(f"  ✓ {desc}")
        return True
    elif b'\xb5\x62\x05\x00' in resp:
        print(f"   {desc} (NAK)")
        return False
    else:
        # 有些固件不返回 ACK，也当作成功
        print(f"  ? {desc} (无ACK, resp={resp[:30].hex() if resp else 'empty'})")
        return True


# ── 主流程 ──────────────────────────────────────────────────
print("=" * 55)
print("  NEO-M8N 多星座配置 (GPS + BeiDou + GLONASS)")
print("=" * 55)

ser = serial.Serial(PORT, BAUD, timeout=2)
time.sleep(0.5)

# Step 1: 查询当前 GNSS 配置
print("\n[1] 查询当前 GNSS 配置 (CFG-GNSS poll)...")
ser.reset_input_buffer()
ser.write(ubx_frame(0x06, 0x3E, b''))  # poll
time.sleep(1)
resp = ser.read(ser.in_waiting or 0)
if b'\xb5\x62\x06\x3e' in resp:
    # 解析响应
    idx = resp.index(b'\xb5\x62\x06\x3e')
    payload_start = idx + 6  # skip sync + cls + id + len(2)
    # 找 payload 结束位置
    plen = struct.unpack('<H', resp[idx+4:idx+6])[0]
    gpayload = resp[payload_start:payload_start + plen]
    if len(gpayload) >= 4:
        ver = gpayload[0]
        num_trk_hw = gpayload[1]
        num_trk_use = gpayload[2]
        num_blocks = gpayload[3]
        print(f"  版本: {ver}, 硬件通道: {num_trk_hw}, 使用通道: {num_trk_use}, 星座块数: {num_blocks}")
        # 解析每个星座块
        offset = 4
        gnss_names = {0: 'GPS', 1: 'SBAS', 2: 'Galileo', 3: 'BeiDou',
                      4: 'IMES', 5: 'QZSS', 6: 'GLONASS'}
        for i in range(num_blocks):
            if offset + 8 <= len(gpayload):
                gnss_id = gpayload[offset]
                res_trk = gpayload[offset + 1]
                max_trk = gpayload[offset + 2]
                flags = struct.unpack('<I', gpayload[offset+4:offset+8])[0]
                enabled = bool(flags & 0x01)
                name = gnss_names.get(gnss_id, f'Unknown({gnss_id})')
                status = "✓启用" if enabled else "✗禁用"
                print(f"    {name}: {status} (resTrk={res_trk}, maxTrk={max_trk})")
                offset += 8
else:
    print(f"  无响应 (可能固件不支持 poll)")

# Step 2: 配置多星座
# NEO-M8N 典型配置: 32 硬件通道, 16 使用通道
# GPS(0): 8 res, 16 max | BeiDou(3): 16 res, 32 max | GLONASS(6): 8 res, 14 max
print("\n[2] 配置多星座 (CFG-GNSS set)...")

blocks = b''
# GPS: gnssId=0, resTrkCh=8, maxTrkCh=16, enabled
blocks += struct.pack('<BBBB', 0, 8, 16, 0)
blocks += struct.pack('<I', 0x00010001)  # flags: bit0=enable, bit16=L1CA

# BeiDou: gnssId=3, resTrkCh=16, maxTrkCh=32, enabled
blocks += struct.pack('<BBBB', 3, 16, 32, 0)
blocks += struct.pack('<I', 0x00010001)

# GLONASS: gnssId=6, resTrkCh=8, maxTrkCh=14, enabled
blocks += struct.pack('<BBBB', 6, 8, 14, 0)
blocks += struct.pack('<I', 0x00010001)

gnss_payload = struct.pack('<BBBB', 0, 32, 16, 3) + blocks
ok = send_and_ack(ser, 0x06, 0x3E, gnss_payload, "写入多星座配置", wait=2.0)

# Step 3: 保存到 BBR (电池备份RAM) 使配置掉电保持
print("\n[3] 保存配置到 BBR (CFG-CFG)...")
# save: ioPort=0x0001 (UART1), clearMask=0, saveMask=0x00000010 (INF), loadMask=0
save_payload = struct.pack('<III', 0x00000000, 0x00000010, 0x00000000)
send_and_ack(ser, 0x06, 0x09, save_payload, "保存配置", wait=1.0)

# Step 4: 热启动 (保留星历)
print("\n[4] 热启动复位 (CFG-RST, hot start)...")
# navBbrMask=0x0000 (保留BBR), resetMode=0x01 (controlled software reset)
reset_payload = struct.pack('<HBB', 0x0000, 0x00, 0x01)
send_and_ack(ser, 0x06, 0x04, reset_payload, "热启动复位", wait=0.5)

print("\n  等待模块重启并搜星...")
time.sleep(8)

# Step 5: 读取 GPS 数据 45 秒
print("\n[5] 读取 GPS 数据 (45秒)...\n")
start = time.time()
buffer = ''
fix_time = None
best_sats = 0
last_gsv_sats = -1

while time.time() - start < 45:
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
        talker = fields[0][:-3]  # GP / GN / GB / GL ...
        elapsed = int(time.time() - start)

        if sent_type == 'GSV':
            try:
                total_sats = int(fields[3]) if fields[3] else 0
                msg_num = int(fields[2]) if fields[2] else 0
                if msg_num == 1 and total_sats != last_gsv_sats:
                    last_gsv_sats = total_sats
                    sats_detail = []
                    for i in range(4, min(len(fields), 20), 4):
                        if i + 3 < len(fields) and fields[i]:
                            sats_detail.append(f"{talker}PRN{fields[i]}(SNR{fields[i+3]})")
                    print(f"[{elapsed:3d}s] 可见{total_sats}颗: {' '.join(sats_detail[:4])}")
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
                    lat = int(fields[2][:2]) + float(fields[2][2:]) / 60
                    lon = int(fields[4][:3]) + float(fields[4][3:]) / 60
                    if fields[3] == 'S':
                        lat = -lat
                    if fields[5] == 'W':
                        lon = -lon
                    print(f"\n{'!'*55}")
                    print(f"  >>> [{elapsed}s] *** 定位成功! ***")
                    print(f"  >>> 纬度: {lat:.6f}°  经度: {lon:.6f}°")
                    print(f"  >>> 卫星: {sats}颗  海拔: {fields[9]}m")
                    print(f"  >>> 质量: {fix_q}  HDOP: {fields[8]}")
                    print(f"  >>> 百度地图:")
                    print(f"  >>> https://api.map.baidu.com/marker?location={lat:.6f},{lon:.6f}&output=html")
                    print(f"{'!'*55}")
            except (IndexError, ValueError):
                pass

        elif sent_type == 'RMC':
            try:
                if fields[2] == 'A' and fix_time is None:
                    fix_time = time.time() - start
                    lat = int(fields[3][:2]) + float(fields[3][2:]) / 60
                    lon = int(fields[5][:3]) + float(fields[5][3:]) / 60
                    if fields[4] == 'S':
                        lat = -lat
                    if fields[6] == 'W':
                        lon = -lon
                    print(f"\n{'!'*55}")
                    print(f"  >>> [{elapsed}s] *** RMC 有效定位! ***")
                    print(f"  >>> 纬度: {lat:.6f}°  经度: {lon:.6f}°")
                    print(f"{'!'*55}")
            except (IndexError, ValueError):
                pass

ser.close()
elapsed = time.time() - start
print(f"\n{'='*55}")
print(f"  读取结束 ({elapsed:.0f}秒)")
print(f"  最多搜到: {best_sats} 颗卫星")
if fix_time:
    print(f"  首次定位耗时: {fix_time:.0f} 秒")
else:
    print(f"  仍未定位。建议：")
    print(f"  1. 移到室外开阔地（窗边可能仍有钢筋混凝土遮挡）")
    print(f"  2. 检查 GPS 天线是否接好（IPEX/U.FL 接口）")
    print(f"  3. 天线是否朝上放置")
