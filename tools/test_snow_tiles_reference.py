#!/usr/bin/env python3
"""Original E67C0 flow, submission parameters, the channel-3 packets, and
captured DMA comparison.

The owner's original ELF supplies all instruction words and tables. COP1
follows the measured EE model (tools/ee_cop1.py, docs/EE_FLOAT_MODEL.md;
the snapshot confirms the rounded DIV.S). The SDK sinf 0011E2A8 executes
directly on the original side and runs em_sdk_math_original's translation
(tables from the ELF) on the native side, as the live weather does; VU
matrix helpers are independently recovered operations.

Packets (docs/SNOW_PARTICLES.md "The channel-3 list"): on a sample of the
flow cases the ORIGINAL 001E67C0 runs again with its draw requests executed
(001CFAE0, 001CD370, 001CFFE0, 001CB9B0) over a render context seeded from
route capture 10 (the channel-3 cursor, the fog, the 001CD370(0) projection,
the P / K scratchpad copies); em_snow_tiles plus em_weather_packets_tile
(em_snow_runtime's per-tile path) must write the same 108 x 0x260 channel-3
bytes and leave the same cursor.
"""
from pathlib import Path
import ctypes as C, struct, subprocess, json, math, random, argparse
ROOT=Path(__file__).resolve().parents[1]
from reference_mode import FULL, MODE, banner, part
import ee_cop1
import ee_float_model
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
           flags=0, area_entry=0x0b00, player_mode=0, camera_id=0, execute=False, seed=None, out=None):
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
    if out is not None: out['load']=load
    for address,data in (seed or {}).items():
        for i,b in enumerate(data):memory[address+i]=b
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
            elif rs in (16, 20):
                # The measured EE model (tools/ee_cop1.py); no FPU ACC op here.
                kind, value = ee_cop1.cop1(word, floats[fs], floats[rt])
                assert kind != 'acc', ('FPU', fn)
                if kind == 'fd': floats[fd] = value
                else: condition = value
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
                # pi/2 -/+ |angle| is COP1 (add.s / sub.s) in 00102B08; the
                # 001029E8 polynomial is VU0 (per-op truncation, VSQRT of |x|).
                x=number(ee_float_model.ee_sub(0x3fc90fdb,bits(abs(angle))));square=truncate(x*x)
                coeff=vec(0x241100);terms=[truncate(c*x) for c in coeff]
                for lanes in [4,3,2,1]:
                    for c in range(lanes):terms[c]=truncate(terms[c]*square)
                cosine=x
                for c in [3,2,1,0]:cosine=truncate(cosine+terms[c])
                sine=number(ee_float_model.vu_sqrt(bits(truncate(1.-truncate(cosine*cosine)))))
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
            elif target in (0x1cfae0,0x1cd370,0x1cb9b0) and execute:
                registers[31]=pc+8;pc=target;continue
            elif target==0x1cfae0:
                storevec(a0,vec(a2,16));storevec(a0+0x44,[number(floats[i]) for i in [12,14,13,15]])
            elif target==0x1cffe0:
                src=registers[7]
                tiles.append({'matrix':vec(src,16),'params':[number(load(src+i)) for i in [0x44,0x48,0x50,0x4c]],'descriptor':bytes(load(a2+i,1) for i in range(144))})
                if execute:registers[31]=pc+8;pc=target;continue
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

TILES_SHIM = r"""
#include "game/em_snow.h"
#include "game/em_sdk_math_original.h"
static EmSdkMathTables g_tables;
int shim_load(const uint8_t *elf, size_t size) { return em_sdk_math_original_load_tables(elf, size, &g_tables); }
static int sine(void *ctx, float x, float *result)
{
    uint32_t fault = 0;
    (void)ctx;
    return em_sdk_math_original_0011E2A8(&g_tables, x, result, &fault) < 0 || fault ? -1 : 0;
}
int shim_tiles(EmWeather *w, const EmSnowConfig *c, float strength, const float eye[3], EmSnowTile *out)
{
    return em_snow_tiles(w, c, strength, eye, sine, NULL, out);
}
"""
PACKET_SHIM = r"""
#include "game/em_snow.h"
#include "game/em_weather_packets.h"
#include <string.h>
typedef struct { uint32_t base, size; uint8_t *bytes; } Region;
static Region *g_r; static unsigned g_n;
static uint8_t *find(void *ctx, uint32_t a, uint32_t n)
{
    (void)ctx;
    for (unsigned i = 0; i < g_n; ++i)
        if (a >= g_r[i].base && (uint64_t)a + n <= (uint64_t)g_r[i].base + g_r[i].size)
            return g_r[i].bytes + (a - g_r[i].base);
    return NULL;
}
static const uint8_t *find_ro(void *ctx, uint32_t a, uint32_t n) { return find(ctx, a, n); }
/* em_snow_runtime's per-tile path over the caller's regions. */
int shim_packets(const EmSnowTile *tiles, unsigned count, Region *r, unsigned n, uint32_t *fault)
{
    g_r = r; g_n = n;
    const EmWeatherMem m = { NULL, find_ro, find, 0x00811CC0u, 0x00814220u };
    for (unsigned i = 0; i < count; ++i) {
        uint8_t matrix[64], obj[0x90];
        uint32_t vu59[4];
        memcpy(matrix, tiles[i].matrix, sizeof matrix);
        memcpy(obj, tiles[i].descriptor, sizeof obj);
        memcpy(vu59, tiles[i].params, sizeof vu59);
        if (em_weather_packets_tile(&m, obj, vu59, matrix, fault) < 0) return -1;
    }
    return 0;
}
"""
CTX,ARENA_AT=0x811CC0,0x563810
SEED_BEAT=ROOT.parent/'Extermination/build/s87/route/10_cage_roof_roger'


