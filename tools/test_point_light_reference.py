#!/usr/bin/env python3
"""Execute original lamp registration/update/fold instructions from the owner's ELF.

This bounded interpreter is an independent test oracle, not an emulator import.
It supports only the EE and VU0 macro instructions these routines execute.
Finite products/sums truncate; EE division rounds and VU division truncates.
Whole-game RNG call ordering is a separate fidelity dependency.
"""
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CONTEXT, STACK, RETURN = 0x600000, 0x700000, 0xBADF00D


def bits(value): return struct.unpack('<I', struct.pack('<f', value))[0]
def number(value): return struct.unpack('<f', struct.pack('<I', value & 0xffffffff))[0]
def signed(value, width=32):
    value &= (1 << width) - 1
    return value - (1 << width) if value >> (width-1) else value
def fp(value):
    rounded = number(bits(value))
    return number(bits(rounded)-1) if abs(rounded) > abs(value) else rounded


class Light(C.Structure):
    _fields_ = [('multiplier', C.c_float), ('adder', C.c_float),
                ('type', C.c_int32), ('handle', C.c_int32),
                ('position', C.c_float*4), ('color', C.c_float*4),
                ('angle', C.c_float*4), ('matrix', C.c_float*16)]


class Pool(C.Structure):
    _fields_ = [('next_handle', C.c_uint32), ('pending_count', C.c_int32),
                ('active', Light*32), ('pending', Light*32)]


