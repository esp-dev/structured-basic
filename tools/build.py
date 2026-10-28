"""Dependency-free, reproducible host tests and complete AVR firmware build.
All generated files stay in build/. Invoke with Python 3.8 or newer.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build'
OUT.mkdir(exist_ok=True)
REPORT = {'commands': [], 'host': {}, 'sanitizers': {}, 'avr': {}}
parser = argparse.ArgumentParser()
parser.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
parser.add_argument('--avr-bin', type=Path)
parser.add_argument('--host-only', action='store_true')
args = parser.parse_args()
cc = shutil.which(args.cc) or args.cc
env = dict(os.environ)
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')


def run(command, required=True, extra_env=None):
    command = [str(x) for x in command]
    active_env = dict(env)
    active_env.update(extra_env or {})
    p = subprocess.run(command, cwd=ROOT, env=active_env, capture_output=True,
                       text=True, errors='replace', timeout=180)
    REPORT['commands'].append({'command': command, 'exit': p.returncode,
                               'stdout': p.stdout, 'stderr': p.stderr})
    if required and p.returncode:
        print(p.stdout + p.stderr)
        raise RuntimeError('Command failed: ' + ' '.join(command))
    return p


try:
    REPORT['host']['compiler'] = run([cc, '--version']).stdout.splitlines()[0]
    REPORT['host']['python'] = sys.version
    exe_suffix = '.exe' if os.name == 'nt' else ''
    common = ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow', '-Werror', '-Iinclude']
    core = ['src/compiler.c', 'src/vm.c']
    for profile, flags in [('c99-release', ['-std=c99', '-O2']),
                           ('c11-debug', ['-std=c11', '-O0', '-g'])]:
        folder = OUT / profile
        folder.mkdir(exist_ok=True)
        host = folder / ('sbasic' + exe_suffix)
        api = folder / ('api_tests' + exe_suffix)
        run([cc] + flags + common + core + ['platform/host.c', '-o', host])
        run([cc] + flags + common + core + ['tests/api.c', '-o', api])
        api_result = run([api]).stdout.strip()
        report_path = folder / 'acceptance.json'
        acceptance = run([sys.executable, 'tests/acceptance.py', host],
                         extra_env={'SB_TEST_REPORT': str(report_path)}).stdout.strip()
        REPORT['host'][profile] = {'api': api_result, 'acceptance': acceptance}
        print(profile + ': ' + acceptance)
    for sanitizer in ['address', 'undefined']:
        binary = OUT / ('api_' + sanitizer + exe_suffix)
        result = run([cc, '-std=c99', '-O1', '-g', '-fno-omit-frame-pointer',
                      '-fsanitize='+sanitizer] + common + core + ['tests/api.c', '-o', binary], required=False)
        if result.returncode:
            REPORT['sanitizers'][sanitizer] = {'status': 'UNAVAILABLE_BUILD', 'reason': result.stderr}
            print(sanitizer + ': unavailable (see report.json)')
        else:
            result = run([binary], extra_env={'ASAN_OPTIONS': 'halt_on_error=1',
                                             'UBSAN_OPTIONS': 'halt_on_error=1'})
            # Also run the black-box language suite under each available sanitizer.
            host = OUT / ('sbasic_' + sanitizer + exe_suffix)
            run([cc, '-std=c99', '-O1', '-g', '-fno-omit-frame-pointer',
                 '-fsanitize='+sanitizer] + common + core + ['platform/host.c', '-o', host])
            language = run([sys.executable, 'tests/acceptance.py', host],
                           extra_env={'ASAN_OPTIONS': 'halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
            REPORT['sanitizers'][sanitizer] = {'status': 'PASS', 'api': result.stdout,
                                              'acceptance': language.stdout}
    if not args.host_only:
        avr_bin = args.avr_bin
        if avr_bin is None and shutil.which('avr-gcc'):
            avr_bin = Path(shutil.which('avr-gcc')).parent
        if avr_bin is None:
            raise RuntimeError('AVR compiler not found: supply --avr-bin, or use --host-only')
        avrcc = avr_bin / ('avr-gcc'+exe_suffix)
        folder = OUT / 'avr'
        folder.mkdir(exist_ok=True)
        REPORT['avr']['compiler'] = run([avrcc, '--version']).stdout.splitlines()[0]
        objects = []
        for source in core + ['platform/avr.c']:
            obj = folder / (Path(source).stem + '.o')
            objects.append(obj)
            run([avrcc, '-std=c99', '-Os', '-mmcu=atmega1284p', '-DF_CPU=16000000UL',
                 '-fno-common', '-ffunction-sections', '-fdata-sections', '-fstack-usage'] +
                common + ['-c', source, '-o', obj])
        elf = folder / 'firmware.elf'
        run([avrcc, '-mmcu=atmega1284p'] + objects +
            ['-Wl,--gc-sections,-Map='+str(folder / 'firmware.map'), '-o', elf])
        run([avr_bin / ('avr-objcopy'+exe_suffix), '-O', 'ihex', '-R', '.eeprom', elf, folder / 'firmware.hex'])
        size = run([avr_bin / ('avr-size'+exe_suffix), '-A', elf]).stdout
        (folder / 'size.txt').write_text(size)
        for name, options, filename in [('avr-nm', ['-S', '--size-sort'], 'symbols.txt'),
                                        ('avr-objdump', ['-d'], 'disassembly.txt')]:
            result = run([avr_bin / (name+exe_suffix)] + options + [elf]).stdout
            (folder / filename).write_text(result)
        sections = {name: int(size_) for name, size_ in re.findall(r'^(\.\S+)\s+(\d+)\s+', size, re.M)}
        data = sections.get('.data', 0)
        bss = sections.get('.bss', 0)
        REPORT['avr'].update({'sections': sections, 'static_sram': data+bss,
                              'sram_before_c_stack': 16384-data-bss,
                              'flash_load': sections.get('.text', 0)+data})
        if data+bss > 12288:
            raise RuntimeError('AVR static SRAM exceeds 12 KiB')
        if sections.get('.text', 0)+data > 131072:
            raise RuntimeError('AVR flash exceeds 128 KiB')
        print('AVR: .text={0}, .data={1}, .bss={2}, SRAM remaining={3}'.format(
            sections.get('.text', 0), data, bss, 16384-data-bss))
    REPORT['status'] = 'PASS'
except Exception as error:
    REPORT['status'] = 'FAIL'
    REPORT['error'] = str(error)
    raise
finally:
    (OUT / 'report.json').write_text(json.dumps(REPORT, indent=2)+'\n')
