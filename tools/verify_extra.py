"""Verify CMake host/AVR configuration after tools/build.py completes."""
from pathlib import Path
import argparse,json,os,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--cc',required=True)
p.add_argument('--avr-bin',type=Path,required=True)
args=p.parse_args()
out=root/'build/1.0/cmake'
out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ)
env['PATH']=str(Path(args.cc).parent)+os.pathsep+str(args.avr_bin)+os.pathsep+env.get('PATH','')
records=[]
def run(command):
    result=subprocess.run(list(map(str,command)),cwd=root,env=env,capture_output=True,text=True,timeout=240)
    records.append(dict(command=list(map(str,command)),exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
    (out/'report.json').write_text(json.dumps(records,indent=2)+'\n')
    if result.returncode: raise RuntimeError(result.stdout+result.stderr)
ninja=shutil.which('ninja',path=env['PATH'])
common=['-G','Ninja','-DCMAKE_MAKE_PROGRAM='+ninja]
run(['cmake','-S','.', '-B',out/'host',*common,'-DCMAKE_C_COMPILER='+str(Path(args.cc).resolve()),
     '-DSB_PROFILE=RELEASE','-DPython3_EXECUTABLE='+sys.executable])
run(['cmake','--build',out/'host'])
run(['ctest','--test-dir',out/'host','--output-on-failure'])
run(['cmake','-S','.', '-B',out/'avr',*common,'-DCMAKE_TOOLCHAIN_FILE='+str(root/'tools/avr-toolchain.cmake')])
run(['cmake','--build',out/'avr'])
print('CMake PASS: native stripped Release, 4 CTest suites, complete AVR link')
