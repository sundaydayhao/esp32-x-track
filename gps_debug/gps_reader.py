"""
野火 GPS 开发板 (NEO-M8N-0-001) NMEA 数据读取与解析
"""
import serial
import sys
import time
import struct
from datetime import datetime


def parse_nmea_checksum(sentence: str) -> bool:
    """验证 NMEA 校验和"""
    if '*' not in sentence:
        return False
    body, chk = sentence.rsplit('*', 1)
    body = body.lstrip('$')
    calc = 0
    for c in body:
        calc ^= ord(c)
    try:
        return calc == int(chk, 16)
    except ValueError:
        return False


def nmea_to_degrees(raw: str, hemisphere: str) -> float:
    """将 NMEA 纬度/经度 (ddmm.mmmm) 转换为十进制度数"""
    if not raw:
        return 0.0
    dot = raw.index('.')
    deg = int(raw[:dot - 2])
    minutes = float(raw[dot - 2:])
    result = deg + minutes / 60.0
    if hemisphere in ('S', 'W'):
        result = -result
    return result


def parse_gga(fields: list) -> dict:
    """解析 GGA 语句 - 核心定位数据"""
    info = {'type': 'GGA', 'fix_quality': 0}
    try:
        info['utc_time'] = fields[1][:6] if len(fields[1]) >= 6 else fields[1]
        info['latitude'] = nmea_to_degrees(fields[2], fields[3]) if fields[2] else None
        info['longitude'] = nmea_to_degrees(fields[4], fields[5]) if fields[4] else None
        info['fix_quality'] = int(fields[6]) if fields[6] else 0
        info['satellites'] = int(fields[7]) if fields[7] else 0
        info['hdop'] = float(fields[8]) if fields[8] else None
        info['altitude'] = float(fields[9]) if fields[9] else None
        info['altitude_unit'] = fields[10] if len(fields) > 10 else 'M'
    except (IndexError, ValueError) as e:
        info['parse_error'] = str(e)
    return info


def parse_rmc(fields: list) -> dict:
    """解析 RMC 语句 - 推荐最小定位数据"""
    info = {'type': 'RMC'}
    try:
        info['utc_time'] = fields[1][:6] if len(fields[1]) >= 6 else fields[1]
        info['status'] = fields[2]  # A=active, V=void
        info['latitude'] = nmea_to_degrees(fields[3], fields[4]) if fields[3] else None
        info['longitude'] = nmea_to_degrees(fields[5], fields[6]) if fields[5] else None
        if fields[7]:
            info['speed_knots'] = float(fields[7])
            info['speed_kmh'] = float(fields[7]) * 1.852
        if fields[8]:
            info['course'] = float(fields[8])
        if len(fields) > 9 and fields[9]:
            date_str = fields[9]
            if len(date_str) == 6:
                info['date'] = f"20{date_str[4:6]}-{date_str[2:4]}-{date_str[0:2]}"
    except (IndexError, ValueError) as e:
        info['parse_error'] = str(e)
    return info


def parse_gsa(fields: list) -> dict:
    """解析 GSA 语句 - DOP 和活跃卫星"""
    info = {'type': 'GSA'}
    try:
        info['mode'] = fields[2]  # A=auto, M=manual
        info['fix_type'] = int(fields[3]) if fields[3] else 0  # 1=no fix, 2=2D, 3=3D
        info['pdop'] = float(fields[15]) if len(fields) > 15 and fields[15] else None
        info['hdop'] = float(fields[16]) if len(fields) > 16 and fields[16] else None
        info['vdop'] = float(fields[17]) if len(fields) > 17 and fields[17] else None
    except (IndexError, ValueError) as e:
        info['parse_error'] = str(e)
    return info


def parse_vtg(fields: list) -> dict:
    """解析 VTG 语句 - 地面速度"""
    info = {'type': 'VTG'}
    try:
        info['course_true'] = float(fields[1]) if fields[1] else None
        info['speed_kmh'] = float(fields[7]) if len(fields) > 7 and fields[7] else None
        info['speed_knots'] = float(fields[5]) if len(fields) > 5 and fields[5] else None
    except (IndexError, ValueError) as e:
        info['parse_error'] = str(e)
    return info