class Region(C.Structure):
    _fields_=[('base',C.c_uint32),('size',C.c_uint32),('bytes',C.c_void_p)]


def build_packets_lib(outdir):
    src,lib=outdir/'packets_shim.c',outdir/'snow_packets.dylib'
    src.write_text(PACKET_SHIM)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-shared','-fPIC','-Isrc',
                    str(src),'src/game/em_snow.c','src/game/em_weather_packets.c','-o',str(lib)],
                   cwd=ROOT,check=True)
    api=C.CDLL(str(lib))
    api.shim_packets.argtypes=[C.POINTER(Tile),C.c_uint,C.POINTER(Region),C.c_uint,C.POINTER(C.c_uint32)]
    return api


def packet_seed():
    """The render context bytes 001CFAE0 / 001CFFE0 read, from route
    capture 10: the context block (its channel-3 cursor moved to an arena
    address), the P / K scratchpad copies; D_00251260 and D_0027567x come
    from the ELF image."""
    ram=(SEED_BEAT/'eeMemory.bin').read_bytes();spad=(SEED_BEAT/'scratchpad.bin').read_bytes()
    ctx=bytearray(ram[CTX:CTX+0x2580]);struct.pack_into('<I',ctx,0x1C,ARENA_AT)
    return {CTX:bytes(ctx),0x70003A40:spad[0x3A40:0x3B40]}


