#!/usr/bin/env python3
"""Render the real LVGL UI on a Linux host without hardware.
Usage: python tests/render_ui.py --lvgl-dir /path/to/lvgl-v8.4.0 [--output /path]
Requires gcc/g++, Python, and optionally Pillow for PNG output.
"""
import argparse, concurrent.futures, pathlib, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--lvgl-dir',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,default=pathlib.Path('rendered'));a=p.parse_args()
project=pathlib.Path(__file__).resolve().parent.parent
lvgl=a.lvgl_dir.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='weather-render-') as tmp:
 tmp=pathlib.Path(tmp)
 conf=(project/'include/lv_conf.h').read_text()
 start=conf.index('#define LV_MEM_CUSTOM');end=conf.index('#define LV_TICK_CUSTOM')
 conf=conf[:start]+'#define LV_MEM_CUSTOM 0\n#define LV_MEM_SIZE (4U * 1024U * 1024U)\n'+conf[end:]
 conf=conf.replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0')
 (tmp/'lv_conf.h').write_text(conf)
 include=['-I'+str(tmp),'-I'+str(project/'tests/native'),'-I'+str(lvgl),'-I'+str(project/'include'),'-DLV_CONF_INCLUDE_SIMPLE']
 def compile_c(src):
  obj=tmp/(str(src.relative_to(lvgl/'src')).replace('/','_')+'.o')
  subprocess.run(['gcc','-O1',*include,'-c',str(src),'-o',str(obj)],check=True,capture_output=True)
  return str(obj)
 with concurrent.futures.ThreadPoolExecutor(8) as pool:objects=list(pool.map(compile_c,(lvgl/'src').rglob('*.c')))
 binary=tmp/'render'
 subprocess.run(['g++','-std=c++17','-O1',*include,str(project/'tests/native/render.cpp'),str(project/'src/ui.cpp'),*objects,'-o',str(binary)],check=True)
 subprocess.run([str(binary)],cwd=out,check=True)
 try:
  from PIL import Image
  for f in out.glob('*.ppm'):Image.open(f).save(f.with_suffix('.png'))
 except ImportError:print('Pillow not installed; PPM files are available.')
print('Rendered sample screens:',out)
