"""Compare portable random state/output to instructions in the user's ELF.

Only the small SDK random leaf is executed. No original instruction bytes
are embedded in this tool or printed by it.
"""
import argparse
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf', type=Path,
                    default=ROOT.parent/'Extermination/elf/SCUS_971.12.elf')
args = parser.parse_args()
elf = args.elf.read_bytes()
assert elf[:6] == b'\x7fELF\x01\x01'
phoff, = struct.unpack_from('<I', elf, 28)
phsize, phnum = struct.unpack_from('<HH', elf, 42)
loads = []
for i in range(phnum):
    kind, off, va, _, size, *_ = struct.unpack_from('<8I', elf, phoff+i*phsize)
    if kind == 1 and size:
        loads.append((va, va+size, off))

def word(address):
    for start, end, off in loads:
        if start <= address < end:
            return struct.unpack_from('<I', elf, off+address-start)[0]
    raise ValueError(f'ELF address outside load segment: {address:#x}')

code = {pc: word(pc) for pc in range(0x122bb8, 0x122be8, 4)}

def original(seed):
    regs = [0]*32
    regs[31] = 0xf0000000
    memory = {0x24295c: 0x300000, 0x300058: seed}
    pc, delayed = 0x122bb8, None
    for _ in range(32):
        if pc == regs[31]:
            return memory[0x300058], regs[2]
        instruction = code[pc]
        op, fn = instruction >> 26, instruction & 63
        rs, rt, rd = (instruction >> 21)&31, (instruction >> 16)&31, (instruction >> 11)&31
        imm = instruction & 65535
        simm = imm if imm < 32768 else imm-65536
        branch = None
        if op == 15:
            regs[rt] = imm << 16
        elif op == 13:
            regs[rt] = regs[rs] | imm
        elif op == 35:
            regs[rt] = memory[(regs[rs]+simm)&0xffffffff]
        elif op == 43:
            memory[(regs[rs]+simm)&0xffffffff] = regs[rt]
        elif op == 9:
            regs[rt] = (regs[rs]+simm)&0xffffffff
        elif op == 0 and fn == 24:  # R5900 MULT also writes rd.
            regs[rd] = (regs[rs]*regs[rt])&0xffffffff
        elif op == 0 and fn == 36:
            regs[rd] = regs[rs] & regs[rt]
        elif op == 0 and fn == 8:
            branch = regs[rs]
        else:
            raise ValueError(f'unexpected random instruction family at {pc:#x}')
        next_pc = delayed if delayed is not None else pc+4
        delayed = branch
        regs[0] = 0
        pc = next_pc
    raise AssertionError('original random leaf did not return')

with tempfile.TemporaryDirectory(prefix='em_random_reference_') as tmp:
    library = Path(tmp)/'random.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-I'+str(ROOT/'src'),
                    str(ROOT/'src/game/em_random.c'), '-o', str(library)], check=True)
    native = ctypes.CDLL(str(library))
    native.em_random_step.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    native.em_random_step.restype = ctypes.c_uint32
    cases = 0
    for seed in [0, 1, 0x45, 0x7fffffff, 0x80000000, 0xffffffff, 0xd965de4d]:
        reference = seed
        state = ctypes.c_uint32(seed)
        for _ in range(4096):
            reference, result = original(reference)
            actual = native.em_random_step(ctypes.byref(state))
            assert (state.value, actual) == (reference, result)
            cases += 1
print(f'random reference: PASS ({cases} state/output pairs)')