def compare_packets(elf,api,before,strength,eye,descriptor,native_tiles):
    seed=packet_seed();size=108*0x260
    got={}
    oracle(elf,before,strength,eye,descriptor,host_sine=False,execute=True,seed=seed,out=got)
    load=got['load']
    original=bytes(load(ARENA_AT+i,1) for i in range(size))
    cursor=load(CTX+0x1C)
    ctx=C.create_string_buffer(seed[CTX],0x2580);spad=C.create_string_buffer(seed[0x70003A40],0x100)
    arena=C.create_string_buffer(size)
    d251260=C.create_string_buffer(elf[0x251260-0x100000+0x300:0x251260-0x100000+0x300+0x80],0x80)
    regions=(Region*4)(Region(CTX,0x2580,C.cast(ctx,C.c_void_p)),Region(0x70003A40,0x100,C.cast(spad,C.c_void_p)),
                       Region(ARENA_AT,size,C.cast(arena,C.c_void_p)),Region(0x251260,0x80,C.cast(d251260,C.c_void_p)))
    fault=C.c_uint32(0)
    assert api.shim_packets(native_tiles,108,regions,4,C.byref(fault))==0,('native packets faulted',hex(fault.value))
    native_cursor=struct.unpack_from('<I',ctx.raw,0x1C)[0]
    assert native_cursor==cursor==ARENA_AT+size,('channel-3 cursor',hex(native_cursor),hex(cursor))
    if arena.raw!=original:
        at=next(i for i in range(size) if arena.raw[i]!=original[i])
        raise AssertionError(('channel-3 packets differ at tile',at//0x260,'offset',hex(at%0x260)))
    return size


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--decomp-root',type=Path,default=ROOT.parent/'Extermination')
    p.add_argument('--reference-ee',type=Path)
    p.add_argument('--reference-tiles',type=Path)
    args=p.parse_args();elf=(args.decomp_root/'config/SCUS_971.12').read_bytes()
    config=Config.from_buffer_copy((ROOT/'assets/scene_snow/snow.emsn').read_bytes()[20:])
    descriptor=bytes(config.descriptor)
    outdir=ROOT/'build/weather_reference';outdir.mkdir(exist_ok=True,parents=True)
    lib=outdir/'snow_tiles.dylib';shim=outdir/'tiles_shim.c'
    shim.write_text(TILES_SHIM)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-shared','-fPIC','-Isrc',str(shim),
                    'src/game/em_snow.c','src/game/em_sdk_math_original.c','-o',str(lib)],cwd=ROOT,check=True)
    native=C.CDLL(str(lib));emit=native.shim_tiles
    native.shim_load.argtypes=[C.c_char_p,C.c_size_t]
    assert native.shim_load(elf,len(elf))==0,'the SDK math tables did not load from the ELF'
    packets=build_packets_lib(outdir)
    emit.argtypes=[C.POINTER(Weather),C.POINTER(Config),C.c_float,C.POINTER(C.c_float),C.POINTER(Tile)]
    emit.restype=C.c_int
    rng=random.Random(0x1e67c0);cases=0;tile_count=0
    # the packet sample: every case in full, the first case of each strength in quick
    packet_cases=set(range(48)) if FULL else {0,6,12,18};packet_bytes=packet_runs=0
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
            expected,tiles,_,_=oracle(elf,before,strength,eye,descriptor,host_sine=False)
            out=(Tile*108)();assert emit(C.byref(w),C.byref(config),strength,(C.c_float*3)(*eye),out)==0
            assert bytes(w)[:68]==expected,('poststate',cases)
            assert len(tiles)==108
            for i,(actual,ref) in enumerate(zip(out,tiles)):
                for field in ['matrix','params']:
                    want=struct.pack('<'+str(len(ref[field]))+'f',*ref[field])
                    assert bytes(getattr(actual,field))==want,(field,cases,i,list(getattr(actual,field)),ref[field])
                assert bytes(actual.descriptor)==ref['descriptor'],('descriptor',cases,i)
            if cases in packet_cases:
                packet_bytes+=compare_packets(elf,packets,before,strength,eye,descriptor,out)
                packet_runs+=1
            cases+=1;tile_count+=108
    assert packet_runs==len(packet_cases),('packet cases run',packet_runs)
    banner(part(cases,48,'instruction-flow cases (every strength)'),
           part(packet_runs,48,'cases with the channel-3 packets executed')+f' ({packet_bytes:,} bytes equal)',
           'captured snapshot comparison in full when --reference-ee/--reference-tiles are given')
    report={'status':'PASS','mode':MODE,'instruction_flow_cases':cases,'tiles_compared':tile_count,
            'packet_cases':packet_runs,'packet_bytes_equal':packet_bytes,
            'sine':'0011E2A8 executed (original) and em_sdk_math_original (native)'}
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
        # 001E55F0 / 001E67C0 arithmetic is COP1: the measured EE model.
        strength=number(ee_float_model.ee_div(bits(w.intensity),bits(127.0)))
        driftstep=number(ee_float_model.ee_mul(bits(.004),bits(strength)))
        for row in range(6):
            w.phase[row]=latest[row*18]['params'][0]
            # Inverting a truncating add is not generally unique. This
            # representative is used only for a measured matrix error, not
            # asserted as the exact pre-render state.
            w.drift[row]=number(ee_float_model.ee_sub(bits(w.drift[row]),bits(driftstep)))
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
        out=(Tile*108)();assert emit(C.byref(w),C.byref(config),strength,(C.c_float*3)(*eye),out)==0
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
            step=ee_float_model.ee_mul(ee_float_model.ee_sub(bits(captured.target),candidate),bits(captured.rate))
            if number(ee_float_model.ee_add(candidate,step))==captured.intensity:
                prior.append(value)
        assert len(prior)==1
        old=Weather.from_buffer_copy(before);old.intensity=prior[0]
        old_strength=number(ee_float_model.ee_div(bits(old.intensity),bits(127.0)))
        for row in range(6):
            old.phase[row]=refs[row*18]['params'][0]
            old.drift[row]=number(ee_float_model.ee_sub(bits(old.drift[row]),
                                  ee_float_model.ee_mul(bits(.004),bits(old_strength))))
        previous_out=(Tile*108)();assert emit(C.byref(old),C.byref(config),old_strength,(C.c_float*3)(*camera_eye(267)),previous_out)==0
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
            'native_vs_original_sdk_matrix_max_error':max(abs(a.matrix[c]-b['matrix'][c])for a,b in zip(out,original_sdk)for c in range(16))})
    print(json.dumps(report))
if __name__=='__main__':main()