def parse_gll(fields: list) -> dict:
    """解析 GLL 语句 - 地理位置"""
    info = {'type': 'GLL'}
    try:
        info['latitude'] = nmea_to_degrees(fields[1], fields[2]) if fields[1] else None
        info['longitude'] = nmea_to_degrees(fields[3], fields[4]) if fields[3] else None
        info['utc_time'] = fields[5][:6] if len(fields[5]) >= 6 else fields[5]
        info['status'] = fields[6] if len(fields) > 6 else ''
    except (IndexError, ValueError) as e:
        info['parse_error'] = str(e)
    return info


PARSERS = {
    'GGA': parse_gga,
    'RMC': parse_rmc,
    'GSA': parse_gsa,
    'VTG': parse_vtg,
    'GLL': parse_gll,
}

FIX_QUALITY = {
    0: '无效',
    1: 'GPS 单点定位',
    2: '差分 GPS (DGPS)',
    4: 'RTK 固定解',
    5: 'RTK 浮点解',
    6: '估算',
}


def format_utc(time_str: str) -> str:
    """格式化 UTC 时间"""
    if len(time_str) >= 6:
        h, m, s = time_str[:2], time_str[2:4], time_str[4:]
        return f"{h}:{m}:{s}"
    return time_str


def print_parsed(data: dict):
    """格式化打印解析结果"""
    t = data.get('type', '?')
    if t == 'GGA':
        fix = FIX_QUALITY.get(data.get('fix_quality', 0), '未知')
        print(f"\n{'='*50}")
        print(f"  [GGA] 核心定位数据")
        print(f"{'='*50}")
        print(f"  UTC 时间:   {format_utc(data.get('utc_time', ''))}")
        if data.get('latitude') is not None:
            print(f"  纬度:       {data['latitude']:.6f}°")
            print(f"  经度:       {data['longitude']:.6f}°")
        else:
            print(f"  位置:       未定位")
        print(f"  定位质量:   {fix}")
        print(f"  使用卫星:   {data.get('satellites', 0)} 颗")
        if data.get('hdop'):
            print(f"  HDOP:       {data['hdop']}")
        if data.get('altitude') is not None:
            print(f"  海拔:       {data['altitude']} {data.get('altitude_unit', 'M')}")

    elif t == 'RMC':
        status = '有效定位' if data.get('status') == 'A' else '未定位'
        print(f"\n{'='*50}")
        print(f"  [RMC] 推荐最小定位数据")
        print(f"{'='*50}")
        print(f"  UTC 时间:   {format_utc(data.get('utc_time', ''))}")
        print(f"  日期:       {data.get('date', 'N/A')}")
        print(f"  状态:       {status}")
        if data.get('latitude') is not None:
            print(f"  纬度:       {data['latitude']:.6f}°")
            print(f"  经度:       {data['longitude']:.6f}°")
        if data.get('speed_kmh') is not None:
            print(f"  速度:       {data['speed_kmh']:.1f} km/h ({data.get('speed_knots', 0):.1f} knots)")
        if data.get('course') is not None:
            print(f"  航向:       {data['course']:.1f}°")

    elif t == 'GSA':
        fix_map = {1: '未定位', 2: '2D 定位', 3: '3D 定位'}
        print(f"\n  [GSA] DOP 精度因子: fix={fix_map.get(data.get('fix_type', 0), '?')}"
              f"  PDOP={data.get('pdop', 'N/A')}"
              f"  HDOP={data.get('hdop', 'N/A')}"
              f"  VDOP={data.get('vdop', 'N/A')}")

    elif t == 'VTG':
        print(f"\n  [VTG] 地面速度: {data.get('speed_kmh', 'N/A')} km/h"
              f"  航向: {data.get('course_true', 'N/A')}°")

    elif t == 'GLL':
        print(f"\n  [GLL] 地理位置: lat={data.get('latitude', 'N/A')}"
              f"  lon={data.get('longitude', 'N/A')}"
              f"  time={format_utc(data.get('utc_time', ''))}")


