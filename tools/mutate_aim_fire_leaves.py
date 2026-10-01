#!/usr/bin/env python3
"""One fixed bounded 40-mutant sweep for the two aim/fire leaf routines."""
import json
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/aim-fire/leaves/mutations'
source=(ROOT/'src/game/em_aim_fire_leaves.c').read_text()
mutants=[]
for kind in (0x28,0x4F,0x34,0x33,0x51,0x1B,0x2B,0x2A,0x18,0x0E,0x0C,0x0A,0x50,0x1E,0x1F,0x1C,0x06):
    old=f'case 0x{kind:02X}:'
    mutants.append((f'kind-{kind:02x}-removed',source.replace(old,'')))
    mutants.append((f'kind-{kind:02x}-value',source.replace(old,old+' return 0xBAD;')))
for old,new,label in (
 ('default: return 0x101','default: return 0x100','default-class'),
 ('return 0x300','return 0x200','class300'),
 ('return 0x200','return 0x300','class200'),
 ('0x40C90FDB','0x40C90FDA','period-low'),
 ('0x40C90FDB','0x40C90FDC','period-high'),
 ('!em_ee_c_le_bits(angle, 0x40C90FDB)','em_ee_c_lt_bits(0x40C90FDB, em_ee_add_bits(angle, 0x35800000))','equality-boundary')):
    mutants.append((label,source.replace(old,new)))
assert len(mutants)==40
OUT.mkdir(parents=True,exist_ok=True)
results=[]
for label,text in mutants:
    p=OUT/(label+'.c');p.write_text(text)
    env=dict(os.environ,EM_AIM_FIRE_LEAVES_SOURCE=str(p))
    try:
        run=subprocess.run(['python3','tools/test_aim_fire_leaves_reference.py'],cwd=ROOT,env=env,capture_output=True,text=True,timeout=15)
        status='killed' if run.returncode else 'survived'
        log=run.stdout+run.stderr
        if 'error:' in log: status='invalid'
    except subprocess.TimeoutExpired:
        status='killed-timeout';log='Exceeded fixed 15-second execution bound'
    (OUT/(label+'.log')).write_text(log);p.unlink()
    results.append({'name':label,'result':status})
    print(label,status,flush=True)
(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
assert all(r['result'].startswith('killed') for r in results),results
