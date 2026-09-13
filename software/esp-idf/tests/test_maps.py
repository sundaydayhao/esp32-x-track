"""Verify actual converter output layout and colors with a generated tile."""
import importlib.util
from pathlib import Path
import struct
import tempfile
from PIL import Image

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_maps', root / 'tools/prepare_maps.py')
maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maps)
with tempfile.TemporaryDirectory() as directory:
    source, output = Path(directory) / 'tile.png', Path(directory) / 'tile.bin'
    image = Image.new('RGBA', (256, 256), (255, 0, 0, 255))
    image.putpixel((1, 0), (0, 255, 0, 255))
    image.putpixel((2, 0), (0, 0, 255, 255))
    image.putpixel((3, 0), (0, 0, 0, 0))
    image.save(source)
    maps.convert(source, output)
    content = output.read_bytes()
    assert len(content) == 4 + 256 * 256 * 2
    header, = struct.unpack('<I', content[:4])
    assert header & 31 == 4 and (header >> 10) & 2047 == 256 and header >> 21 == 256
    assert content[4:12] == bytes.fromhex('f80007e0001fffff')
    Image.new('RGB', (512, 256)).save(source)
    try:
        maps.convert(source, output)
    except ValueError:
        pass
    else:
        raise AssertionError('Unexpected tile size must be rejected')
print('PASS tile dimensions, LVGL header, RGB565 byte order and transparency')