class Oracle:
    def __init__(self, elf, rng_values=()):
        self.elf = elf
        self.mem = {}
        self.r = [0]*32
        self.f = [0]*32
        self.v = [[0]*4 for _ in range(32)]
        self.v[0][3] = bits(1.0)
        self.acc = [0.0]*4
        self.q = 0.0
        self.condition = False
        self.rng_values = iter(rng_values)
        self.rng_calls = 0
        self.calls = {}
        self.truncate_ee_division = False
        self.save(0x275670, CONTEXT)
        self.r[28], self.r[29] = 0x27d370, STACK

    def save(self, address, value, size=4):
        for i in range(size): self.mem[address+i] = value >> (i*8) & 255

    def load(self, address, size=4):
        if address not in self.mem and 0x100000 <= address < 0x275b00:
            offset = address - 0x100000 + 0x300
            return int.from_bytes(self.elf[offset:offset+size], 'little')
        return sum(self.mem.get(address+i, 0) << (i*8) for i in range(size))

    def write(self, address, data):
        for i, value in enumerate(data): self.save(address+i, value, 1)

    def read(self, address, size):
        return bytes(self.load(address+i, 1) for i in range(size))

    def load_pool(self, pool):
        self.save(CONTEXT+0x210, pool.next_handle)
        self.save(CONTEXT+0x214, pool.pending_count)
        self.write(CONTEXT+0x220, bytes(pool.active))
        self.write(CONTEXT+0x1220, bytes(pool.pending))

    def pool_bytes(self):
        return self.read(CONTEXT+0x210, 8)+self.read(CONTEXT+0x220, 8192)

    def macro(self, word):
        op, fs, ft, fd, mask = word & 63, word >> 11 & 31, word >> 16 & 31, word >> 6 & 31, word >> 21 & 15
        x, y = list(map(number, self.v[fs])), list(map(number, self.v[ft]))
        result, destination, accumulator = [0.0]*4, fd, False
        if op < 4: result = [fp(a+y[op]) for a in x]
        elif op < 8: result = [fp(a-y[op-4]) for a in x]
        elif op < 12: result = [fp(self.acc[i]+fp(x[i]*y[op-8])) for i in range(4)]
        elif 24 <= op < 28: result = [fp(a*y[op-24]) for a in x]
        elif op == 28: result = [fp(a*self.q) for a in x]
        elif op == 32: result = [fp(a+self.q) for a in x]
        elif op == 40: result = [fp(a+b) for a,b in zip(x,y)]
        elif op == 42: result = [fp(a*b) for a,b in zip(x,y)]
        elif op == 44: result = [fp(a-b) for a,b in zip(x,y)]
        elif op >= 60:
            destination = ft
            if fd == 12 and op == 60: result = x
            elif fd == 12 and op == 61: result = x[1:]+x[:1]
            elif fd == 6:
                result = [fp(a*y[op & 3]) for a in x]; accumulator = True
            elif fd == 2:
                result = [fp(self.acc[i]+fp(x[i]*y[op & 3])) for i in range(4)]; accumulator = True
            elif fd == 14 and op == 61:
                self.q = fp(math.sqrt(abs(y[word >> 23 & 3]))); return
            elif fd == 14 and op == 60:
                denominator = y[word >> 23 & 3]
                self.q = fp(x[word >> 21 & 3]/denominator) if denominator else number(0x7f7fffff)
                return
            elif fd == 14 and op == 63: return
            else: raise AssertionError(('VU special', hex(word), op, fd))
        else: raise AssertionError(('VU', hex(word), op))
        for lane in range(4):
            if mask & (8 >> lane):
                if accumulator: self.acc[lane] = result[lane]
                elif destination: self.v[destination][lane] = bits(result[lane])

    def plain(self, word):
        r,f = self.r,self.f
        op,rs,rt,rd = word >> 26,word >> 21 & 31,word >> 16 & 31,word >> 11 & 31
        imm = signed(word & 65535,16); address = (r[rs]+imm) & 0xffffffff
        if op == 0:
            fn = word & 63
            if fn == 0: r[rd] = (r[rt] << (word >> 6 & 31)) & 0xffffffff
            elif fn == 2: r[rd] = (r[rt] & 0xffffffff) >> (word >> 6 & 31)
            elif fn == 3: r[rd] = signed(r[rt]) >> (word >> 6 & 31) & 0xffffffff
            elif fn in (33,45): r[rd] = (r[rs]+r[rt]) & 0xffffffff
            elif fn == 35: r[rd] = (r[rs]-r[rt]) & 0xffffffff
            elif fn == 36: r[rd] = r[rs] & r[rt]
            elif fn == 37: r[rd] = r[rs] | r[rt]
            elif fn == 42: r[rd] = int(signed(r[rs]) < signed(r[rt]))
            else: raise AssertionError(('SPECIAL',fn))
        elif op in (8,9): r[rt] = address
        elif op == 10: r[rt] = int(signed(r[rs]) < imm)
        elif op == 12: r[rt] = r[rs] & (word & 65535)
        elif op == 13: r[rt] = r[rs] | (word & 65535)
        elif op == 15: r[rt] = (word & 65535) << 16
        elif op == 28 and word & 63 == 40:
            r[rd] = sum((((r[rs] >> (8*i) & 255)+(r[rt] >> (8*i) & 255)) & 255) << (8*i) for i in range(16))
        elif op in (30,33,35,36):
            size = {30:16,33:2,35:4,36:1}[op]
            value = self.load(address,size)
            r[rt] = signed(value,16) & 0xffffffff if op == 33 else value
        elif op in (31,40,41,43): self.save(address,r[rt],{31:16,40:1,41:2,43:4}[op])
        elif op == 49: f[rt] = self.load(address)
        elif op == 57: self.save(address,f[rt])
        elif op == 54: self.v[rt] = [self.load(address+4*i) for i in range(4)]
        elif op == 62:
            for i,value in enumerate(self.v[rt]): self.save(address+4*i,value)
        elif op == 18:
            if rs == 5: self.v[rd] = [(r[rt] >> (32*i)) & 0xffffffff for i in range(4)]
            elif rs == 1: r[rt] = sum(value << (32*i) for i,value in enumerate(self.v[rd]))
            elif rs >= 16: self.macro(word)
            else: raise AssertionError(('COP2',rs))
        elif op == 17:
            fs,fd,fn = rd,word >> 6 & 31,word & 63
            if rs == 0: r[rt] = f[fs]
            elif rs == 4: f[fs] = r[rt] & 0xffffffff
            elif rs == 20 and fn == 32: f[fd] = bits(fp(float(signed(f[fs]))))
            elif rs == 16:
                x,y = number(f[fs]),number(f[rt])
                if fn == 0: f[fd] = bits(fp(x+y))
                elif fn == 1: f[fd] = bits(fp(x-y))
                elif fn == 2: f[fd] = bits(fp(x*y))
                elif fn == 3: f[fd] = bits(fp(x/y) if self.truncate_ee_division else x/y)
                elif fn == 6: f[fd] = f[fs]
                elif fn == 50: self.condition = x == y
                elif fn == 52: self.condition = x < y
                elif fn == 54: self.condition = x <= y
                else: raise AssertionError(('FPU',fn))
            else: raise AssertionError(('COP1',rs,fn))
        else: raise AssertionError(('opcode',op,hex(word)))
        r[0] = 0

    def run(self, entry, args=(), floats=(), stop=RETURN):
        self.r[31] = RETURN
        for i,value in enumerate(args): self.r[4+i] = value
        for i,value in enumerate(floats): self.f[12+i] = bits(value)
        pc = entry
        for _ in range(100000):
            if pc == stop: return
            word = self.load(pc); op,rs,rt = word >> 26,word >> 21 & 31,word >> 16 & 31
            offset = signed(word & 65535,16)*4; branch = None
            if op in (2,3):
                target = (word & 0x3ffffff)*4
                if op == 3: self.r[31] = pc+8
                self.plain(self.load(pc+4))
                if target == 0x122bb8:
                    self.r[2] = next(self.rng_values); self.rng_calls += 1; pc += 8
                elif target in self.calls:
                    self.calls[target](self); pc += 8
                else: pc = target
                continue
            if op in (4,5,20,21):
                taken = (self.r[rs] == self.r[rt]) == (op in (4,20))
                if op in (20,21) and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op in (6,7):
                taken = signed(self.r[rs]) <= 0 if op == 6 else signed(self.r[rs]) > 0
                branch = pc+4+offset if taken else pc+8
            elif op == 1:
                assert rt in (0,1)
                taken = signed(self.r[rs]) < 0 if rt == 0 else signed(self.r[rs]) >= 0
                branch = pc+4+offset if taken else pc+8
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                if rt & 2 and not taken: pc += 8; continue
                branch = pc+4+offset if taken else pc+8
            elif op == 0 and word & 63 == 8: branch = self.r[rs]
            if branch is not None:
                self.plain(self.load(pc+4)); pc = branch
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc),error) from error
                pc += 4
        raise AssertionError('original point-light routine did not return')


