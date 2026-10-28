#!/usr/bin/env python3
"""Observe original SNDN2DRV/LIBSD initialization in the existing IOP oracle.

Only OS service imports are scripted; every reached LIBSD routine executes.
SPU2 MMIO has separate storage, never an alias of IOP RAM. Outputs are private
register evidence, not a live hardware-state capture. No module bytes or
instruction listings are emitted. Run on the project's native macOS host.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import export_spu2_effects as effects
import test_iop_stream_reference as oracle_module

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / "Extermination"
# These boundaries manage threads, interrupts, timers and RPC, not SPU2 data.
OS_BOUNDARIES = {
    "intrman": {4, 5, 6, 8, 9},
    "sifman": {5, 29},
    "sifcmd": {14, 17, 19, 22},
    "thbase": {4, 6, 20},
    "timrman": {4, 7, 11, 16},
}


def imports_in(memory):
    """Read resident IRX import metadata without producing a code listing."""
    result = {}
    for at in range(0, len(memory) - 20, 4):
        if oracle_module.u32(memory, at) != 0x41E00000:
            continue
        name = memory[at + 12:at + 20].split(b"\0")[0]
        if not name or any(x < 32 or x > 126 for x in name):
            continue
        pos = at + 20
        while (pos + 8 <= len(memory)
               and oracle_module.u32(memory, pos + 4) & 0xFFFF0000 == 0x24000000):
            result[pos] = (name.decode("ascii"), oracle_module.u32(memory, pos + 4) & 0xFFFF)
            pos += 8
    return result


def full_game_commands():
    """Read the extra master/reset requests in the pure C game initializer."""
    source = DECOMP / "src/func_001FB210.c"
    definition = source.read_text().split("void func_001FB210(void) {", 1)[1]
    names = {"00119978": 0x28, "001197F8": 0x14,
             "00119810": 0x15, "00119828": 0x16}
    result = []
    for match in re.finditer(r"func_(00119978|00119390|001197F8|00119810|00119828)\(([^)]*)\);",
                             definition):
        values = [int(x.strip(), 0) for x in match[2].split(",")]
        if match[1] == "00119390":
            wrapper = (DECOMP / "src/func_00119390.c").read_text()
            setup = re.search(r"func_001157F0\((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*arg,\s*0\)", wrapper)
            assert setup and len(values) == 1
            result.append([int(setup[1], 0), int(setup[2], 0), values[0], 0])
        else:
            result.append([names[match[1]], *values, *([0] * (3 - len(values)))])
    assert len(result) == 9
    return result


def run_initialization(oracle, module, imports, seed, game_effects=False,
                       full_game=False, restore=None):
    cpu, registers, written = effects.mmio_cpu(oracle)
    # Vary configuration only; a synthetic busy status is not a valid reset.
    # Public register layout: https://ps2dev.github.io/ps2sdk/spu2regs_8h.html
    for core in (0, 1):
        for start, end in ((0x180 + core * 0x400, 0x19A + core * 0x400),
                           (0x760 + core * 40, 0x770 + core * 40)):
            registers[start:end] = bytes([seed]) * (end - start)
    cpu.hooks.clear()
    calls, threads, libsd_calls = [], [], []
    writes = []
    save = cpu.save

    def observe_save(address, value, size, track=True):
        if address & 0x1FFFF800 == 0x1F900000:
            writes.append({"offset": address & 0x7FF, "size": size,
                           "value": value & ((1 << (8 * size)) - 1)})
        save(address, value, size, track)

    cpu.save = observe_save

    def os_hook(address, name, ordinal):
        def invoke(c):
            if ordinal not in OS_BOUNDARIES.get(name, set()):
                raise RuntimeError("unrecognized OS boundary")
            args = [x & 0xFFFFFFFF for x in c.r[4:8]]
            calls.append({"address": address, "library": name, "ordinal": ordinal, "args": args})
            c.r[2] = 0
            if (name, ordinal) == ("thbase", 4):
                pointer = args[0]
                threads.append({"entry": c.load(pointer + 8, 4),
                                "stack_size": c.load(pointer + 12, 4),
                                "priority": c.load(pointer + 16, 4)})
                c.r[2] = len(threads)
        return invoke

    for address, (name, ordinal) in imports.items():
        if name != "libsd":
            cpu.hooks[address] = os_hook(address, name, ordinal)
    fetch = cpu.fetch
    reached, visits, last_pc = set(), {}, [0]

    def observe_fetch(pc):
        last_pc[0] = pc
        reached.add(pc)
        visits[pc] = visits.get(pc, 0) + 1
        if visits[pc] > 200000:
            raise RuntimeError("bounded loop exceeded")
        if pc in imports and imports[pc][0] == "libsd":
            libsd_calls.append({"import": imports[pc][1],
                                "args": [x & 0xFFFFFFFF for x in cpu.r[4:8]]})
        return fetch(pc)

    cpu.fetch = observe_fetch
    result = {"seed_byte": seed, "status": "PASS"}
    try:
        entry = oracle.base + struct.unpack_from("<I", module, 24)[0]
        result["module_return"] = cpu.call(entry, (0, 0))
        assert len(threads) == 1
        visits.clear()
        result["rpc_thread_return"] = cpu.call(threads[0]["entry"], (0,))
        visits.clear()
        command = oracle.base + oracle_module.RPC_BUFFER
        cpu.write(command, struct.pack("<4I", 0x1E, 0, 0, 0))
        result["initialization_return"] = cpu.call(oracle.base + 0x634, (command, 16, 0))
        assert result["initialization_return"] == 0
        assert any(row["import"] == 4 for row in libsd_calls), "original SdInit did not execute"
        if game_effects:
            commands = full_game_commands() if full_game else effects.original_init()[0]
            result["game_effect_commands"] = []
            for words in commands:
                visits.clear()
                cpu.write(command, struct.pack("<4I", *words))
                before = len(writes)
                returned = cpu.call(oracle.base + 0x634, (command, 16, 0))
                assert returned == 0
                result["game_effect_commands"].append({
                    "command": words, "return": returned, "register_writes": len(writes) - before})
        if restore:
            result["post_movie_restore"] = []
            for words in restore:
                visits.clear()
                cpu.write(command, struct.pack("<4I", *words))
                before = len(writes)
                returned = cpu.call(oracle.base + 0x634, (command, 16, 0))
                assert returned == 0
                result["post_movie_restore"].append({
                    "command": words, "return": returned, "register_writes": len(writes) - before})
    except Exception as error:
        # Some interpreter exceptions contain fetched words. Keep only class/PC.
        result.update(status="FAIL", failure_class=type(error).__name__, last_pc=last_pc[0])
    result.update(os_calls=calls, threads=threads, libsd_calls=libsd_calls,
                  reached_words=len(reached), written_bytes=sorted(written), writes=writes)
    return result, bytes(registers)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=DECOMP / "Extermination-rebuilt.iso")
    ap.add_argument("--iop", type=Path, default=DECOMP / (
        "build/startup-reference/fork-states/beats/s87/route/00_panel_no_battery/state.p2s"))
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--game-effects", action="store_true",
                    help="execute the game's six initial effect commands on the same initialized CPU")
    ap.add_argument("--full-game", action="store_true",
                    help="include the game's master mute and additional reset request before effects")
    ap.add_argument("--master-restore", type=Path,
                    help="apply commands from a PASS original-master-restore receipt as a conditional post-movie state")
    args = ap.parse_args(argv)
    out = args.out.resolve()
    if (ROOT / "build").is_symlink() or ROOT / "build" not in out.parents or out.exists():
        ap.error("choose a fresh private directory inside this worktree's real build/")
    _, module = oracle_module.iso_file(args.iso.resolve(), "/IRX/SNDN2DRV.IRX")
    memory = effects.capture_iop(args.iop.resolve())
    oracle = oracle_module.DriverOracle(module, memory)
    imports = imports_in(memory)
    restore = None
    if args.master_restore:
        proof = json.loads(args.master_restore.read_text())
        assert proof["schema"] == "spu2-original-master-restore-v1" and proof["status"] == "PASS"
        assert proof["sha256"]["driver"] == hashlib.sha256(module).hexdigest()
        assert proof["sha256"]["resident_iop"] == hashlib.sha256(memory).hexdigest()
        restore = proof["restore_commands"]
        assert len(restore) == 2 and [r[:2] for r in restore] == [[0x28, 0], [0x28, 1]]
        assert all(len(r) == 4 and all(type(x) is int and 0 <= x <= 0xFFFF for x in r)
                   for r in restore)
    out.mkdir(parents=True)
    runs, registers = [], []
    for seed in (0, 0x55, 0xAA):
        row, regs = run_initialization(oracle, module, imports, seed,
                                      args.game_effects or args.full_game,
                                      args.full_game, restore)
        runs.append(row)
        registers.append(regs)
        (out / f"registers-{seed:02x}.bin").write_bytes(regs)
    complete = all(row["status"] == "PASS" for row in runs)
    common = set.intersection(*(set(row["written_bytes"]) for row in runs))
    stable = sorted(i for i in common if len({regs[i] for regs in registers}) == 1)
    result = {
        "schema": "original-spu2-initialization-v1",
        "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "status": "PASS" if complete else "FAIL",
        "driver_sha256": hashlib.sha256(module).hexdigest(),
        "resident_iop_sha256": hashlib.sha256(memory).hexdigest(),
        "oracle_driver_words_checked": oracle.checked,
        "stable_initialized_bytes": stable,
        "unstable_written_bytes": sorted(common - set(stable)),
        "runs": runs,
        "game_effects_applied": args.game_effects or args.full_game,
        "full_game_commands_applied": args.full_game,
        "game_definition_sha256": {
            name: hashlib.sha256((DECOMP / "src" / name).read_bytes()).hexdigest()
            for name in ("func_001FB210.c", "func_00119390.c")
        } if args.game_effects or args.full_game else {},
        "master_restore": None if not args.master_restore else {
            "receipt": str(args.master_restore.resolve()),
            "sha256": hashlib.sha256(args.master_restore.read_bytes()).hexdigest(),
            "scope": "Conditional post-movie state after queued restore commands are processed.",
        },
        "scope": "Original module entry, RPC thread setup and command 0x1e; "
                 "OS services scripted. Every LIBSD body executes. "
                 "Three synthetic initial mixing/volume configuration fills test "
                 "initialization dependence; status/control registers start zero.",
        "limits": ["Not a live register capture or hardware timing proof.",
                   "Apply subsequent game command writes before comparing audio.",
                   "Unwritten bytes and initial effect RAM/history are not certified.",
                   "Resident LIBSD RAM starts from the cited capture, not a cold IOP reset."],
    }
    (out / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"status": result["status"], "seed_runs": len(runs),
                      "stable_initialized_bytes": len(stable),
                      "unstable_written_bytes": len(common - set(stable))}))
    return 0 if complete else 1


if __name__ == "__main__":
    raise SystemExit(main())
