#!/usr/bin/env python3
"""AREA01's original script chains on the sole shared native interpreter.

Same worker boundaries and modeled external services as the existing AREA11
script oracle. Callback implementations, placement, timeline and pose remain
explicit boundaries, not successful live binding claims. Every script-owned
write, worker argument/order, return value and tick is compared with the
pinned original instructions. Default quick clamps delay records as documented
by test_area_script_reference; EM_TEST_FULL=1 retains original durations.
"""
import subprocess
import ctypes as C
import export_area01_common as A
import export_area01_tables as T
import export_area01_boot_scripts as B
import test_area_script_reference as S
from reference_mode import FULL, banner


# Handler owners have their own original-instruction suites. At this
# composition layer they are explicit original-address call boundaries.
EXTENDED=(0x1B9CF0,0x1B7F90,0x1B76D0,0x1B7670,0x1B6D70,0x1B6AE0,0x1798D0)
old_native_init=S.Native.__init__
def native_init(self,*args,**kwargs):
    old_native_init(self,*args,**kwargs)
    fn=S.WORKER_TYPES['c_record'](self.make('c_record',None,'special',''))
    self.keep.append(fn);self.workers.handler=C.cast(fn,C.c_void_p).value
    self.mem.host=self.host
S.Native.__init__=native_init
old_install=S.install_oracle
def install(oracle,env,calls,callbacks=()):
    old_install(oracle,env,calls,tuple(callbacks)+EXTENDED)
S.install_oracle=install
old_before=S.Env.before_tick
def before(self,mem):
    old_before(self,mem)
    if hasattr(mem,'host'):mem.host.st_0E |= 0x1000
    else:mem.save(S.SYNTHETIC_OWNER+0x1FE,mem.load(S.SYNTHETIC_OWNER+0x1FE,2)|0x1000,2)
S.Env.before_tick=before

def main():
    out=A.ROOT/'build/level2/scripts';out.mkdir(parents=True,exist_ok=True)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Isrc',
        'tests/area01_script_binding_test.c','src/game/em_area01_script_live.c',
        'src/game/em_script.c','src/game/em_spawn_table.c','-o',str(out/'binding')],cwd=A.ROOT,check=True)
    subprocess.run([str(out/'binding'),'assets/area01_boot_scripts',
                    'assets/area01/door_destinations.emsp'],cwd=A.ROOT,check=True)
    elf=A.read_elf();cap=A.Capture(A.ARRIVAL)
    # Load-time scripts are the original data. Do not treat mutations made
    # during any captured beat as fresh per-visit initialization.
    ram=bytearray(cap.ram)
    read=A.static_reader(elf,A.read_overlay())
    ram[0x823500:0x82CD00]=A.read_overlay()
    for _name,lo,hi,_entry,_entries in B.WINDOWS:ram[lo:hi]=read(lo,hi-lo)
    S.CONTEXT.update(elf=elf,ram=bytes(ram),spad=cap.spad,lib=S.build(),synthetic=b'')
    callbacks=(0x825130,0x825240,0x825910,0x826950,0x158130,0x158050,0x1580C0,0x157F60,0x1575B0,
               0x1BB400,0x1BB310,0x1BBAE0,0x1BBBF0,0x1B6EA0)
    images=[('overlay',0x828A00,0x82CD00,T.SCRIPT_ENTRIES)]
    images += [(name,lo,hi,entries) for name,lo,hi,_entry,entries in B.WINDOWS]
    cases=[]
    for name,lo,hi,entries in images:
        for entry in entries:
            for skip in ((None,4,12) if FULL else (None,4)):
                scene=dict(init=S.SPAD_IDLE,callbacks=callbacks,callback_ticks=2,
                           cursor_step=20,limit=12000)
                if skip is not None:scene['skip_tick']=skip
                cases.append((f'{name} {entry:08X} skip={skip}',entry,S.SYNTHETIC_OWNER,(lo,hi),scene))
    ticks=0
    for case in cases:
        label,n,outcome,fault,_math=S.run_scenario(case)
        assert outcome!='fault',(label,fault)
        ticks+=n
        print(f'{label}: {n} ticks -> {outcome}',flush=True)
    banner(f'{len(cases)} AREA01/boot script variants',f'{ticks} original-instruction lockstep ticks',
           'binding resource aliases/identity/callback contract')

if __name__=='__main__':main()
