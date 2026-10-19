#!/usr/bin/env python3
"""AREA03 arrival assets against original delivered bytes and native loaders.

Quick mode checks the arrival; full mode includes the elevator snapshot.
The strict earlier-area readers are reused per category, without their
AREA13-specific census pins. Every non-constant collision hull is still
derived by the original instruction oracle. Later gameplay is not bound.
"""
import contextlib
import copy
import ctypes
import io
import json
import struct
from types import SimpleNamespace

import test_area13_assets_reference as R
import export_area03_common as A
from reference_mode import FULL, banner

C, T = A.C, R.T01
OUT = A.SCRATCH/'reference'


def setup():
    A.install()
    every = C.captures()
    caps = every if FULL else every[:1]
    target = A.A13.current()
    R.A, R.BUILD, T.BUILD = A.OUT, OUT, OUT/'loaders'
    R.GRID['area03'] = C.u32(every[0].ram, 0x28A598)
    R.STALE_5A4 = A.LV.previous_word('area03', 0x28A5A4)
    assert R.STALE_5A4 is not None
    T.OVERLAY_SHA256 = target.overlay_sha256
    T._DISC.clear()
    image_map, info = C.build_load_map(every[0])
    k = SimpleNamespace(t=target, el=A.LV.L.load_export_level(), elf=C.read_elf(),
                        ov=A.A13.read_overlay(target), first=every[0], every=every,
                        lmap=image_map, info=info, image=C.LoadedImage(image_map))
    return k, caps


def table_problems(k, caps):
    sub = A.OUT/'sub0'
    report = json.loads((A.OUT/'tables.json').read_text())
    read = T.disc_reader()
    roster = (sub/'roster.emro').read_bytes()
    problems = []
    if roster != T.roster_image(read):
        problems.append('roster differs from independent disc-table walk')
    for cap in caps:
        if not T.roster_equal(roster, cap.ram):
            problems.append('roster differs from original RAM')
        problems += R.field_problems(roster, cap.ram)
    for name, layout in (('spawn_table.emsp', T.spawn_layout),
                         ('door_destinations.emsp', T.doors_layout)):
        problems += T.emsp_problems(name, (A.OUT/name).read_bytes(), caps, layout(read))[0]
    problems += R.spawn_entry_problems(caps)
    problems += R.script_entry_problems(k.t, report)
    problems += R.door_behaviour_problems(k.t)
    lo, hi = A.TB.E02.overlay_data_window(k.ov)
    original = k.ov[lo-C.OVERLAY_ARENA:hi-C.OVERLAY_ARENA]
    records = A.TB.chain_records(original, lo)
    start, end = A.TB.scripts_window(k.ov)
    for label, name, begin, stop, metadata in (
            ('scripts', 'scripts.emsc', start, end, 'scripts'),
            ('overlay data', 'overlay_data.emsc', lo, hi, 'overlay_data')):
        problems += R.window_problems(label, (A.OUT/name).read_bytes(), (begin, stop-begin),
            k.ov[begin-C.OVERLAY_ARENA:stop-C.OVERLAY_ARENA], caps, records,
            report[metadata]['run_time_words'])
    problems += T.messages_problems((sub/'message_data.emmd').read_bytes(), caps, k.elf)[0]
    problems += T.world_models_problems((sub/'world_models.emwm').read_bytes(), caps)[0]
    door_row = read(A.TB.DOOR_ROW['area03'], 4*report['doors']['records'])
    problems += R.doors_used_problems(roster, door_row, report['doors']['used'], caps)
    return problems


def sound_problems(k, caps):
    sub = A.OUT/'sub0/sfx'
    data = (sub/'area03_banks.bin').read_bytes()
    _, disc, _, _, _ = A.SX.area_container(k.first)
    problems = []
    if data != disc:
        problems.append('sound container differs from disc source')
    global_name = A.SX.X.GLOBAL_CONTAINER
    global_data = (C.DECOMP/global_name).read_bytes()
    name = 'area03_sub0_banks.bin'
    containers = {global_name: (global_data, A.SX.X.A.parse_container(global_data)),
                  name: (disc, A.SX.X.A.parse_container(disc))}
    bound, _, _ = A.SX.S.bindings_from_captures(caps, containers)
    registry = (sub/'sfx_registry.emsr').read_bytes()
    for cap in caps:
        expected, _, _, _ = T.registry_from_ram(cap.ram, bound, {global_name: global_data, name: disc})
        if registry != expected:
            problems.append('sound registry differs from original RAM re-derivation')
    return problems


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    k, caps = setup()
    sub = A.OUT/'sub0'
    cells = (sub/'area03_cells.bin').read_bytes()
    cell_report = json.loads((sub/'cells.json').read_text())
    problems = R.load_problems(k, caps, len(cells))
    p, level = R.level_problems(k, caps, sub)
    problems += p
    problems += R.emcl_problems(k, (sub/'area03.emcl').read_bytes(), caps, len(cells))
    problems += R.cells_problems(k, cells, caps, cell_report)
    problems += table_problems(k, caps)
    problems += sound_problems(k, caps)
    room = A.check_ctx_block(caps, k.elf)
    with contextlib.redirect_stdout(io.StringIO()):
        loaders = R.check_loaders(T.build_loaders(), R.background_lib(), k.t, A.OUT)
    problems += ['native loader rejected '+name for name, ok in loaders.items() if not ok]

    # Counterexamples exercise data comparisons, not only file parsers.
    mutants = {}
    bank = bytearray((sub/'level/static_bank.emsc').read_bytes())
    bank[-1] ^= 1
    mutants['bank byte'] = bool(R.bank_problems(bytes(bank), caps))
    changed = bytearray(cells)
    changed[-1] ^= 1
    mutants['cell byte'] = bool(R.cells_problems(k, bytes(changed), caps, cell_report))
    models = bytearray((sub/'world_models.emwm').read_bytes())
    models[-1] ^= 1
    mutants['model byte'] = bool(T.world_models_problems(bytes(models), caps)[0])
    cap = copy.copy(caps[0])
    cap.ram = bytearray(cap.ram)
    cap.ram[0x251C20] ^= 1
    try:
        A.check_ctx_block([cap], k.elf)
    except ValueError:
        mutants['retained colour'] = True
    else:
        mutants['retained colour'] = False
    bad = OUT/'truncated-roster.emro'
    bad.write_bytes((sub/'roster.emro').read_bytes()[:20])
    try:
        lib = T.build_loaders()
        lib.em_actor_roster_load.restype = ctypes.c_int
        mutants['truncated roster'] = lib.em_actor_roster_load(
            ctypes.create_string_buffer(16 << 20), str(bad).encode()) != 0
    finally:
        bad.unlink()
    problems += ['mutant accepted: '+name for name, ok in mutants.items() if not ok]
    report = dict(captures=len(caps), level=level, native_loaders=loaders,
                  mutants_rejected=mutants, room_captures=len(room), problems=problems,
                  scope='recorded AREA03 sub-0 asset delivery; no live binding')
    report_path = OUT/('report-full.json' if FULL else 'report-quick.json')
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    if problems:
        raise SystemExit(f'AREA03 assets: FAIL ({len(problems)} problems; private report {report_path})')
    banner(f'{len(caps)} of {len(k.every)} AREA03 snapshots')
    print(f'AREA03 assets: PASS; {len(loaders)} native loaders, {len(mutants)} rejected mutations')


if __name__ == '__main__':
    main()
