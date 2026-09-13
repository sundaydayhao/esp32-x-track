"""Compile and run the same navigation/GPX sources used by the ESP32 firmware."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'test-output'
out.mkdir(exist_ok=True)
core = root / 'components' / 'xtrack' / 'core'
compiler = shutil.which(args.cxx) or args.cxx
env = os.environ.copy()
env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
exe = out / ('core_tests.exe' if os.name == 'nt' else 'core_tests')
subprocess.run([compiler, '-std=gnu++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(core), str(root / 'tests/core_tests.cpp'),
                str(core / 'navigation.cpp'), str(core / 'gpx_writer.cpp'),
                '-o', str(exe)], check=True, env=env)
subprocess.run([str(exe), str(out)], check=True, env=env)
ns = {'g': 'http://www.topografix.com/GPX/1/1'}
for file, count, segments in [('normal.gpx', 2, 2), ('interrupted.gpx', 1, 1)]:
    tree = ET.parse(out / file)
    points = tree.findall('.//g:trkpt', ns)
    assert len(points) == count
    assert len(tree.findall('.//g:trkseg', ns)) == segments
    for point in points:
        assert float(point.attrib['lat']) == 31.2
        assert float(point.attrib['lon']) == 121.5
        assert point.find('g:time', ns).text == '2026-09-12T02:03:04Z'
print('PASS GPX XML, coordinate order, UTC timestamps and recovery validation')
