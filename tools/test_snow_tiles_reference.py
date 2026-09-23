#!/usr/bin/env python3
"""Original E67C0 flow, submission parameters, and captured DMA comparison.

The owner's original ELF supplies all instruction words and tables. Finite
arithmetic follows truncating ADD/MUL and snapshot-confirmed rounded DIV.S.
SDK sine instructions execute directly; VU matrix helpers are independently
recovered operations. This does not claim universal hardware rounding.
"""
from pathlib import Path
import ctypes as C, struct, subprocess, json, math, random, argparse
ROOT=Path(__file__).resolve().parents[1]
from reference_mode import FULL, MODE, banner, part
ENTRY,END,ACTOR,RETURN=0x1E67C0,0x1E6F60,0x600000,0xBADF00D
LIB=C.CDLL(None)
LIB.sinf.argtypes=[C.c_float];LIB.sinf.restype=C.c_float
def signed(value, bits=32):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def number(value):
    return struct.unpack('<f', struct.pack('<I', value & 0xffffffff))[0]


def truncate(value):
    rounded = number(bits(value))
    return number(bits(rounded) - 1) if abs(rounded) > abs(value) else rounded


class Weather(C.Structure):
    _fields_ = [('phase', C.c_float * 6), ('drift', C.c_float * 6),
                ('intensity', C.c_float), ('target', C.c_float),
                ('rate', C.c_float), ('seed', C.c_uint32),
                ('burst_wait', C.c_int32), ('state', C.c_uint8)]



class Config(C.Structure):
    _fields_=[('descriptor',C.c_float*36),('lookup',C.c_float*80),('rows',C.c_float*72)]
class Tile(C.Structure):
    _fields_=[('descriptor',C.c_float*36),('params',C.c_float*4),('matrix',C.c_float*16)]

