#!/usr/bin/env python3
"""One bounded 40-mutation check of the armed-pose instruction oracle."""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'src/game/em_aim_fire_pose.c'
OUT=ROOT/'build/aim-fire/pose/mutations'
# Each mutation changes one original operation, write or call argument.
MUTANTS=[
 ('half','HALF 0x3F000000u','HALF 0x3E800000u'),
 ('one','ONE 0x3F800000u','ONE 0x3F000000u'),
 ('two','TWO 0x40000000u','TWO 0x3F800000u'),
 ('scratch-address','SCRATCH 0x70003A20u','SCRATCH 0x70003A24u'),
 ('pose-slot','p, slot, 0, 0, 0, &clip','p, slot + 1, 0, 0, 0, &clip'),
 ('clip-sign','(int32_t)(int16_t)clip','(int32_t)(uint16_t)clip'),
 ('publish-destination','p, clip, dest, 0, 0, NULL','p, clip, dest + 16, 0, 0, NULL'),
 ('bank-count','i<rd(h,p+0xCu,1)','i+1<rd(h,p+0xCu,1)'),
 ('bank-weight','em_ee_mul_bits(TWO, rd(h,SCRATCH,4))','em_ee_add_bits(TWO, rd(h,SCRATCH,4))'),
 ('bank-stride','dest+64*i','dest+32*i'),
 ('bank-source','0x00288D40u+64*i','0x00287F40u+64*i'),
 ('x-field','x = rd(h,p+0x278u,4)','x = rd(h,p+0x27Cu,4)'),
 ('low-compare','em_ee_c_le_bits(x,0)','em_ee_c_lt_bits(x,0)'),
 ('high-compare','!em_ee_c_lt_bits(x,ONE)','!em_ee_c_le_bits(x,ONE)'),
 ('center-slot','pose(h,p,center,0x00288D40u)','pose(h,p,center+1,0x00288D40u)'),
 ('low-weight-order','em_ee_sub_bits(HALF,x)','em_ee_sub_bits(x,HALF)'),
 ('high-weight-order','em_ee_sub_bits(x,HALF)','em_ee_sub_bits(HALF,x)'),
 ('horizontal-slots','plane(h,p,0,2,1','plane(h,p,0,1,2'),
 ('vertical-low-center','plane(h,p,4,8,7','plane(h,p,3,8,7'),
 ('vertical-high-center','plane(h,p,3,6,5','plane(h,p,4,6,5'),
 ('vertical-low-slot','plane(h,p,4,8,7','plane(h,p,4,6,7'),
 ('vertical-high-slot','plane(h,p,3,6,5','plane(h,p,3,6,7'),
 ('y-field','y = rd(h,p+0x27Cu,4)','y = rd(h,p+0x278u,4)'),
 ('y-low-order','em_ee_sub_bits(HALF,y)','em_ee_sub_bits(y,HALF)'),
 ('y-high-order','em_ee_sub_bits(y,HALF)','em_ee_sub_bits(HALF,y)'),
 ('final-matrix-offset','node+0x90u,0x00287140u','node+0xA0u,0x00287140u'),
 ('normalize-count','j<3','j<2'),
 ('normalize-stride','0x90u+16*j','0x90u+4*j'),
 ('published-flag','p+0x303u,1,1','p+0x303u,0,1'),
 ('final-slot','0x0017A0B0u,p,0,0','0x0017A0B0u,p,1,0'),
 ('final-time','(int16_t)rd(h,p+0x276u,2)','(uint16_t)rd(h,p+0x276u,2)'),
 ('state-table','state==0x1D || state==0x1E','state==0x1D || state==0x1F'),
 ('slot-subtype','4*rd(h,a+0x275u,1)','4*rd(h,a+0x274u,1)'),
 ('slot-return-sign','(int16_t)rd(h,row+2*b,2)','(uint16_t)rd(h,row+2*b,2)'),
 ('publish-time-mode','state==0x31 || state==0x34','state==0x31 || state==0x35'),
 ('publish-time-sign','(int16_t)rd(h,a+0x276u,2)','(uint16_t)rd(h,a+0x276u,2)'),
 ('publish-copy-source','c+64*i,node+0x90u','c+64*i,node+0xA0u'),
 ('blend-complement','em_ee_sub_bits(ONE,f)','em_ee_sub_bits(f,ONE)'),
 ('blend-product','em_ee_mula_bits(omt,x)','em_ee_mula_bits(omt,em_ee_neg_bits(x))'),
 ('blend-affine-tail','wr(h,a+60,ONE,4)','wr(h,a+60,0,4)'),
]

def main():
    assert len(MUTANTS)==40
    OUT.mkdir(parents=True,exist_ok=True)
    source=SOURCE.read_text()
    report=[]
    for name,old,new in MUTANTS:
        assert old in source,name
        path=OUT/(name+'.c');path.write_text(source.replace(old,new,1))
        env=dict(os.environ,EM_AIM_FIRE_POSE_SOURCE=str(path),EM_TEST_JOBS='1')
        env.pop('EM_TEST_FULL',None)
        p=subprocess.run([sys.executable,'tools/test_aim_fire_pose_reference.py'],cwd=ROOT,
                         env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        (OUT/(name+'.log')).write_text(p.stdout)
        result='survived' if p.returncode==0 else ('compile-failed' if 'error:' in p.stdout else 'killed')
        report.append({'name':name,'result':result})
        print(name,result,flush=True)
    (OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print({r:sum(x['result']==r for x in report) for r in ('killed','survived','compile-failed')})

if __name__=='__main__':main()