def main():
    decomp = ROOT.parent/'Extermination'
    elf = (decomp/'config/SCUS_971.12').read_bytes()
    out = ROOT/'build/point_light_reference'; out.mkdir(parents=True, exist_ok=True)
    lib = out/'point_light.dylib'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-shared','-fPIC','-Isrc','src/game/em_point_light.c','-o',str(lib)],cwd=ROOT,check=True)
    native = C.CDLL(str(lib)); random_fn = C.CFUNCTYPE(C.c_uint32,C.c_void_p)
    native.em_point_light_tick.argtypes = [C.POINTER(Pool),C.c_uint16,random_fn,C.c_void_p]
    native.em_point_light_register.argtypes = [C.POINTER(Pool),C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_int32,C.c_float,C.c_float]
    native.em_point_light_fold.argtypes = [C.POINTER(C.c_float),C.POINTER(C.c_float),C.POINTER(Pool),C.POINTER(C.c_float)]
    rng = random.Random(0x1d7c30); cases = calls = folds = 0
    for case in range(256):
        pool = Pool()
        for light in list(pool.active)+list(pool.pending):
            light.multiplier = rng.choice([1,1,.6,.95,0])
            light.adder = rng.choice([0,-1,-.05,.125])
            light.type = rng.randrange(3); light.handle = rng.randrange(200)
            for lane in range(4):
                light.color[lane] = rng.uniform(.01,2000) if rng.randrange(3) else 0
                light.position[lane] = rng.uniform(-1000,1000)
                light.angle[lane] = rng.uniform(-.04,.04)
            for lane in range(16): light.matrix[lane] = rng.uniform(-1,1)
        pool.next_handle = rng.randrange(10000); pool.pending_count = rng.randrange(33)
        key = [0x0b00,0x0f00][case & 1]
        randoms = [rng.randrange(0x80000000) for _ in range(64)]
        oracle = Oracle(elf,randoms); oracle.load_pool(pool)
        oracle.save(0x810700,key >> 8,1); oracle.save(0x810701,key & 255,1)
        oracle.run(0x1d7c30)
        values = iter(randoms); used = []
        @random_fn
        def next_random(_):
            value = next(values); used.append(value); return value
        native.em_point_light_tick(C.byref(pool),key,next_random,None)
        expected = oracle.pool_bytes()
        assert bytes(pool) == expected, ('pool',case,next(i for i,(a,b) in enumerate(zip(bytes(pool),expected)) if a != b))
        assert len(used) == oracle.rng_calls
        calls += len(used); cases += 1
        direction = (C.c_float*4)(*(rng.uniform(-20,20) for _ in range(3)),0)
        color = (C.c_float*4)(*(rng.uniform(0,128) for _ in range(3)),16)
        anchor = (C.c_float*4)(*(rng.uniform(-1000,1000) for _ in range(3)),1)
        fold = Oracle(elf); fold.load_pool(pool)
        fold.save(0x275688,0x500000); fold.r[20] = 0x500200
        fold.write(STACK+0x150,bytes(direction)); fold.write(STACK+0x160,bytes(color))
        fold.write(0x500200,bytes(anchor)); fold.run(0x1d8534,stop=0x1d8654)
        native.em_point_light_fold(direction,color,C.byref(pool),anchor)
        assert bytes(direction) == fold.read(0x5000c0,16), ('fold direction',case,list(direction),struct.unpack('<4f',fold.read(0x5000c0,16)))
        assert bytes(color) == fold.read(0x5000f0,16), ('fold color',case)
        folds += 1
    # Room reset executes both original selectors and their actual lifecycle
    # calls. Key0B00 selects the secondary list at0025D5A0, omitted previously.
    reset = Oracle(elf); reset.load_pool(pool)
    reset.save(0x810700,11,1); reset.save(0x810701,0,1)
    reset.run(0x1d7bb0)
    native.em_point_light_reset(C.byref(pool))
    position = (C.c_float*4)(*struct.unpack('<3f',elf[0x25d5ac-0x100000+0x300:0x25d5b8-0x100000+0x300]),1)
    color = (C.c_float*4).from_buffer_copy(elf[0x26eb90-0x100000+0x300:0x26eba0-0x100000+0x300])
    native.em_point_light_register(C.byref(pool),position,color,1,1,0)
    assert bytes(pool) == reset.pool_bytes(), 'room reset and auxiliary registration'
    # Registration capacity, preserved unrelated bytes and allocator wrap.
    registrations = 0
    pool.next_handle = 0xfffffff0
    register = Oracle(elf); register.load_pool(pool)
    register.write(0x500000,bytes(position)); register.write(0x500010,bytes(color))
    for i in range(40):
        register.run(0x1d7fa0,[0x500000,0x500010,1],[1,0])
        result = native.em_point_light_register(C.byref(pool),position,color,1,1,0)
        assert result == signed(register.r[2]) and bytes(pool) == register.pool_bytes(), ('register',i)
        registrations += 1
    ram = (decomp/'build/startup-reference/opening_ee.bin').read_bytes()
    context = struct.unpack_from('<I',ram,0x275670)[0]
    captured = Light.from_buffer_copy(ram[context+0x220:context+0x2a0])
    captured_pool = Pool()
    C.memmove(C.addressof(captured_pool.active),ram[context+0x220:context+0x1220],4096)
    player_node = struct.unpack_from('<I',ram,0x8102b0+0x114)[0]
    captured_colors = []
    for truncate_division in (False,True):
        fold = Oracle(elf); fold.load_pool(captured_pool)
        fold.truncate_ee_division = truncate_division
        fold.save(0x275688,0x500000); fold.r[20] = 0x500200
        fold.write(0x500200,ram[player_node+0xc0:player_node+0xd0])
        fold.write(STACK+0x150,bytes(16)); fold.write(STACK+0x160,bytes(16))
        fold.run(0x1d8534,stop=0x1d8654)
        captured_colors.append(fold.read(0x5000f0,12))
    # The unique current Dennis body matrix in this immutable snapshot.
    assert captured_colors[0] == ram[0x2fe060:0x2fe06c]
    assert ram.count(captured_colors[0]) == 1
    matrix = Oracle(elf); matrix.write(0x500000, bytes(captured.matrix))
    matrix.run(0x1029c0,[0x500000]); matrix.run(0x102b08,[0x500000,0x500000],[captured.angle[0]])
    matrix.run(0x102bb0,[0x500040,0x500000],[captured.angle[1]])
    assert matrix.read(0x500040,64) == bytes(captured.matrix), 'captured flicker matrix'
    report = {'status':'PASS','original_update_cases':cases,'original_random_calls':calls,
              'original_fold_cases':folds,'original_registration_cases':registrations,
              'original_room_reset_and_auxiliary_registration':True,
              'captured_flicker_matrix_bytes_equal':64,'original_elf_sha256':hashlib.sha256(elf).hexdigest()}
    report['captured_player_point_color_bytes_equal'] = 12
    report['captured_player_color_distinguishes_ee_division_rounding'] = captured_colors[0] != captured_colors[1]
    report['original_opening_ee_sha256'] = hashlib.sha256(ram).hexdigest()
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
