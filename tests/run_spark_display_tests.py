#!/usr/bin/env python3
"""Test Spark's receiver and LVGL view without building or flashing firmware.

Requires an existing native simulator build for its SDK flags and LVGL objects.
Only the test, Spark sources, and MessagePack library are compiled here.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--native-build', type=Path,
                    default=root / 'simulator/FloatairSimulator/build-macos-llvm')
parser.add_argument('--artifacts', type=Path)
args = parser.parse_args()
build = args.native_build.resolve()
commands = json.loads(subprocess.check_output(['ninja', '-C', str(build), '-t', 'compdb'], text=True))
entry = next(row for row in commands if row['file'] == str(root / 'apps/spark/view.c'))
command = shlex.split(entry['command'])
flags = []
i = 1
while i < len(command):
    if command[i] in ('-o', '-MF', '-MT', '-MQ'):
        i += 2
    elif command[i] in ('-c', '-MD', '-MMD') or command[i] == entry['file']:
        i += 1
    else:
        flags.append(command[i])
        i += 1

objects = build / 'CMakeFiles/floatair_simulator.dir'
lvgl = [object_file for object_file in sorted((objects / 'lvgl/src').rglob('*.o'))
        if '/drivers/sdl/' not in object_file.as_posix()
        and '/draw/sdl/' not in object_file.as_posix()]
if not lvgl:
    parser.error('The configured native build has no LVGL objects.')
if args.artifacts:
    args.artifacts = args.artifacts.resolve()
    args.artifacts.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory(prefix='spark-display-tests-') as temporary:
    temp = Path(temporary)
    sources = [root / file for file in ('tests/spark_display_test.c', 'apps/spark/display.c',
                                       'apps/spark/app.c', 'apps/spark/view.c',
                                       'apps/spark/assistant_avatar.c',
                                       'apps/spark/assistant_listening.c',
                                       'apps/spark/assistant_thinking.c',
                                       'apps/spark/assistant_working.c')]
    sources += sorted((root / 'thirdparty/mpack').glob('*.c'))
    compiled = []
    for index, source in enumerate(sources):
        output = temp / f'{index}.o'
        subprocess.run([command[0], *flags, '-DLV_USE_LODEPNG=1',
                        '-g', '-O1', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-c', str(source), '-o', str(output)], check=True)
        compiled.append(output)
    executable = temp / 'spark_display_test'
    subprocess.run([command[0], '-fsanitize=address,undefined', '-Wl,-dead_strip',
                    *map(str, compiled), *map(str, lvgl),
                    str(objects / 'simulator/FloatairSimulator/simulator_platform.c.o'),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable), *([str(args.artifacts)] if args.artifacts else [])], check=True)
