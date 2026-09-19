"""读取原始 NMEA 数据，包括 GSV 卫星信息"""
import serial
import time

port = 'COM31'
baudrate = 9600
duration = 30

print(f"打开 {port} @ {baudrate}，读取 {duration} 秒原始 NMEA 数据...\n")
ser = serial.Serial(port, baudrate, timeout=2)
start = time.time()
buffer = ''
sentence_counts = {}

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

        # 统计语句类型
        if '*' in line:
            body = line[1:line.rindex('*')]
        else:
            body = line[1:]
        fields = body.split(',')
        sent_type = fields[0][-3:]
        sentence_counts[sent_type] = sentence_counts.get(sent_type, 0) + 1

        # 打印 GSV（卫星信息）和 GGA 原始数据
        if sent_type in ('GSV', 'GGA', 'RMC'):
            print(f"  {line}")

ser.close()

print(f"\n{'='*50}")
print(f"语句类型统计（{duration}秒内）:")
for k, v in sorted(sentence_counts.items(), key=lambda x: -x[1]):
    print(f"  {k}: {v} 条")
print(f"  总计: {sum(sentence_counts.values())} 条")
