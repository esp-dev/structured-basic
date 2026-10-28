"""Record GCC analyzer, CMake builds/CTest and linked no-heap checks."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--avr-bin', type=Path, required=True)
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'build'
out.mkdir(exist_ok=True)
env = dict(os.environ)
env['PATH'] = str(Path(args.cc).parent)+os.pathsep+str(args.avr_bin)+os.pathsep+env.get('PATH', '')
report = {'commands': []}

def run(command):
    command = [str(x) for x in command]
    result = subprocess.run(command, cwd=root, env=env, capture_output=True,
                            text=True, errors='replace', timeout=180)
    report['commands'].append({'command': command, 'exit': result.returncode,
                               'stdout': result.stdout, 'stderr': result.stderr})
    if result.returncode:
        raise RuntimeError(result.stdout+result.stderr)
    return result.stdout

try:
    run(['git', '--version'])
    run(['cmake', '--version'])
    for source in ['compiler', 'vm']:
        run([args.cc, '-std=c99', '-O0', '-fanalyzer', '-Wall', '-Wextra', '-Wpedantic',
             '-Wconversion', '-Wshadow', '-Werror', '-Iinclude', '-c',
             'src/'+source+'.c', '-o', out/(source+'-analysis.o')])
    # This checks allocation symbols in the complete ELF in addition to source.
    suffix = '.exe' if os.name == 'nt' else ''
    symbols = run([args.avr_bin/('avr-nm'+suffix), out/'avr/firmware.elf'])
    if re.search(r'\b(?:malloc|calloc|realloc|free)$', symbols, re.M):
        raise RuntimeError('Unexpected heap allocator in AVR firmware')
    for source in (root/'src').glob('*.c'):
        if re.search(r'\b(?:malloc|calloc|realloc|free)\s*\(', source.read_text()):
            raise RuntimeError('Unexpected allocator call in core source')
    host_build = out/'cmake-host'
    ninja = shutil.which('ninja', path=env['PATH'])
    generator = ['-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM='+ninja] if ninja else []
    run(['cmake', '-S', '.', '-B', host_build] + generator +
        ['-DCMAKE_C_COMPILER='+str(Path(args.cc).resolve()), '-DCMAKE_BUILD_TYPE=Release',
         '-DPython3_EXECUTABLE='+sys.executable])
    run(['cmake', '--build', host_build])
    run(['ctest', '--test-dir', host_build, '--output-on-failure'])
    target_build = out/'cmake-avr'
    run(['cmake', '-S', '.', '-B', target_build]+generator+
        ['-DCMAKE_TOOLCHAIN_FILE='+str(root/'tools/avr-toolchain.cmake')])
    run(['cmake', '--build', target_build])
    report['status'] = 'PASS'
    print('Extra checks PASS: GCC analyzer, linked no-heap check, CMake host/CTest and complete AVR link')
except Exception as error:
    report['status'] = 'FAIL'
    report['error'] = str(error)
    raise
finally:
    (out/'extra-report.json').write_text(json.dumps(report, indent=2)+'\n')
