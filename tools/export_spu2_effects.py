#!/usr/bin/env python3
"""Export private SPU2 effect settings by executing the user's original driver.

No presets are embedded. Resident LIBSD writes are observed through a separate
MMIO page (not aliased onto IOP RAM). The existing IOP interpreter checks the
original module against the supplied capture. No emulator source is used.

The public register map is https://ps2dev.github.io/ps2sdk/spu2regs_8h.html .
The PSX-SPX equations supply the normalized tap names; the original mode-four
register writes independently agree with the documented Studio Large scaling.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / "Extermination"
MAGIC = b"EMRV"
VERSION = 1
HEADER_SIZE = 96
RECORD_SIZE = 124


def private_assets(path, root=ROOT):
    """Reject links and traversal for every destination before any write."""
    destination = Path(os.path.abspath(path))
    base = Path(os.path.abspath(root)) / "assets"
    if not destination.is_relative_to(base):
        raise ValueError("output must be inside this worktree's private assets")
    for candidate in (destination, *destination.parents):
        if candidate.is_symlink():
            raise ValueError("refusing a symlink in the private asset destination")
    if not destination.resolve().is_relative_to(base.resolve()):
        raise ValueError("resolved output escaped private assets")
    return destination


def destinations(path, root=ROOT):
    output = private_assets(path, root)
    metadata = private_assets(output.with_suffix(".json"), root)
    if output == metadata:
        raise ValueError("binary and metadata destinations must be different")
    return output, metadata


def signed(value):
    return value - 65536 if value & 32768 else value


def params(registers, core):
    """Normalize public SPU2 register addresses into EmSpu2ReverbParams order."""
    a, p = 0x2E0 + core * 0x400, 0x760 + core * 40
    half = lambda off: struct.unpack_from("<H", registers, off)[0]
    address = lambda off: (half(a + off) << 16) | half(a + off + 2)
    addresses = [
        address(4), address(8),                      # APF delays
        address(0xC), address(0x10),                 # SAME destinations
        address(0x24), address(0x28),                # SAME sources
        address(0x2C), address(0x30),                # DIFF destinations
        address(0x44), address(0x48),                # DIFF sources, PSX L/R order
        *[address(x) for x in (0x14, 0x1C, 0x34, 0x3C)],  # left COMBs
        *[address(x) for x in (0x18, 0x20, 0x38, 0x40)],  # right COMBs
        address(0x4C), address(0x54),                # left APFs
        address(0x50), address(0x58),                # right APFs
    ]
    coefficients = [signed(half(p + x)) for x in
                    (0x24, 0x26, 0x14, 0x1E, 0x16, 0x18, 0x1A, 0x1C, 0x20, 0x22)]
    assert len(addresses) == 22 and len(coefficients) == 10
    return addresses, coefficients


def original_init():
    """Read the C definition, not its label; retain only configuration calls."""
    path = DECOMP / "src/func_001FB210.c"
    text = path.read_text().split("void func_001FB210(void) {", 1)[1]
    commands = []
    names = {"001197F8": 0x14, "00119810": 0x15, "00119828": 0x16}
    for match in re.finditer(r"func_(001197F8|00119810|00119828)\(([^)]*)\);", text):
        values = [int(x.strip(), 0) for x in match[2].split(",")]
        commands.append([names[match[1]], *values, *([0] * (3 - len(values)))])
    assert len(commands) == 6, "initial effect configuration changed; inspect its definition"
    ends, modes, returns = {}, {}, {}
    for cmd, core, a, b in commands:
        assert core in (0, 1)
        if cmd == 0x14:
            ends[core] = a
        elif cmd == 0x15:
            modes[core] = a
        else:
            returns[core] = [a, b]
    assert set(ends) == set(modes) == set(returns) == {0, 1}
    return commands, ends, modes, returns


def mmio_cpu(oracle):
    cpu = oracle.cpu(([0] * 48, [0] * 48, 1))
    registers, written = bytearray(0x800), set()
    load, save = cpu.load, cpu.save

    def is_mmio(address):
        return (address & 0x1FFFF800) == 0x1F900000

    def read(address, n, track=True):
        if is_mmio(address):
            off = address & 0x7FF
            assert off + n <= 0x800
            return int.from_bytes(registers[off:off + n], "little")
        return load(address, n, track)

    def write(address, value, n, track=True):
        if is_mmio(address):
            off = address & 0x7FF
            assert off + n <= 0x800
            registers[off:off + n] = (value & ((1 << (8 * n)) - 1)).to_bytes(n, "little")
            written.update(range(off, off + n))
        else:
            save(address, value, n, track)

    cpu.load, cpu.save = read, write
    # Run the original libsd set-param/set-address imports. The effect setter
    # is already unhooked by DriverOracle. The clear-work-area DMA helper is
    # stubbed: it changes RAM contents, not the effect coefficients.
    for offset in (0x314C, 0x3164):
        cpu.hooks.pop(oracle.base + offset)
    cpu.hooks[oracle.base + 0x3174] = lambda c: c.r.__setitem__(2, 0)
    return cpu, registers, written


def execute(oracle, commands):
    cpu, registers, written = mmio_cpu(oracle)
    for command in commands:
        cpu.write(oracle.base + 0x34B0, struct.pack("<4I", *command))
        cpu.call(oracle.base + 0x634, (oracle.base + 0x34B0, 16, 0))
    return bytes(registers), written


def capture_iop(path):
    if path.suffix != ".p2s":
        return path.read_bytes()
    script = ("from pathlib import Path\n"
              "import sys\n"
              "from tools.parse_pcsx2_state import extract_zstd_entry\n"
              "sys.stdout.buffer.write(extract_zstd_entry(Path(sys.argv[1]), 'iopMemory.bin'))\n")
    # Captured bytes stay in this process, never in the tool's text output.
    return subprocess.run([str(DECOMP / ".venv/bin/python"), "-c", script, str(path)],
                          cwd=DECOMP, check=True, capture_output=True).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=DECOMP / "Extermination-rebuilt.iso")
    parser.add_argument("--iop", type=Path, default=DECOMP / (
        "build/startup-reference/fork-states/beats/s87/route/00_panel_no_battery/state.p2s"))
    parser.add_argument("--out", type=Path, default=ROOT / "assets/spu2/effects.emrv")
    args = parser.parse_args()
    output, metadata_output = destinations(args.out)
    spec = importlib.util.spec_from_file_location("spu2_effect_oracle",
                                                 ROOT / "tools/test_iop_stream_reference.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    _, irx = module.iso_file(args.iso, "/IRX/SNDN2DRV.IRX")
    iop = capture_iop(args.iop.resolve())
    oracle = module.DriverOracle(irx, iop)
    commands, ends, modes, returns = original_init()
    initial_regs, initial_written = execute(oracle, commands)
    digest = hashlib.sha256(irx).digest()
    capture_digest = hashlib.sha256(iop).digest()
    header = struct.pack("<4s7I", MAGIC, VERSION, HEADER_SIZE, RECORD_SIZE, 20,
                         modes[0], modes[1], 0) + digest + capture_digest
    assert len(header) == HEADER_SIZE
    rows, binary = [], bytearray(header)
    for core in range(2):
        for mode in range(10):
            regs, written = execute(oracle, [
                [0x14, core, ends[core], 0], [0x15, core, mode, 0],
                [0x16, core, *returns[core]]])
            a = 0x2E0 + 0x400 * core
            start = (int.from_bytes(regs[a:a+2], "little") << 16
                     | int.from_bytes(regs[a+2:a+4], "little"))
            end = ends[core] // 2  # 0x14 argument is an inclusive byte endpoint.
            addresses, coefficients = params(regs, core)
            assert 0 <= start <= end < 0x100000
            assert all(0 <= x < 0x100000 for x in addresses)
            # Every coefficient/tap must come from an original libsd write.
            required = set(range(a + 4, a + 0x5C))
            required.update(range(0x774 + core * 40, 0x788 + core * 40))
            assert required <= written, "effect configuration had unwritten fields"
            binary.extend(struct.pack("<4I22i10h", core, mode, start, end,
                                      *addresses, *coefficients))
            rows.append({"core": core, "mode": mode, "start": start, "end": end,
                         "addresses": addresses, "coefficients": coefficients,
                         "written_registers": {f"{x:03x}": int.from_bytes(regs[x:x+2], "little")
                                               for x in sorted(written) if x % 2 == 0}})
    assert len(binary) == HEADER_SIZE + 20 * RECORD_SIZE
    struct.pack_into("<I", binary, 28, zlib.crc32(binary[HEADER_SIZE:]))
    # Recheck both names immediately before writing, after the oracle ran.
    output, metadata_output = destinations(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(binary)
    metadata = {
        "schema": "EMRV1", "driver_sha256": digest.hex(), "iop_sha256": capture_digest.hex(),
        "asset_sha256": hashlib.sha256(binary).hexdigest(),
        "method": "original SNDN2DRV dispatcher and captured resident LIBSD",
        "register_map": "https://ps2dev.github.io/ps2sdk/spu2regs_8h.html",
        "source_init": "Extermination/src/func_001FB210.c",
        "initial_modes": [modes[0], modes[1]], "initial_return_volumes": [returns[0], returns[1]],
        "initial_written_registers": {f"{x:03x}": int.from_bytes(initial_regs[x:x+2], "little")
                                      for x in sorted(initial_written) if x % 2 == 0},
        "unknown": ["core master initialization", "MMIX initialization", "dynamic sequence mode changes"],
        "notes": ["Offsets are normalized 16-bit sample positions.",
                  "The work-area clear DMA is stubbed; no delay RAM is exported.",
                  "The raw page contains only registers written by the original calls."],
        "rows": rows,
    }
    metadata_output.write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Exported {len(rows)} effect configurations ({len(binary)} bytes) to {output}")
    print(f"Driver SHA256: {digest.hex()}")
    print(f"Asset SHA256: {metadata['asset_sha256']}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        # Do not expose oracle exception payloads that may contain instruction
        # words or original register contents.
        sys.exit(f"SPU2 effect export failed ({type(error).__name__}); no source data printed")
