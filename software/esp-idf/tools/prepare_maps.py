"""Convert local 256px XYZ tiles to LVGL 8 RGB565 (byte-swapped) images.

No map provider is contacted. Obtain tiles that permit offline use separately.
Input: z/x/y.png or z/x/y.jpg; output: MAP/z/x/y.bin on the SD card.
"""
import argparse
import json
from pathlib import Path
import struct

from PIL import Image


def convert(source: Path, destination: Path) -> None:
    with Image.open(source) as image:
        if image.size != (256, 256):
            raise ValueError(f'{source}: expected 256x256, got {image.size}')
        # Flatten transparent tiles against white, then produce RGB565 MSB first.
        rgba = image.convert('RGBA')
        rgb = Image.new('RGBA', image.size, 'white')
        rgb.alpha_composite(rgba)
        pixels = bytearray()
        for red, green, blue in rgb.convert('RGB').getdata():
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            pixels.extend(struct.pack('>H', value))
    # lv_img_header_t: cf[4:0], zero[7:5], reserved[9:8], w[20:10], h[31:21].
    content = struct.pack('<I', 4 | (256 << 10) | (256 << 21)) + pixels
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.bin.tmp')
    temporary.write_bytes(content)
    temporary.replace(destination)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True, help='Root containing z/x/y image files')
    parser.add_argument('--output', type=Path, required=True, help='SD card MAP directory')
    parser.add_argument('--coordinates', choices=['wgs84', 'gcj02'], default='wgs84')
    parser.add_argument('--overwrite', action='store_true', help='Replace existing output tiles')
    args = parser.parse_args()
    if not args.input.is_dir():
        parser.error('Input directory does not exist')
    jobs = {}
    for file in sorted(args.input.rglob('*')):
        if not file.is_file() or file.suffix.lower() not in ('.png', '.jpg', '.jpeg'):
            continue
        parts = file.relative_to(args.input).with_suffix('').parts
        if len(parts) != 3 or not all(part.isdecimal() for part in parts):
            raise ValueError(f'{file}: use the z/x/y XYZ directory structure')
        z, x, y = map(int, parts)
        if not (0 <= z <= 19 and 0 <= x < 2**z and 0 <= y < 2**z):
            raise ValueError(f'{file}: tile coordinates out of range (zoom 0..19)')
        target = args.output / str(z) / str(x) / f'{y}.bin'
        if target in jobs:
            raise ValueError(f'Duplicate tile: {file} and {jobs[target]}')
        if target.exists() and not args.overwrite:
            raise FileExistsError(f'{target}: pass --overwrite to replace')
        jobs[target] = file
    if not jobs:
        parser.error('No PNG/JPEG XYZ tiles found')
    for target, source in jobs.items():
        convert(source, target)
    manifest = {'format': 'LVGL8 RGB565 swapped', 'tile_size': 256,
                'coordinates': args.coordinates, 'tiles_converted': len(jobs),
                'zoom_levels': sorted({int(target.parent.parent.name) for target in jobs})}
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    # This template is deliberately separate: copying it to the SD root enables import.
    config = {'sysConfig.mapDirPath': '/MAP', 'sysConfig.mapExtName': 'bin',
              'sysConfig.mapWGS84': args.coordinates == 'wgs84', 'sysConfig.timeZone': 8}
    (args.output / 'XTrackConfig.example.json').write_text(json.dumps(config, indent=2), encoding='utf-8')
    print(f'Converted {len(jobs)} tiles to {args.output}; coordinate system: {args.coordinates}')
    print('Copy XTrackConfig.example.json to the SD root as XTrackConfig.json to import settings.')


if __name__ == '__main__':
    main()