def oracle(elf, initial, strength, eye, descriptor=None, host_sine=False,
           flags=0, area_entry=0x0b00, player_mode=0, camera_id=0):
    memory={};registers=[0]*32;floats=[0]*32;condition=False;hi=0;tiles=[];sines=[];angles=[];sine_return=None
    def save(address,value,size=4):
        for i in range(size):memory[address+i]=value>>(8*i)&255
    def load(address,size=4):
        if address not in memory and 0x100000<=address and address+size<=0x275B00:
            return int.from_bytes(elf[address-0x100000+0x300:address-0x100000+0x300+size],'little')
        return sum(memory.get(address+i,0)<<(8*i) for i in range(size))
    def fetch(pc):return load(pc)
    def vec(a,n=4):return [number(load(a+4*i)) for i in range(n)]
    def storevec(a,v):
        for i,x in enumerate(v):save(a+4*i,bits(x))
    for i,b in enumerate(initial[:68]):save(ACTOR+0x1f0+i,b,1)
    if descriptor:
        for i,b in enumerate(descriptor):save(0x255170+i,b,1)
    for i,x in enumerate(eye):save(0x8105d0+4*i,bits(x))
    save(0x8105dc,bits(1));save(0x810700,area_entry>>8,1);save(0x810701,area_entry&255,1)
    save(0x8101e4,player_mode,1);save(0x81024e,camera_id,2)
    registers[4]=ACTOR;registers[5]=flags;registers[6]=load(ACTOR+0x22c)
    registers[28]=0x27d370;registers[29]=0x700000;registers[31]=RETURN;floats[12]=bits(strength)
    def plain(word):
        nonlocal hi, condition
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        imm = signed(word & 65535, 16)
        address = (registers[rs] + imm) & 0xffffffff
        if op == 0:
            fn = word & 63
            if fn == 0: registers[rd] = (registers[rt] << (word >> 6 & 31)) & 0xffffffff
            elif fn == 2: registers[rd] = (registers[rt] & 0xffffffff) >> (word >> 6 & 31)
            elif fn == 3: registers[rd] = signed(registers[rt]) >> (word >> 6 & 31) & 0xffffffff
            elif fn == 4: registers[rd] = (registers[rt] << (registers[rs] & 31)) & 0xffffffff
            elif fn == 6: registers[rd] = (registers[rt] & 0xffffffff) >> (registers[rs] & 31)
            elif fn == 7: registers[rd] = signed(registers[rt]) >> (registers[rs] & 31) & 0xffffffff
            elif fn == 16: registers[rd] = hi
            elif fn == 26:
                x, y = signed(registers[rs]), signed(registers[rt])
                hi = (x - int(x / y) * y) & 0xffffffff
            elif fn == 33: registers[rd] = (registers[rs] + registers[rt]) & 0xffffffff
            elif fn == 35: registers[rd] = (registers[rs] - registers[rt]) & 0xffffffff
            elif fn == 36: registers[rd] = registers[rs] & registers[rt]
            elif fn == 37: registers[rd] = registers[rs] | registers[rt]
            elif fn == 38: registers[rd] = registers[rs] ^ registers[rt]
            elif fn == 42: registers[rd] = int(signed(registers[rs]) < signed(registers[rt]))
            elif fn == 43: registers[rd] = int((registers[rs] & 0xffffffff) < (registers[rt] & 0xffffffff))
            elif fn == 45: registers[rd] = (registers[rs] + registers[rt]) & 0xffffffffffffffff
            else: raise AssertionError(('SPECIAL', fn))
        elif op == 9: registers[rt] = address
        elif op == 10: registers[rt] = int(signed(registers[rs]) < imm)
        elif op == 11: registers[rt] = int((registers[rs] & 0xffffffff) < (imm & 0xffffffff))
        elif op == 12: registers[rt] = registers[rs] & (word & 65535)
        elif op == 13: registers[rt] = registers[rs] | (word & 65535)
        elif op == 14: registers[rt] = registers[rs] ^ (word & 65535)
        elif op == 15: registers[rt] = (word & 65535) << 16
        elif op == 28 and word & 63 == 40:
            registers[rd] = sum((((registers[rs] >> (8*i) & 255) +
                                  (registers[rt] >> (8*i) & 255)) & 255) << (8*i)
                                for i in range(16))
        elif op in (30, 33, 35, 36, 55):
            size = {30: 16, 33: 2, 35: 4, 36: 1, 55: 8}[op]
            value = load(address, size)
            registers[rt] = signed(value, 16) & 0xffffffff if op == 33 else value
        elif op in (31, 40, 41, 43, 63): save(address, registers[rt], {31: 16, 40: 1, 41: 2, 43: 4, 63: 8}[op])
        elif op == 49: floats[rt] = load(address)
        elif op == 57: save(address, floats[rt])
        elif op == 17:
            fs, fd, fn = rd, word >> 6 & 31, word & 63
            if rs == 0: registers[rt] = floats[fs]
            elif rs == 4: floats[fs] = registers[rt] & 0xffffffff
            elif rs == 20 and fn == 32: floats[fd] = bits(truncate(float(signed(floats[fs]))))
            elif rs == 16:
                x, y = number(floats[fs]), number(floats[rt])
                if fn == 0: floats[fd] = bits(truncate(x + y))
                elif fn == 1: floats[fd] = bits(truncate(x - y))
                elif fn == 2: floats[fd] = bits(truncate(x * y))
                elif fn == 3: floats[fd] = bits(x / y)
                elif fn == 5: floats[fd] = floats[fs] & 0x7fffffff
                elif fn == 6: floats[fd] = floats[fs]
                elif fn == 7: floats[fd] = floats[fs] ^ 0x80000000
                elif fn in (13, 36): floats[fd] = int(x) & 0xffffffff
                elif fn == 50: condition = x == y
                elif fn == 52: condition = x < y
                elif fn == 54: condition = x <= y
                else: raise AssertionError(('FPU', fn))
            else: raise AssertionError(('COP1', rs, fn))
        else: raise AssertionError(('opcode', op))
        registers[0] = 0


    pc=ENTRY
    for _ in range(100000):
        if pc==sine_return:sines[-1][1]=number(floats[0]);sine_return=None
        if pc==RETURN:
            return bytes(load(ACTOR+0x1f0+i,1) for i in range(68)),tiles,sines,angles
        word=fetch(pc);op,rs,rt=word>>26,word>>21&31,word>>16&31
        offset=signed(word&65535,16)*4;branch=None
        if op==2:
            plain(fetch(pc+4));pc=word&0x3ffffff;pc*=4;continue
        if op==3:
            target=(word&0x3ffffff)*4;plain(fetch(pc+4))
            a0,a1,a2=registers[4:7]
            if target==0x21b9a0:pass
            elif target==0x1281c0:registers[2]=int(number(floats[12]))&0xffffffff
            elif target in (0x11e2a8,0x11c7b0,0x11d770,0x11ccc8):
                if target==0x11e2a8 and host_sine:
                    floats[0]=bits(LIB.sinf(number(floats[12])));pc+=8;continue
                if target==0x11e2a8:sines.append([number(floats[12]),None]);sine_return=pc+8
                registers[31]=pc+8;pc=target;continue
            elif target==0x11df78:floats[0]=floats[12]&0x7fffffff
            elif target==0x1029c0:storevec(a0,[1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.])
            elif target==0x102b08:
                angle=number(floats[12]);angles.append(angle)
                x=truncate(number(0x3fc90fdb)-abs(angle));square=truncate(x*x)
                coeff=vec(0x241100);terms=[truncate(c*x) for c in coeff]
                for lanes in [4,3,2,1]:
                    for c in range(lanes):terms[c]=truncate(terms[c]*square)
                cosine=x
                for c in [3,2,1,0]:cosine=truncate(cosine+terms[c])
                sine=truncate(math.sqrt(truncate(1.-truncate(cosine*cosine))))
                if angle<0:sine=-sine
                storevec(a0,[1.,0.,0.,0.,0.,cosine,sine,0.,0.,-sine,cosine,0.,0.,0.,0.,1.])
            elif target==0x1026a0:
                m=vec(a1,16);v=vec(a2);result=[]
                for c in range(4):
                    x=truncate(m[c]*v[0])
                    for j in range(1,4):x=truncate(x+truncate(m[4*j+c]*v[j]))
                    result.append(x)
                storevec(a0,result)
            elif target==0x1028b8:storevec(a0,[truncate(x+y) for x,y in zip(vec(a1),vec(a2))])
            elif target==0x102900:storevec(a0,[truncate(x*number(floats[12])) for x in vec(a1)])
            elif target==0x102948:storevec(a0,vec(a1))
            elif target==0x1cfae0:
                storevec(a0,vec(a2,16));storevec(a0+0x44,[number(floats[i]) for i in [12,14,13,15]])
            elif target==0x1cffe0:
                src=registers[7]
                tiles.append({'matrix':vec(src,16),'params':[number(load(src+i)) for i in [0x44,0x48,0x50,0x4c]],'descriptor':bytes(load(a2+i,1) for i in range(144))})
            else:raise AssertionError(('callee',hex(target),hex(pc)))
            pc+=8;continue
        if op in (4, 5, 20, 21):
            taken = (registers[rs] == registers[rt]) == (op in (4, 20))
            if op in (20, 21) and not taken:
                pc += 8
                continue
            branch = pc + 4 + offset if taken else pc + 8
        elif op == 7: branch = pc + 4 + offset if signed(registers[rs]) > 0 else pc + 8
        elif op == 6: branch = pc + 4 + offset if signed(registers[rs]) <= 0 else pc + 8
        elif op == 1:
            taken = signed(registers[rs]) >= 0 if rt == 1 else signed(registers[rs]) < 0
            assert rt in (0, 1)
            branch = pc + 4 + offset if taken else pc + 8
        elif op == 17 and rs == 8: branch = pc + 4 + offset if condition == bool(rt & 1) else pc + 8
        elif op == 0 and word & 63 == 8: branch = registers[rs]
        if branch is not None:
            plain(fetch(pc + 4))
            pc = branch
        else:
            plain(word)
            pc += 4
    raise AssertionError('snow tile oracle failed to return')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
    p.add_argument('--reference-ee',type=Path)
    p.add_argument('--reference-tiles',type=Path)
    args=p.parse_args();elf=(args.decomp_root/'config/SCUS_971.12').read_bytes()
    config=Config.from_buffer_copy((ROOT/'assets/scene_snow/snow.emsn').read_bytes()[20:])
    descriptor=bytes(config.descriptor)
    outdir=ROOT/'build/weather_reference';outdir.mkdir(exist_ok=True,parents=True)
    lib=outdir/'snow_tiles.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-shared','-fPIC','-Isrc','src/game/em_snow.c','-o',str(lib)],cwd=ROOT,check=True)
    native=C.CDLL(str(lib));emit=native.em_snow_tiles
    emit.argtypes=[C.POINTER(Weather),C.POINTER(Config),C.c_float,C.POINTER(C.c_float),C.POINTER(Tile)]
    rng=random.Random(0x1e67c0);cases=0;tile_count=0;sdk_matrix_error=0.0;sdk_matrix_matches=0
    # The full sweep draws 12 random weather states per strength. Every draw
    # is made in both modes (same stream); quick runs the first 6 of each
    # strength: every strength, including 0 and 1, with fresh seeds/phases.
    per_strength=12 if FULL else 6
    for strength in [0.0,0.1,0.5,1.0]:
        for case in range(12):
            w=Weather();w.seed=rng.randrange(0x100000000);w.state=1
            for i in range(6):w.phase[i]=rng.uniform(1.0,2.0);w.drift[i]=rng.uniform(-0.2,1.5)
            eye=[rng.uniform(-800,800) for _ in range(3)]
            if case>=per_strength:continue
            before=bytes(w)
            expected,tiles,_,_=oracle(elf,before,strength,eye,descriptor,host_sine=True)
            out=(Tile*108)();emit(C.byref(w),C.byref(config),strength,(C.c_float*3)(*eye),out)
            assert bytes(w)[:68]==expected,('poststate',cases)
            assert len(tiles)==108
            for i,(actual,ref) in enumerate(zip(out,tiles)):
                for field in ['matrix','params']:
                    want=struct.pack('<'+str(len(ref[field]))+'f',*ref[field])
                    assert bytes(getattr(actual,field))==want,(field,cases,i,list(getattr(actual,field)),ref[field])
                assert bytes(actual.descriptor)==ref['descriptor'],('descriptor',cases,i)
            _,sdk_tiles,_,_=oracle(elf,before,strength,eye,descriptor,host_sine=False)
            for actual,ref in zip(out,sdk_tiles):
                difference=max(abs(x-y) for x,y in zip(actual.matrix,ref['matrix']))
                sdk_matrix_error=max(sdk_matrix_error,difference)
                sdk_matrix_matches+=difference==0.0
            cases+=1;tile_count+=108
    banner(part(cases,48,'instruction-flow cases (every strength)'),
           'captured snapshot comparison in full when --reference-ee/--reference-tiles are given')
    report={'status':'PASS','mode':MODE,'instruction_flow_cases':cases,'tiles_compared':tile_count,
            'flow_sine_dependency':'host sinf shared with native; original SDK waveform checked separately',
            'original_sdk_matrix_exact_matches':sdk_matrix_matches,
            'original_sdk_matrix_comparisons':tile_count,
            'original_sdk_matrix_max_error':sdk_matrix_error}
    if args.reference_ee:
        assert args.reference_tiles
        ram=args.reference_ee.read_bytes();refs=json.loads(args.reference_tiles.read_text())
        # Locate the actual weather actor by callback, rather than treating a
        # prior guessed allocation address as permanent engine structure.
        actors=[a for a in range(0x700000,0x810000,16) if struct.unpack_from('<I',ram,a+0x10)[0]==0x1e55f0]
        assert len(actors)==1,(actors,'expected one original weather controller')
        actor=actors[0]
        initial=ram[actor+0x1f0:actor+0x1f0+68]+bytes([1])+bytes(3)
        latest=refs[108:];w=Weather.from_buffer_copy(initial)
        strength=number(bits(w.intensity/127));driftstep=truncate(number(bits(.004))*strength)
        for row in range(6):
            w.phase[row]=latest[row*18]['params'][0]
            # Inverting a truncating add is not generally unique. This
            # representative is used only for a measured matrix error, not
            # asserted as the exact pre-render state.
            w.drift[row]=truncate(w.drift[row]-driftstep)
        before=bytes(w)
        # The saved globals contain camera sample134.5; this renderer ran
        # before that update, using sample134.0. Read the original track.
        bank=(args.decomp_root/'extract/chunk15/f12_id44.bin').read_bytes()
        def camera_eye(half_tick):
            a=struct.unpack_from('<8f',bank,0xd0820+(half_tick//2)*32)
            b=struct.unpack_from('<8f',bank,0xd0820+(half_tick//2+1)*32)
            return [truncate(a[i]+truncate(truncate(b[i]-a[i])*(half_tick%2*.5)))for i in range(3)]
        assert struct.pack('<3f',*camera_eye(269))==ram[0x8105d0:0x8105dc]
        eye=camera_eye(268)
        _,original_sdk,_,_=oracle(elf,before,strength,eye,descriptor,host_sine=False)
        out=(Tile*108)();emit(C.byref(w),C.byref(config),strength,(C.c_float*3)(*eye),out)
        assert bytes(w.phase)==ram[actor+0x1f0:actor+0x208]
        for actual,ref in zip(out,latest):
            assert bytes(actual.params)==struct.pack('<4f',*ref['params'])
            assert bytes(actual.descriptor)[32:48]==struct.pack('<4f',*ref['color'])
        # The previous intensity is uniquely constrained by the original
        # controller's saved target/rate and next intensity in this snapshot.
        # Its quotient mode remains ambiguous: both modes produce the same
        # observed colors and all six phase advances here.
        captured=Weather.from_buffer_copy(initial)
        guess=bits((captured.intensity-captured.rate*captured.target)/(1-captured.rate))
        prior=[]
        for candidate in range(guess-4,guess+5):
            value=number(candidate)
            if truncate(value+truncate(truncate(captured.target-value)*captured.rate))==captured.intensity:
                prior.append(value)
        assert len(prior)==1
        old=Weather.from_buffer_copy(before);old.intensity=prior[0]
        old_strength=number(bits(old.intensity/127))
        for row in range(6):
            old.phase[row]=refs[row*18]['params'][0]
            old.drift[row]=truncate(old.drift[row]-truncate(number(bits(.004))*old_strength))
        previous_out=(Tile*108)();emit(C.byref(old),C.byref(config),old_strength,(C.c_float*3)(*camera_eye(267)),previous_out)
        assert bytes(old.phase)==struct.pack('<6f',*[latest[row*18]['params'][0]for row in range(6)])
        for actual,ref in zip(previous_out,refs[:108]):
            assert bytes(actual.params)==struct.pack('<4f',*ref['params'])
            assert bytes(actual.descriptor)[32:48]==struct.pack('<4f',*ref['color'])
        seed_matches=0
        for frame in [refs[:108],latest]:
            seed=Weather.from_buffer_copy(initial).seed
            for ref in frame:
                fraction=number(bits((seed>>16)/65535.0));seed=(seed*37+11)&0xffffffff
                assert bits(truncate(fraction+number(bits(.0001))))==bits(ref['params'][3])
                seed_matches+=1
        report.update({'snapshot_seed_params_equal':seed_matches,'complete_snapshot_params_equal':216,
            'snapshot_colors_equal':216,'post_phase_bytes_equal':48,
            'snapshot_camera_time':134.5,'latest_tile_camera_time':134.0,
            'matrix_basis_max_error':max(abs(a.matrix[c]-b['matrix'][c])for a,b in zip(out,latest)for c in range(12)),
            'matrix_translation_max_error':max(abs(a.matrix[c]-b['matrix'][c])for a,b in zip(out,latest)for c in range(12,15)),
            'host_sine_vs_original_sdk_matrix_max_error':max(abs(a.matrix[c]-b['matrix'][c])for a,b in zip(out,original_sdk)for c in range(16))})
    print(json.dumps(report))
if __name__=='__main__':main()
