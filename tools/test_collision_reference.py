"""Execute only the original integer gate blocks from the user's local ELF.

No original instructions or asset data are embedded in this diagnostic.
The small interpreter follows branches and their delay slots and stops at
the original accept/reject labels before any polygon or external call.
"""
from pathlib import Path
import json
import struct
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--decomp-root", type=Path, default=root.parent / "Extermination")
args = parser.parse_args()
elf_path = args.decomp_root / "elf/SCUS_971.12.elf"
elf = elf_path.read_bytes()
if elf[:6] != b"\x7fELF\x01\x01":
    raise ValueError("the collision reference requires the original little-endian ELF32")
phoff, = struct.unpack_from("<I", elf, 28)
phentsize, phnum = struct.unpack_from("<HH", elf, 42)
loads = []
for i in range(phnum):
    kind, off, va, _, size, *_ = struct.unpack_from("<8I", elf, phoff+i*phentsize)
    if kind == 1:
        loads.append((va, va+size, off))

def word(address):
    for start, end, off in loads:
        if start <= address < end:
            return struct.unpack_from("<I", elf, off+address-start)[0]
    raise ValueError(f"address outside ELF: {address:#x}")

def signed32(x):
    return x if x < 0x80000000 else x - 0x100000000

def gate(start, accept, reject, attr_register, attr, query_id):
    regs = [0] * 32
    regs[attr_register] = attr
    pc = start
    delayed = None
    for _ in range(100):
        if pc == accept: return True
        if pc == reject: return False
        instruction = word(pc)
        op = instruction >> 26
        rs, rt = (instruction >> 21) & 31, (instruction >> 16) & 31
        imm = instruction & 65535
        simm = imm if imm < 32768 else imm-65536
        branch = None
        if instruction == 0:
            pass
        elif op == 15:  # lui
            regs[rt] = imm << 16
        elif op == 9:  # addiu
            regs[rt] = (regs[rs] + simm) & 0xffffffff
        elif op == 10:  # slti
            regs[rt] = int(signed32(regs[rs]) < simm)
        elif op == 33:  # lh: this gate reads only the query ID
            address = (regs[rs] + simm) & 0xffffffff
            assert address == 0x7000324e, hex(address)
            regs[rt] = query_id & 0xffffffff
        elif op in (4, 5):
            equal = regs[rs] == regs[rt]
            taken = equal if op == 4 else not equal
            branch = pc + 4 + 4*simm if taken else pc+8
        else:
            raise ValueError(f"unsupported opcode {op} at {pc:#x}")
        next_pc = delayed if delayed is not None else pc+4
        delayed = branch
        regs[0] = 0
        pc = next_pc
    raise AssertionError("gate never reached a terminal")

def expected(kind, attr, query_id):
    if kind == "camera":
        return not 0x51 <= attr <= 0x53
    if attr >= 0x5a or (kind == "segment" and attr == 0x50):
        return False
    if attr == 0x51: return query_id == 0
    if attr == 0x52: return query_id == 2
    if attr == 0x53: return query_id != -1
    return True

gates = {
    "segment": (0x19d614, 0x19d698, 0x19d6f0, 4),
    "movement": (0x19cdf4, 0x19ce60, 0x19ced0, 4),
    "camera": (0x19da54, 0x19da70, 0x19dac8, 3),
}
ids = [-32768, -1, 0, 1, 2, 3, 31, 32767]
report = {"reference_cases": 0, "native_cases": 0}
for name, config in gates.items():
    for attr in range(256):
        for query_id in ids:
            result = gate(*config, attr, query_id)
            assert result == expected(name, attr, query_id), (name, attr, query_id)
            report["reference_cases"] += 1
with tempfile.TemporaryDirectory(prefix="em_collision_reference_") as directory:
    binary = Path(directory) / "collision_test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", "-I"+str(root/"src"),
                    str(root/"tests/collision_test.c"),
                    str(root/"src/game/em_collision.c"), "-lm", "-o", str(binary)],
                   check=True)
    result = subprocess.run([str(binary), "--dump-gates"], check=True,
                            capture_output=True, text=True)
    for line in result.stdout.splitlines():
        family, attr, query_id, actual = line.split()
        wanted = gate(*gates[family], int(attr), int(query_id))
        assert bool(int(actual)) == wanted, line
        report["native_cases"] += 1
assert report["native_cases"] == 2560
print("collision reference: PASS", json.dumps(report, sort_keys=True))
