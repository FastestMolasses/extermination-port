#!/usr/bin/env python3
"""Run existing area exporters and full original-data checks in this worktree.

All game data and detailed logs stay local under ignored assets/ and build/.
Only command names, exit status, timing and file hashes enter receipts. Output
folders must be new and every existing asset path must be a real private path.
This is an explicit long-running task, never a default test target.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
PROFILES=('01','00','02','04','22','06','13','13x','19','19x','19s1','15','03')
SPLIT={'13x','19x','19s1','15'}
REQUIRES={'13x':'13','19x':'19','19s1':'19'}


def private_assets(root):
    """Refuse symlinked ancestors or descendants before starting any writer."""
    assets=root/'assets'
    if root.resolve()!=root or assets.is_symlink():
        raise ValueError('asset destination must be a real directory in this worktree')
    if assets.exists() and not assets.is_dir():
        raise ValueError('asset destination is not a directory')
    if assets.exists():
        for directory, dirs, files in os.walk(assets,followlinks=False):
            for name in dirs+files:
                if Path(directory,name).is_symlink():
                    raise ValueError('asset tree contains a symlink; refusing writes')
    return assets


def commands(profile):
    if profile not in PROFILES:raise ValueError('unknown asset profile')
    if profile=='03':
        return [('export','tools/export_area03_assets.py'),
                ('verify','tools/test_area03_assets_reference.py')]
    jobs=[('export',f'tools/export_area{profile}_{part}.py') for part in ('level','tables','sfx')]
    if profile in SPLIT:jobs.append(('export',f'tools/export_area{profile}_split.py'))
    jobs.append(('verify',f'tools/test_area{profile}_assets_reference.py'))
    return jobs


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',type=Path,required=True,help='new ignored build directory')
    parser.add_argument('--profile',choices=PROFILES,action='append')
    args=parser.parse_args(argv)
    folder=args.run.resolve()
    if not folder.is_relative_to(ROOT/'build'):parser.error('--run must be under this worktree build/')
    private_assets(ROOT)
    folder.mkdir(parents=True,exist_ok=False)
    profiles=args.profile or PROFILES
    if len(set(profiles))!=len(profiles):parser.error('duplicate profiles')
    env={k:v for k,v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_HEADLESS='1',EM_REFERENCE='fork',EM_TEST_FULL='1',EM_TEST_JOBS='2',PYTHONUNBUFFERED='1')
    report={'schema':1,'scope':'private area exports and full original-data comparisons; no live certification',
            'started_utc':datetime.now(timezone.utc).isoformat(),
            'revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'capture_selection':'Each exporter/checker selects its documented original inputs; retained legacy captures are read-only.',
            'profiles':[]}
    failed=set()
    for profile in profiles:
        row={'profile':profile,'commands':[],'passed':False};report['profiles'].append(row)
        if REQUIRES.get(profile) in failed:
            row['blocked_by']=REQUIRES[profile];failed.add(profile)
        else:
            for phase,script in commands(profile):
                private_assets(ROOT)
                log=folder/(Path(script).stem+'.log');start=time.monotonic()
                source_sha=hashlib.sha256((ROOT/script).read_bytes()).hexdigest()
                print('START',profile,phase,flush=True)
                with log.open('x') as out:
                    rc=subprocess.run([sys.executable,script],cwd=ROOT,env=env,stdout=out,stderr=subprocess.STDOUT).returncode
                if hashlib.sha256((ROOT/script).read_bytes()).hexdigest()!=source_sha:
                    raise RuntimeError('export/check source changed during execution')
                item={'phase':phase,'script':script,'script_sha256':source_sha,
                      'exit_code':rc,'seconds':round(time.monotonic()-start,3),'private_log':str(log.relative_to(ROOT))}
                row['commands'].append(item)
                print('PASS' if rc==0 else 'FAIL',profile,script,item['seconds'],flush=True)
                if rc:failed.add(profile);break
            row['passed']=profile not in failed
        (folder/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    private_assets(ROOT)
    print('asset profiles:',len(report['profiles'])-len(failed),'passed;',len(failed),'failed',flush=True)
    return int(bool(failed))


if __name__=='__main__':raise SystemExit(main())