def read_gps(port: str, baudrate: int = 9600, duration: int = 30):
    """读取并解析 GPS 数据"""
    print(f"正在打开 {port} @ {baudrate} baud ...")
    try:
        ser = serial.Serial(port, baudrate, timeout=2)
    except serial.SerialException as e:
        print(f"无法打开 {port}: {e}")
        return False

    print(f"已打开，等待 GPS 数据（{duration}秒）...\n")
    start = time.time()
    line_count = 0
    parsed_count = 0
    last_gga = None
    last_rmc = None
    buffer = ''

    try:
        while time.time() - start < duration:
            try:
                raw = ser.read(ser.in_waiting or 1)
                if not raw:
                    continue
                buffer += raw.decode('ascii', errors='ignore')
            except serial.SerialException as e:
                print(f"读取错误: {e}")
                break

            while '\r\n' in buffer:
                line, buffer = buffer.split('\r\n', 1)
                line = line.strip()
                if not line.startswith('$'):
                    continue
                line_count += 1

                # 去掉校验和后的语句体
                sentence = line
                if '*' in sentence:
                    body = sentence[1:sentence.rindex('*')]
                else:
                    body = sentence[1:]

                fields = body.split(',')
                talker_sent = fields[0]  # e.g. GNGGA, GPRMC, etc.
                # 取后3位作为语句类型
                sent_type = talker_sent[-3:]

                if sent_type in PARSERS:
                    parsed = PARSERS[sent_type](fields)
                    parsed_count += 1
                    print_parsed(parsed)

                    if sent_type == 'GGA':
                        last_gga = parsed
                    elif sent_type == 'RMC':
                        last_rmc = parsed

            # 每 10 秒打印一次原始数据采样
            elapsed = time.time() - start
            if line_count > 0 and int(elapsed) % 10 == 0 and line_count % 20 < 5:
                pass  # 避免过多输出

    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        ser.close()

    # 最终摘要
    print(f"\n{'#'*50}")
    print(f"  读取摘要")
    print(f"{'#'*50}")
    print(f"  总 NMEA 语句数: {line_count}")
    print(f"  已解析语句数:   {parsed_count}")
    print(f"  运行时长:       {time.time() - start:.1f} 秒")

    if last_gga:
        print(f"\n  --- 最新 GGA 定位 ---")
        if last_gga.get('latitude') is not None:
            print(f"  纬度:  {last_gga['latitude']:.6f}°")
            print(f"  经度:  {last_gga['longitude']:.6f}°")
            print(f"  卫星:  {last_gga.get('satellites', 0)} 颗")
            print(f"  海拔:  {last_gga.get('altitude', 'N/A')} m")
            fix = FIX_QUALITY.get(last_gga.get('fix_quality', 0), '未知')
            print(f"  质量:  {fix}")
        else:
            print(f"  状态:  未定位（卫星数: {last_gga.get('satellites', 0)}）")

    if last_rmc and last_rmc.get('latitude') is not None:
        print(f"\n  --- 百度地图链接 ---")
        print(f"  https://api.map.baidu.com/marker?location={last_rmc['latitude']:.6f},{last_rmc['longitude']:.6f}&output=html")

    return line_count > 0


def try_ports():
    """尝试两个 COM 口，找到有 GPS 数据的那个"""
    ports = ['COM20', 'COM31']
    for port in ports:
        print(f"\n>>> 尝试 {port} ...")
        try:
            ser = serial.Serial(port, 9600, timeout=3)
            time.sleep(1)
            # 读取一小段看有没有 NMEA 数据
            data = b''
            read_start = time.time()
            while time.time() - read_start < 3:
                chunk = ser.read(ser.in_waiting or 1)
                if chunk:
                    data += chunk
                if b'$G' in data:
                    ser.close()
                    print(f"  ✓ {port} 检测到 NMEA 数据！")
                    return port
            ser.close()
            if data:
                preview = data[:100]
                print(f"  ✗ {port} 有数据但非 NMEA: {preview[:60]}")
            else:
                print(f"  ✗ {port} 无数据")
        except serial.SerialException as e:
            print(f"  ✗ {port} 无法打开: {e}")
    return None


if __name__ == '__main__':
    print("=" * 50)
    print("  野火 GPS 开发板 (NEO-M8N-0-001) 数据读取")
    print("=" * 50)

    # 先探测哪个 COM 口有 GPS 数据
    gps_port = try_ports()

    if gps_port:
        read_gps(gps_port, baudrate=9600, duration=20)
    else:
        print("\n未在默认端口检测到 GPS 数据。")
        print("尝试直接读取 COM31（USB 串行设备）...")
        read_gps('COM31', baudrate=9600, duration=20)
