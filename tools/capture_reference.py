"""Run bounded headless MAME captures on Linux/WSL; no ROM uploads."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess

def capture(index, args):
    root=(args.out/f'round_{index+1:02d}').resolve();root.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,PACLAND_CAPTURE_ROOT=str(root),PACLAND_ROUND=str(index),PACLAND_FRAMES=str(args.frames))
    cmd=['xvfb-run','-a',args.mame,'pacland','-rompath',str(args.romdir.resolve()),
         '-video','soft','-sound','sdl' if args.audio else 'none','-nothrottle','-skip_gameinfo',
         '-cfg_directory',str(root/'cfg'),'-nvram_directory',str(root/'nvram'),
         '-autoboot_script',str(Path(__file__).with_suffix('.lua').resolve())]
    if args.audio:
        cmd+=['-wavwrite',str(root/'reference.wav')]
        env['SDL_AUDIODRIVER']='dummy'
    r=subprocess.run(cmd,env=env,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
    (root/'mame.log').write_bytes(r.stdout)
    if r.returncode:raise RuntimeError(f'MAME failed for round {index+1}')
    info=json.loads((root/'registers.json').read_text())
    if info['round_index']!=index:raise RuntimeError(f'Selection did not reach round {index+1}: {info}')
    return index+1

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--romdir',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--mame',default='/usr/games/mame')
    p.add_argument('--rounds',type=int,nargs='+',default=list(range(1,33)))
    p.add_argument('--frames',type=int,default=720);p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--audio',action='store_true')
    a=p.parse_args()
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        for n in pool.map(lambda n:capture(n-1,a),a.rounds):print('Captured round',n,flush=True)
