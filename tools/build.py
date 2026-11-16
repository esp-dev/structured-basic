"""Build, test and measure Structured BASIC 1.0. Python 3.8+, no packages."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/1.0'
OUT.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser()
parser.add_argument('--cc',default=os.environ.get('CC','clang'))
parser.add_argument('--avr-bin',type=Path)
parser.add_argument('--host-only',action='store_true')
parser.add_argument('--coverage',action='store_true')
args=parser.parse_args()
CC=Path(shutil.which(args.cc) or args.cc).resolve()
env=dict(os.environ)
env['PATH']=str(CC.parent)+os.pathsep+env.get('PATH','')
suffix='.exe' if os.name=='nt' else ''
report={'commands':[],'profiles':{},'analysis':{},'avr':{}}
core=sorted((ROOT/'src').glob('*.c'))
common=['-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Werror','-fno-common','-Iinclude','-Iplatform']

def record():
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')

def run(command,required=True,extra_env=None):
    command=list(map(str,command)); active=dict(env); active.update(extra_env or {})
    p=subprocess.run(command,cwd=ROOT,env=active,capture_output=True,text=True,errors='replace',timeout=240)
    report['commands'].append(dict(command=command,exit=p.returncode,stdout=p.stdout,stderr=p.stderr))
    record()
    if required and p.returncode:
        print(p.stdout+p.stderr,flush=True)
        raise RuntimeError('Command failed: '+' '.join(command))
    return p

def binary(folder,name): return folder/(name+suffix)

def compile_objects(folder,flags,compiler=CC,paths=None):
    folder.mkdir(parents=True,exist_ok=True)
    objects=[]
    for source in paths or core:
        obj=folder/(source.stem+'.o'); objects.append(obj)
        run([compiler,*flags,*common,'-c',source,'-o',obj])
    return objects

def host_profile(name,flags,link_flags=(),strip=False):
    print('Building '+name,flush=True)
    folder=OUT/name; objects=compile_objects(folder,flags)
    host_objects=compile_objects(folder,flags,paths=[ROOT/'platform/host.c',ROOT/'platform/mock.c'])
    unstripped=binary(folder,'sbasic-debug' if strip else 'sbasic')
    linked=run([CC,*flags,*objects,*host_objects,*link_flags,'-lm','-o',unstripped])
    (folder/'link.log').write_text(linked.stdout+linked.stderr)
    host=binary(folder,'sbasic')
    if strip:
        shutil.copyfile(unstripped,host)
        run([CC.parent/('llvm-strip'+suffix),'--strip-all',host])
    # The unchanged stage-one harness intentionally uses a wrapping LCG.
    # Only that TU excludes integer-overflow instrumentation; core objects keep it.
    api_flags=[f for f in flags if not f.startswith('-fsanitize=')]
    if any(f.startswith('-fsanitize=') for f in flags): api_flags+=['-fsanitize=address,undefined']
    api_obj=folder/'api.o'
    run([CC,*api_flags,*common,'-c','tests/api.c','-o',api_obj])
    api=binary(folder,'api_tests')
    run([CC,*flags,*objects,api_obj,*link_flags,'-lm','-o',api])
    api_result=run([api]).stdout.strip()
    acceptance=run([sys.executable,'tests/acceptance.py',host],extra_env={'SB_TEST_REPORT':str(folder/'acceptance.json')}).stdout.strip()
    extended=run([sys.executable,'tests/extended.py',host],extra_env={'SB_EXT_REPORT':str(folder/'extended.json')}).stdout.strip()
    stress_obj=folder/'stress.o'; stress_flags=flags[:]
    if any(f.startswith('-fsanitize=') for f in flags): stress_flags+=['-DSB_SANITIZED']
    run([CC,*stress_flags,*common,'-c','tests/stress10.c','-o',stress_obj])
    stress=binary(folder,'stress_tests')
    run([CC,*flags,*objects,folder/'mock.o',stress_obj,*link_flags,'-lm','-o',stress])
    stress_result=run([stress]).stdout.strip()
    report['profiles'][name]=dict(api=api_result,acceptance=acceptance,extended=extended,stress=stress_result,
                                  exe_bytes=host.stat().st_size,unstripped_bytes=unstripped.stat().st_size)
    print(name+': '+acceptance+'; '+extended+'; '+stress_result,flush=True)
    record(); return host

try:
    report['compiler']=run([CC,'--version']).stdout
    report['python']=sys.version
    report['git']=run(['git','--version']).stdout.strip()
    if 'clang' not in report['compiler'].lower(): raise RuntimeError('BASIC 1.0 verification requires Clang; use --cc')
    env['ASAN_OPTIONS']='halt_on_error=1:abort_on_error=1'
    env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    host_profile('debug',['-std=c11','-O0','-g','-DSB_DEBUG'])
    host_profile('sanitized',['-std=c99','-O1','-g','-DSB_DEBUG','-fno-omit-frame-pointer',
                              '-fno-sanitize-recover=all','-fsanitize=address,undefined'])
    host_profile('extended',['-std=c99','-O1','-g','-DSB_DEBUG','-fno-omit-frame-pointer',
                             '-fno-sanitize-recover=all','-fsanitize=address,undefined,integer,local-bounds'])
    release_flags=['-std=c99','-Os','-g','-DNDEBUG','-flto','-ffunction-sections','-fdata-sections']
    release_link=['-fuse-ld=lld','-Wl,--gc-sections,--verbose,-Map='+str(OUT/'release/link.map')]
    release=host_profile('release',release_flags,release_link,strip=True)
    folder=OUT/'baseline'; folder.mkdir(exist_ok=True)
    baseline=binary(folder,'sbasic')
    run([CC,'-std=c99','-Os','-g','-DNDEBUG',*common,*core,'platform/host.c','platform/mock.c',
         '-Wl,--no-gc-sections','-lm','-o',baseline])
    report['baseline']=dict(exe_bytes=baseline.stat().st_size,
                            acceptance=run([sys.executable,'tests/acceptance.py',baseline]).stdout.strip(),
                            extended=run([sys.executable,'tests/extended.py',baseline]).stdout.strip())
    # Isolate linker GC from LTO and stripping with the SAME non-LTO objects.
    folder=OUT/'gc-experiment'; flags=['-std=c99','-Os','-g','-ffunction-sections','-fdata-sections']
    objects=compile_objects(folder,flags,paths=core+[ROOT/'platform/host.c',ROOT/'platform/mock.c'])
    report['gc_experiment']={}
    for gc in ['gc-sections','no-gc-sections']:
        exe=binary(folder,gc)
        link=run([CC,*flags,*objects,'-fuse-ld=lld','-Wl,--'+gc+',--verbose,-Map='+str(folder/(gc+'.map')),'-lm','-o',exe])
        (folder/(gc+'.log')).write_text(link.stdout+link.stderr)
        sizes=run([CC.parent/('llvm-size'+suffix),'-A',exe]).stdout
        (folder/(gc+'.sections')).write_text(sizes)
        report['gc_experiment'][gc]=dict(exe_bytes=exe.stat().st_size,sections=sizes,
            discarded=[x for x in (link.stdout+link.stderr).splitlines() if 'Discarded' in x or 'Removed' in x])
    if os.name=='nt':
        imports=run([CC.parent/'llvm-readobj.exe','--file-headers','--coff-imports',release]).stdout
        (OUT/'release/imports.txt').write_text(imports)
        report['dlls']=re.findall(r'^\s*Name: (\S+\.dll)',imports,re.M|re.I)
        assert 'IMAGE_FILE_MACHINE_AMD64' in imports
        clean_path=str(Path(os.environ['SystemRoot'])/'System32')+';'+os.environ['SystemRoot']
        standalone=run([release,'examples/acceptance.bas'],extra_env={'PATH':clean_path})
        expected=run([baseline,'examples/acceptance.bas']).stdout
        assert standalone.stdout==expected
        report['standalone']='PASS: native x86-64 stripped EXE, PATH contains Windows directories only'
    analysis=OUT/'analysis'; analysis.mkdir(exist_ok=True)
    for source in core+[ROOT/'platform/host.c',ROOT/'platform/mock.c']:
        result=run([CC,'--analyze','-std=c99',*common,'-Xanalyzer','-analyzer-output=text',source,
                    '-o',analysis/(source.stem+'.txt')])
        if 'warning:' in result.stderr: raise RuntimeError('Analyzer diagnostics need review: '+result.stderr)
    report['analysis']['clang']='PASS: core and Windows adapters, no diagnostics'
    for tool in ['clang-tidy','cppcheck']:
        found=shutil.which(tool,path=env['PATH'])
        if not found: report['analysis'][tool]='UNAVAILABLE'
        elif tool=='cppcheck':
            run([found,'--enable=warning,style,performance,portability','--error-exitcode=1','--std=c99','-Iinclude','src','platform/host.c','platform/mock.c'])
            report['analysis'][tool]='PASS'
        else:
            for source in core: run([found,source,'--','-std=c99','-Iinclude'])
            report['analysis'][tool]='PASS'
    attempted=run([CC,'-std=c99','-O1','-g','-fsanitize=fuzzer,address,undefined','-Iinclude',*core,
                   'tests/fuzzer.c','-lm','-o',binary(OUT,'fuzzer')],required=False)
    if attempted.returncode:
        report['libfuzzer']=dict(status='UNAVAILABLE',reason=attempted.stderr)
    else:
        corpus=OUT/'corpus'; corpus.mkdir(exist_ok=True)
        for path in (ROOT/'examples').glob('*.bas'): shutil.copyfile(path,corpus/path.name)
        report['libfuzzer']=dict(status='PASS',output=run([binary(OUT,'fuzzer'),'-runs=50000','-seed=12345',corpus]).stderr)
    if args.coverage:
        folder=OUT/'coverage'; raw=folder/'raw'; raw.mkdir(parents=True,exist_ok=True)
        flags=['-std=c99','-O0','-g','-fprofile-instr-generate','-fcoverage-mapping']
        objects=compile_objects(folder,flags)
        for name,paths in [('sbasic',['platform/host.c','platform/mock.c']),('stress',['platform/mock.c','tests/stress10.c'])]:
            run([CC,*flags,*common,*objects,*paths,'-lm','-o',binary(folder,name)])
        profile_env={'LLVM_PROFILE_FILE':str(raw/'%p.profraw')}
        run([sys.executable,'tests/acceptance.py',binary(folder,'sbasic')],extra_env=profile_env)
        run([sys.executable,'tests/extended.py',binary(folder,'sbasic')],extra_env=profile_env)
        run([binary(folder,'stress')],extra_env=profile_env)
        listing=folder/'inputs.txt'; listing.write_text('\n'.join(str(x) for x in raw.glob('*.profraw'))+'\n')
        data=folder/'combined.profdata'
        run([CC.parent/('llvm-profdata'+suffix),'merge','--sparse','--input-files='+str(listing),'-o',data])
        coverage=run([CC.parent/('llvm-cov'+suffix),'report',binary(folder,'sbasic'),'--instr-profile='+str(data)]).stdout
        (folder/'report.txt').write_text(coverage); report['coverage']=coverage
    if not args.host_only:
        avr=args.avr_bin
        if avr is None:
            located=shutil.which('avr-gcc')
            if located: avr=Path(located).parent
            elif os.name=='nt':
                candidates=list((Path(os.environ['LOCALAPPDATA'])/'Arduino15/packages/arduino/tools/avr-gcc').glob('*/bin/avr-gcc.exe'))
                if candidates: avr=candidates[-1].parent
        if avr is None: raise RuntimeError('AVR compiler not found; supply --avr-bin')
        report['avr']['compiler']=run([avr/('avr-gcc'+suffix),'--version']).stdout
        folder=OUT/'avr'
        objects=compile_objects(folder,['-std=c99','-Os','-mmcu=atmega1284p','-DF_CPU=16000000UL',
                                        '-ffunction-sections','-fdata-sections','-fstack-usage'],
                                compiler=avr/('avr-gcc'+suffix),paths=core+[ROOT/'platform/avr.c'])
        elf=folder/'firmware.elf'
        run([avr/('avr-gcc'+suffix),'-mmcu=atmega1284p',*objects,'-Wl,--gc-sections,-Map='+str(folder/'firmware.map'),
             '-Wl,-u,vfprintf','-lprintf_flt','-lm','-o',elf])
        run([avr/('avr-objcopy'+suffix),'-O','ihex','-R','.eeprom',elf,folder/'firmware.hex'])
        size=run([avr/('avr-size'+suffix),'-A',elf]).stdout
        (folder/'size.txt').write_text(size)
        sections={name:int(n) for name,n in re.findall(r'^(\.\S+)\s+(\d+)\s+',size,re.M)}
        report['avr']['sections']=sections
        assert sections.get('.data',0)+sections.get('.bss',0)<=12288
        assert sections.get('.text',0)+sections.get('.data',0)<=131072
        for tool,flags,name in [('avr-nm',['-S','--size-sort'],'symbols.txt'),('avr-objdump',['-d'],'disassembly.txt')]:
            text=run([avr/(tool+suffix),*flags,elf]).stdout
            (folder/name).write_text(text)
            if tool=='avr-nm' and re.search(r'\b(malloc|calloc|realloc|free)$',text,re.M): raise RuntimeError('Allocator linked into AVR firmware')
        print('AVR sections: '+str(sections),flush=True)
    report['status']='PASS'
except Exception as error:
    report['status']='FAIL'; report['error']=str(error); raise
finally:
    record()
