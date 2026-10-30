#!/usr/bin/env python3
"""Local CPU/platform proofs; report contains metadata and hashes, never assets.
Run from a checkout with a sibling Extermination and private disc exports.
Raw diagnostic logs remain local in ignored build/platforms/. No uploads.
"""
from __future__ import annotations
import argparse, hashlib, json, os, platform, shlex, shutil, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',choices=['gcc','clang'],default='clang')
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--baseline',type=Path,help='macOS results.json from identical source')
    ap.add_argument('--runtime',action='store_true',help='also build and run New Game and the main level smoke')
    ap.add_argument('--full',action='store_true',help='exhaustive CPU proofs and whole main route')
    ap.add_argument('--ps2-drive',action='store_true',help='use the original optional disc-drive timing for the runtime proof')
    args=ap.parse_args(); os.chdir(ROOT)
    out=args.out.resolve(); out.mkdir(parents=True,exist_ok=True)
    if (out/'results.json').exists(): ap.error('use a new output folder; receipts are never overwritten')
    cc=shutil.which(args.compiler)
    if not cc: ap.error('compiler not found')
    flags=['-ffp-contract=off']
    if sys.platform.startswith('linux'): flags+=['-D_GNU_SOURCE']
    if platform.machine().lower() in ('x86_64','amd64'): flags+=['-msse2','-mfpmath=sse']
    bindir=out/'bin'; bindir.mkdir(exist_ok=True)
    # Tests historically invoke cc directly. This local wrapper selects the
    # requested compiler without editing test assertions or system alternatives.
    wrapper=bindir/'cc'; wrapper.write_text('#!/bin/sh\nexec '+shlex.join([cc,*flags])+' "$@"\n'); wrapper.chmod(0o755)
    clang_wrapper=bindir/'clang'
    clang_wrapper.write_text(wrapper.read_text()); clang_wrapper.chmod(0o755)
    env={k:v for k,v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_HEADLESS='1',CC=str(wrapper),PATH=str(bindir)+os.pathsep+env.get('PATH',''),PYTHONDONTWRITEBYTECODE='1')
    if args.full: env['EM_TEST_FULL']='1'
    sources=sorted(p for directory in ('src','tests','tools') for p in (ROOT/directory).rglob('*')
                   if p.is_file() and p.suffix in ('.c','.h','.m','.py') and '__pycache__' not in p.parts)
    def source_digest():
        h=hashlib.sha256()
        for p in [ROOT/'Makefile',*sources]: h.update(str(p.relative_to(ROOT)).encode()+b'\0'+p.read_bytes())
        return h.hexdigest()
    try:
        commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True,stderr=subprocess.DEVNULL).strip()
    except (OSError,subprocess.CalledProcessError):
        commit=None  # read-only container mounts may omit the worktree's Git directory
    result=dict(schema=1,platform=platform.platform(),machine=platform.machine(),compiler=args.compiler,
                compiler_version=subprocess.check_output([cc,'--version'],text=True).splitlines()[0],
                git_commit=commit,
                source_sha256=source_digest(),full=args.full,ps2_drive=args.ps2_drive,tests=[],runtime={},mac_only=[])
    def save():
        (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    def run(name,cmd,extra=None,timeout=1800):
        started=time.monotonic(); log=out/(name+'.log')
        with log.open('w') as f:
            try: rc=subprocess.run(cmd,cwd=ROOT,env=env| (extra or {}),stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
            except subprocess.TimeoutExpired: rc=124
        # Missing oracle inputs may be a soft skip in legacy tests. They
        # are a failure of this platform proof, except the declared Metal part.
        skipped=[line for line in log.read_text(errors='replace').splitlines() if 'SKIP' in line.upper() and not (name=='fog' and ('part B' in line or 'Metal' in line))]
        if skipped and name not in ('build','newgame_control','level_run','level_check','present_again') and rc==0: rc=125
        entry=dict(name=name,exit_code=rc,seconds=round(time.monotonic()-started,3),log_sha256=sha(log))
        result['tests'].append(entry); save(); print(name,'PASS' if rc==0 else 'FAIL',flush=True)
        return rc==0
    run('vu_host',[sys.executable,'tools/test_vu_host_lanes.py'])
    wrapper.write_text('#!/bin/sh\nexec '+shlex.join([cc,*flags,'-DEM_VU_HOST_FORCE_INTEGER=1'])+' "$@"\n')
    run('vu_integer',[sys.executable,'tools/test_vu_host_lanes.py'])
    wrapper.write_text('#!/bin/sh\nexec '+shlex.join([cc,*flags])+' "$@"\n')
    for name,script in [('ee','ee_float_header'),('raster','gs_raster_reference'),('dof','dof_pass_reference'),
                        ('fog','gs_fog_conformance'),('memory','gs_memory_reference'),('texture','gs_texture_reference')]:
        run(name,[sys.executable,'tools/test_'+script+'.py'])
    run('swizzle',['make','test-gs-swizzle','CC='+str(wrapper)])
    gs=out/'gs_world'
    if run('world_build',[str(wrapper),'-std=c11','-O2','-Wall','-Wextra','-Werror','-Isrc',
                          'tests/gs_world_test.c','src/gs/em_gs_world.c','src/gs/em_gs_raster.c','src/gs/em_gs_frame.c',
                          '-pthread','-lm','-o',str(gs)]):
        # The fixture itself asserts equality at 1/2/3/8 workers, all band
        # settings, and the host default. One invocation covers them all.
        run('world',[str(gs)])
    if sys.platform!='darwin':
        result['mac_only']=[
            dict(test='test_gs_fog_conformance part B',reason='Metal shader readback; CPU portions still run'),
            dict(test='check_present_again.py / EM_PRESENT_LOG',reason='Metal retained presented-frame readback'),
            dict(test='test_fb2_pixels presented BMP',reason='null backend has no overlay/GPU presented image')]
    if args.runtime:
        exe=out/'extermination'
        if run('build',['make','-j4','all','HEADLESS=1','CC='+cc,'BIN='+str(exe)]):
            result['binary_sha256']=sha(exe)
            env['EM_LEVEL_SMOKE_BIN']=str(exe)
            run('newgame_control',[str(exe)],{'EM_STARTUP_TEST':'newgame-control','EM_UNCAPPED':'1'})
            # Existing checker remains unchanged. GPU assertions are separately
            # reported mac-only; no synthetic present.log is ever generated.
            smoke=out/'smoke'; smoke.mkdir(exist_ok=True)
            extra=dict(EM_STARTUP_TEST='newgame-level',EM_UNCAPPED='1',EM_LEVEL_SMOKE_UNTIL='' if args.full else 'battery',
                       EM_AREA_CHANGE_LOG=str(smoke/'ticks.jsonl'),EM_RAND_TRACE=str(smoke/'rand.trace'),
                       EM_STATUS_PAGES_TRACE=str(smoke/'status_pages.trace'))
            if args.ps2_drive: extra['EM_PS2_DISC_DRIVE_TIMING']='1'
            if sys.platform=='darwin': extra['EM_PRESENT_LOG']=str(smoke/'present.log')
            if run('level_run',[str(exe)],extra):
                run('level_check',[sys.executable,'tools/test_level_smoke.py','--log',str(smoke/'ticks.jsonl'),
                    '--run-log',str(out/'level_run.log'),'--rand-trace',str(smoke/'rand.trace'),
                    '--status-pages-trace',str(smoke/'status_pages.trace'),'--require-through','last' if args.full else 'battery'])
                if sys.platform=='darwin':
                    run('present_again',[sys.executable,'tools/check_present_again.py','--present',str(smoke/'present.log'),
                        '--ticks',str(smoke/'ticks.jsonl'),'--entries','3' if args.full else '2'])
            for filename in ('ticks.jsonl','rand.trace'):
                p=smoke/filename
                if p.exists(): result['runtime'][filename]=dict(bytes=p.stat().st_size,sha256=sha(p))
            p=smoke/'rand.trace'
            if p.exists():
                # Return addresses differ between ISAs; compare exactly the
                # requested counter and state columns, without remapping ticks.
                stream=b''.join(b' '.join(line.split()[:2])+b'\n' for line in p.read_bytes().splitlines())
                result['runtime']['rand_counter_state_sha256']=hashlib.sha256(stream).hexdigest()
            import re
            p=out/'newgame_control.log'
            match=re.search(r'locked_ticks=(\d+).*?displacement[= ]([\d.]+)',p.read_text())
            result['runtime']['control']=match.groups() if match else None
    if args.baseline:
        base=json.loads(args.baseline.read_text())
        result['comparison']=dict(same_source=base['source_sha256']==result['source_sha256'],
            ticks_equal=base['runtime'].get('ticks.jsonl')==result['runtime'].get('ticks.jsonl') if 'ticks.jsonl' in result['runtime'] else False,
            rand_equal=base['runtime'].get('rand_counter_state_sha256')==result['runtime'].get('rand_counter_state_sha256') if 'rand_counter_state_sha256' in result['runtime'] else False,
            control_equal=base['runtime'].get('control')==result['runtime'].get('control') if result['runtime'].get('control') else False)
    result['source_unchanged']=source_digest()==result['source_sha256']
    result['passed']=result['source_unchanged'] and all(t['exit_code']==0 for t in result['tests']) and all(result.get('comparison',{'no_comparison':True}).values())
    # The CPU-only report never claims the complete platform fidelity gate.
    result['phase1_complete']=False
    save(); return 0 if result['passed'] else 1
if __name__=='__main__': raise SystemExit(main())
