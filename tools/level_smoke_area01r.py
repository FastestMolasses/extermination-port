"""AREA01 revisit recorded input and strict scene observations, fork by default.

The revisit's main line (FOURTH_LEVEL_ROUTE.md section 4; docs/AREA01_REVISIT.md):
a01r_00 (back to the train room), a01r_01 (the event: D_008107DF 1 .. 0xFF,
D_0081075F = 0xFF, item 0x20) and a01r_02 (the bridge: D_008107E0 -> 0xFF,
D_00810760 = 0xFF). a01r_00 starts where AREA00's a00_10 return ends.
The side beats (FOURTH_LEVEL_ROUTE.md section 4) branch from a main beat's
end: a01r_s0 (the event's pickup) from a01r_01, a01r_s1 (door [14],
locked) from a01r_02; a run --until a side plays the main line to its
source, then only the side. The main line goes on through AREA02, AREA04,
AREA22, AREA01's upper floor and AREA06's first walk (a06_00).
Preparation exports pads only. A native route is certified only by the row
comparisons below; they do not certify rendered pixels.
"""
import hashlib
import json
import os
import re
import struct

import level_smoke_area00 as a00
import level_smoke_area01 as a01
import reference_root as refs

ROUTE_DIR = refs.capture('s87/route_a01r')
# AREA02's beats follow the revisit on the same native path (their pads in
# the same EM_LEVEL4_PAD_DIR); their recordings are route_a02's.
A02_ROUTE_DIR = refs.capture('s87/route_a02')
# AREA04's main line (FIFTH_LEVEL_ROUTE.md) follows a02_05 on the same path.
A04_ROUTE_DIR = refs.capture('s87/route_a04')
# AREA22 (SIXTH_LEVEL_ROUTE.md) follows a04_05, then AREA01's upper floor and
# AREA06 (SEVENTH_LEVEL_ROUTE.md); their recordings are their route sets'.
A22_ROUTE_DIR = refs.capture('s87/route_a22')
A01U_ROUTE_DIR = refs.capture('s87/route_a01u')
A06_ROUTE_DIR = refs.capture('s87/route_a06')
A06B_ROUTE_DIR = refs.capture('s87/route_a06b')
A01V_ROUTE_DIR = refs.capture('s87/route_a01v')
A22B_ROUTE_DIR = refs.capture('s87/route_a22b')
# The bed branch (EIGHTH_LEVEL_ROUTE.md "Fork continuation work"): AREA04's
# second visit is recorded only on the fork continuation, which leaves the
# canonical line at the bed (a02_s0) with health 100 and replays a02_00 ..
# a22b_01 from there. A run --until a04b_* plays the bed, then those beats
# against this set; any other run plays the canonical recordings.
BED_ROOT = refs.capture('levels-recordings-20261018-10/s87')
# The pistol fight continues a04b_03's end (EIGHTH_LEVEL_ROUTE.md section 8).
BOSS_ROUTE_DIR = refs.capture('boss_fight_20261030/s87/route_a04b')
BED = os.environ.get('EM_LEVEL_SMOKE_UNTIL', '').startswith('a04b_')
MAIN_BEATS = {'a01r_00': 'a01r_00_to_train_room', 'a01r_01': 'a01r_01_event', 'a01r_02': 'a01r_02_bridge',
              'a01r_03': 'a01r_03_door16', 'a02_00': 'a02_00_duct', 'a02_01': 'a02_01_switch',
              'a02_02': 'a02_02_ladder_escape', 'a02_03': 'a02_03_over_wreck', 'a02_04': 'a02_04_panel',
              'a02_05': 'a02_05_progression_exit', 'a04_00': 'a04_00_door45_event', 'a04_01': 'a04_01_door40',
              'a04_02': 'a04_02_console', 'a04_03': 'a04_03_back_to_hall', 'a04_04': 'a04_04_conveyor',
              'a04_05': 'a04_05_progression_exit', 'a22_00': 'a22_00_door7', 'a22_01': 'a22_01_corridor',
              'a22_02': 'a22_02_progression_exit', 'a01u_00': 'a01u_00_gap_jump',
              'a01u_01': 'a01u_01_catwalk_event', 'a01u_02': 'a01u_02_progression_exit',
              'a06_00': 'a06_00_beam', 'a06_01': 'a06_01_crate_door2', 'a06_02': 'a06_02_keypad',
              'a06_03': 'a06_03_room_out', 'a06b_00': 'a06b_00_trigger_jump', 'a06b_01': 'a06b_01_door1',
              'a01v_00': 'a01v_00_catwalk_east', 'a01v_01': 'a01v_01_gap_jump', 'a01v_02': 'a01v_02_door18',
              'a22b_00': 'a22b_00_to_door7', 'a22b_01': 'a22b_01_door6', 'a04b_00': 'a04b_00_door45',
              'a04b_01': 'a04b_01_npc', 'a04b_02': 'a04b_02_door45_out', 'a04b_03': 'a04b_03_reader'}
SIDE_BEATS = {'a01r_s0': 'a01r_s0_pickup', 'a01r_s1': 'a01r_s1_door14_locked', 'a02_s0': 'a02_s0_mts_bed',
              # HEALING's item 0x20 from the menu (tools/capture_heal_beats.py)
              'a01r_s2': 'a01r_s2_menu_cure_none', 'a02_s1': 'a02_s1_menu_cure',
              # the beam's collapse, away from the story (SEVENTH_LEVEL_ROUTE.md)
              'a06_04': 'a06_04_beam_collapse', 'a06_05': 'a06_05_door3_locked',
              # from a04b_03's end: the route's own lift ride past the live
              # creature, and the pistol recording: the fight, the drop, the lift
              'a04b_04': 'a04b_04_lift', 'a04b_s5': 'a04b_s5_fight', 'a04b_s6': 'a04b_s6_drop',
              'a04b_s7': 'a04b_s7_lift'}
# A side that starts from another side's end (Phase.from_side).
FROM_SIDE = {'a06_05': 'a06_04', 'a04b_s6': 'a04b_s5', 'a04b_s7': 'a04b_s6'}
# AREA13 is not hosted yet: these beats' rows end before the load starts.
ROW_LIMIT = {'a04b_04': 1436, 'a04b_s7': 1763}
# em_level_smoke_test.c's phase order: each side right after its source.
ROUTE_PHASES = ('a01r_00', 'a01r_01', 'a01r_s0', 'a01r_02', 'a01r_s1', 'a01r_s2', 'a01r_03', 'a02_s0', 'a02_s1',
                'a02_00', 'a02_01', 'a02_02', 'a02_03', 'a02_04', 'a02_05', 'a04_00', 'a04_01', 'a04_02', 'a04_03',
                'a04_04', 'a04_05', 'a22_00', 'a22_01', 'a22_02', 'a01u_00', 'a01u_01', 'a01u_02', 'a06_00',
                'a06_01', 'a06_02', 'a06_03', 'a06_04', 'a06_05', 'a06b_00', 'a06b_01', 'a01v_00', 'a01v_01',
                'a01v_02', 'a22b_00', 'a22b_01', 'a04b_00', 'a04b_01', 'a04b_02', 'a04b_03', 'a04b_04',
                'a04b_s5', 'a04b_s6', 'a04b_s7')
# The phases the bed branch's recordings replace, and its own sides.
BED_PHASES = tuple(p for p in ROUTE_PHASES if (p not in SIDE_BEATS and not p.startswith('a01r_')) or
                   p.startswith('a04b_'))
SOURCES = {'a01r_00': 'a00_10', 'a01r_01': 'a01r_00', 'a01r_02': 'a01r_01', 'a01r_03': 'a01r_02',
           'a01r_s0': 'a01r_01', 'a01r_s1': 'a01r_02', 'a01r_s2': 'a01r_02', 'a02_00': 'a01r_03',
           'a02_s0': 'a01r_03', 'a02_s1': 'a01r_03',
           'a02_01': 'a02_00', 'a02_02': 'a02_01', 'a02_03': 'a02_02', 'a02_04': 'a02_03',
           'a02_05': 'a02_04', 'a04_00': 'a02_05', 'a04_01': 'a04_00', 'a04_02': 'a04_01', 'a04_03': 'a04_02',
           'a04_04': 'a04_03', 'a04_05': 'a04_04', 'a22_00': 'a04_05', 'a22_01': 'a22_00', 'a22_02': 'a22_01',
           'a01u_00': 'a22_02', 'a01u_01': 'a01u_00', 'a01u_02': 'a01u_01', 'a06_00': 'a01u_02',
           'a06_01': 'a06_00', 'a06_02': 'a06_01', 'a06_03': 'a06_02', 'a06_04': 'a06_03', 'a06_05': 'a06_04',
           'a06b_00': 'a06_03', 'a06b_01': 'a06b_00', 'a01v_00': 'a06b_01', 'a01v_01': 'a01v_00',
           'a01v_02': 'a01v_01', 'a22b_00': 'a01v_02', 'a22b_01': 'a22b_00', 'a04b_00': 'a22b_01',
           'a04b_01': 'a04b_00', 'a04b_02': 'a04b_01', 'a04b_03': 'a04b_02', 'a04b_04': 'a04b_03',
           'a04b_s5': 'a04b_03', 'a04b_s6': 'a04b_s5', 'a04b_s7': 'a04b_s6'}
# FOURTH_LEVEL_ROUTE.md section 1.1 (measured in the a00_10 end snapshot;
# route_capture checks every one against each beat's saved RAM).
OWNERS = dict(shaft_door_r12=0x7ADD60, r13_158D30=0x7AE050, door_r14=0x7AE340, door_r15=0x7AE630,
              door_r16=0x7AE920, bridge_r41=0x7B3290, bridge_r42=0x7B3580, r45_8267C0=0x7B3E50,
              g5_825950=0x7A64F0, g7_825950=0x7A6AD0, g6_826CF0=0x7A67E0, g0_823CD0=0x7A5640,
              g1_823CD0=0x7A5930, g2_823CD0=0x7A5C20, g3_823CD0=0x7A5F10, g4_823CD0=0x7A6200)
# FOURTH_LEVEL_ROUTE.md section 1.1, A02_OWNERS: the sub-1 table s1_* and
# the sub-0 table s0_* (the loads reuse the pool addresses; every AREA02 row
# samples both). The tick log's "a02" observation (em_scene_bindings.c
# log_area02) holds the same nodes.
A02_OWNERS = dict(s1_pick_g0=0x7A5640, s1_pick_g1=0x7A5930, s1_pick_g2=0x7A5C20, s1_bed_r0=0x7A5F10,
                  s1_r1_1C4AF0=0x7A6200, s1_r2_159970=0x7A64F0, s1_save_r3=0x7A67E0, s1_panel_r4=0x7A6AD0,
                  s1_door_r5=0x7A6DC0, s1_door_r6=0x7A70B0, s0_lock_r21=0x7AD780, s0_door_r22=0x7ADA70,
                  s0_panel_r24=0x7AE050, s0_door_r25=0x7AE340, s0_lamp_r26=0x7AE630, s0_r27_825100=0x7AE920,
                  s0_turntable_r28=0x7AEC10, s0_r31_823980=0x7AF4E0, s0_k7_r32=0x7AF7D0, s0_k8_r33=0x7AFAC0,
                  s0_k11_r34=0x7AFDB0, s0_k10_r35=0x7B00A0, s0_gate_r36=0x7B0390, s0_gate_r37=0x7B0680,
                  s0_gate_r38=0x7B0970)
# FIFTH_LEVEL_ROUTE.md section 1.1, A04_OWNERS: the AREA04 sub-0 load's pool
# nodes (measured in the a02_05 end snapshot). The tick log's "a04"
# observation (em_scene_bindings.c log_area04) holds the same nodes.
A04_OWNERS = dict(director_r1=0x7A9FB0, npc_r2=0x7AA2A0, r3_8246B0=0x7AA590, r4_824DC0=0x7AA880,
                  door35_r35=0x7B0390, door37_r37=0x7B0970, door38_r38=0x7B0C60, door40_r40=0x7B1240,
                  door42_r42=0x7B1820, door45_r45=0x7B20F0, r47_823EE0=0x7B26D0, lock_r48=0x7B29C0,
                  door49_r49=0x7B2CB0, lift_r51=0x7B3290, lift_r62=0x7B52E0, console_r69=0x7B6770,
                  reel_r70=0x7B6A60, pick_g8=0x7A6DC0, pick_g12=0x7A7690)
# SIXTH_LEVEL_ROUTE.md section 1.1, A22_OWNERS (the a04_05 end snapshot); the
# tick log's "a22" observation (em_scene_bindings.c log_area22).
A22_OWNERS = dict(pick_g0=0x7A5640, pick_g1=0x7A5930, pick_g2=0x7A5C20, pick_g3=0x7A5F10, pick_g4=0x7A6200,
                  bug_g6=0x7A67E0, bug_g10=0x7A73A0, door6_r6=0x7A9FB0, door7_r7=0x7AA2A0, door8_r8=0x7AA590,
                  reader_r9=0x7AA880, door10_r10=0x7AAB70, lamp_r11=0x7AAE60, r13_158D30=0x7AB440,
                  r17_156F30=0x7AC000, r18_156F30=0x7AC2F0, r19_1C1A80=0x7AC5E0, r20_1C1A80=0x7AC8D0)
# SEVENTH_LEVEL_ROUTE.md section 1.1, A01U_OWNERS (the a22_02 arrival's AREA01
# load) and A06_OWNERS (the a01u_02 end snapshot); the tick log's "a01u" and
# "a06" observations (log_area01u, log_area06).
A01U_OWNERS = dict(door18_r18=0x7AFAC0, door19_r19=0x7AFDB0, pick_g11=0x7A7690, pick_g13=0x7A7C70,
                   n_g36_1C02E0=0x7ABA20, n_1BFFD0=0x7B8DA0, bug_g30=0x7AA880, bug_g31=0x7AAB70,
                   bug_g32=0x7AAE60, bug_g33=0x7AB150, bug_g34=0x7AB440, bug_g35=0x7AB730)
A06_OWNERS = dict(door1_r1=0x7AB730, door2_r2=0x7ABA20, door3_r3=0x7ABD10, lamp_r4=0x7AC000, keypad_r6=0x7AC5E0,
                  r7_1C50B0=0x7AC8D0, r8_823B50=0x7ACBC0, r10_22DCD0=0x7AD1A0, beam_r11=0x7AD490,
                  crate_r31=0x7B0C60, save_r43=0x7B2FA0, r47_1E7D20=0x7B3B60, r54_15A2C0=0x7B4FF0,
                  r56_1C1A80=0x7B55D0, doc_g2=0x7A5C20, pick_g8=0x7A6DC0, g11_219870=0x7A7690,
                  bug_g27=0x7AA590, bug_g28=0x7AA880, bug_g29=0x7AAB70)
# EIGHTH_LEVEL_ROUTE.md section 1.1 (a06b uses A06_OWNERS): A01V_OWNERS, the
# AREA01 load of a06b_01's arrival; A22B_OWNERS, the AREA22 load of a01v_02's;
# A04B_OWNERS, the AREA04 load of a22b_01's. The tick log's "a01v", "a22b" and
# "a04r" observations.
A01V_OWNERS = dict(door18_r18=0x7AEC10, door19_r19=0x7AEF00, r45_8267C0=0x7B3B60, pick_g11=0x7A7690,
                   gap_g36_1BF6B0=0x7AAB70, n_1BFFD0=0x7A6200, n_1BE6C0=0x7A6AD0, bug_a=0x7B7EF0,
                   bug_b=0x7B81E0, bug_c=0x7B84D0, bug_g30=0x7AA880)
A22B_OWNERS = dict(pick_g0=0x7A5640, pick_g4=0x7A6200, door6_r6=0x7A9CC0, door7_r7=0x7A9FB0, door8_r8=0x7AA2A0,
                   reader_r9=0x7AA590, door10_r10=0x7AA880, lamp_r11=0x7AAB70)
A04B_OWNERS = dict(npc_r2=0x7AA2A0, r3_8246B0=0x7AA590, r4_824DC0=0x7AA880, door37_r37=0x7B0970,
                   door45_r45=0x7B20F0, r47_823EE0=0x7B26D0, reader_r50=0x7B2FA0, lift_r51=0x7B3290,
                   button_r53=0x7B3870, button_r54=0x7B3B60, lift_r56=0x7B4140, socket_r55=0x7B3E50,
                   button_r58=0x7B4720, reel_r70=0x7B6A60)
# The pistol recording's rows add the creature and the four bugs event [3]
# spawns (tools/capture_boss_fight.py: this branch's nodes).
A04S_OWNERS = dict(A04B_OWNERS, creature=0x7C2660, bug_a=0x7C2080, bug_b=0x7C2370, bug_c=0x7C3800, bug_d=0x7C3AF0)
# The tick log's keys that carry AREA04's extra bytes (compare_route_row);
# AREA22 on adds D_00810768..6B, D_00810857 and the counts of items 0x23 / 0x24.
LATER_KEYS = ('a22', 'a01u', 'a06', 'a01v', 'a22b', 'a04r')


def owners_of(phase):
    """The recorded owner table and the tick log's observation key."""
    if phase.startswith('a04b_s'):
        return A04S_OWNERS, 'a04r'
    if phase.startswith('a04b_'):
        return A04B_OWNERS, 'a04r'
    if phase.startswith('a22b_'):
        return A22B_OWNERS, 'a22b'
    if phase.startswith('a01v_'):
        return A01V_OWNERS, 'a01v'
    if phase.startswith(('a06_', 'a06b_')):
        return A06_OWNERS, 'a06'
    if phase.startswith('a01u_'):
        return A01U_OWNERS, 'a01u'
    if phase.startswith('a22_'):
        return A22_OWNERS, 'a22'
    if phase.startswith('a04_'):
        return A04_OWNERS, 'a04'
    return (A02_OWNERS, 'a02') if phase.startswith('a02_') else (OWNERS, 'a01r')


# The fork recorder's input corrections (trace metadata v263_pad_corrections).
PAD_CORRECTIONS = {'a01r_01': [('late', 0, 5)], 'a02_02': [('late', 0, 2)]}


def capture_path(phase):
    if phase == 'a00_10':
        return a00.capture_path(phase)
    prefix = phase.split('_')[0]
    folder = {'a02': A02_ROUTE_DIR, 'a04': A04_ROUTE_DIR, 'a22': A22_ROUTE_DIR, 'a01u': A01U_ROUTE_DIR,
              'a06': A06_ROUTE_DIR, 'a06b': A06B_ROUTE_DIR, 'a01v': A01V_ROUTE_DIR,
              'a22b': A22B_ROUTE_DIR}.get(prefix, ROUTE_DIR)
    if phase.startswith('a04b_s'):
        folder = BOSS_ROUTE_DIR
    elif phase.startswith('a04b_') or (BED and phase in BED_PHASES):
        folder = BED_ROOT / f'route_{prefix}'
    return folder / (MAIN_BEATS | SIDE_BEATS)[phase] / 'trace.json'


def revisit_path(until):
    """The revisit phases a run --until `until` plays: the main line up to
    it, without the side beats it passes (a side: then only that side)."""
    assert until in ROUTE_PHASES, ('unknown AREA01 revisit phase', until)
    chain, side = {until}, until
    while side in FROM_SIDE:
        side = FROM_SIDE[side]
        chain.add(side)
    if until.startswith('a04b_'):       # the bed branch leaves the main line at a02_s0
        chain.add('a02_s0')
    return tuple(p for p in ROUTE_PHASES[:ROUTE_PHASES.index(until) + 1] if p not in SIDE_BEATS or p in chain)


def phase_path(until):
    """The native path: New Game through AREA00's a00_10, then the revisit."""
    return (*a00.phase_path('a00_10'), *revisit_path(until))


def source_of(phase):
    return 'a02_s0' if BED and phase == 'a02_00' else SOURCES[phase]


def source_capture(phase):
    source = source_of(phase)
    return a00.route_capture(source) if source == 'a00_10' else route_capture(source)


def route_capture(phase):
    path = capture_path(phase)
    capture = json.loads(path.read_text())
    if phase in ROW_LIMIT:
        limit = ROW_LIMIT[phase]
        capture.update(rows=capture['rows'][:limit + 1], frames=limit, last_counter=capture['rows'][limit]['counter'],
                       inputs=[x for x in capture['inputs'] if x['f'] <= limit])
    rows = capture['rows']
    assert not capture.get('teleports'), (phase, 'reference contains teleports')
    assert len(rows) == capture['frames'] + 1, (phase, 'truncated reference')
    assert all(r['f'] == i and r['counter'] == capture['first_counter'] + i
               for i, r in enumerate(rows)), (phase, 'noncontiguous reference')
    assert rows[-1]['counter'] == capture['last_counter'], (phase, 'reference endpoint')
    assert capture['source'] == capture_path(source_of(phase)).parent.name, \
        (phase, 'reference source differs from native path', capture['source'])
    if refs.FORK_MODE:
        applied = [(c['kind'], c['f'], c['frames']) for c in capture.get('v263_pad_corrections', [])]
        assert applied == PAD_CORRECTIONS.get(phase, []), (phase, 'reference pad correction changed', applied)
        assert all(c.get('applied') for c in capture.get('v263_pad_corrections', []))
    return capture


def check_owner_nodes(phase, capture=None):
    """Each OWNERS node holds the recorded owner in the beat's saved RAM:
    callback +0x10 and position +0xB0 equal the last row's."""
    if phase in ROW_LIMIT:              # the saved RAM is past the compared rows
        return
    capture = capture or route_capture(phase)
    ram = capture_path(phase).with_name('eeMemory.bin').read_bytes()
    row = capture['rows'][-1]
    for name, node in owners_of(phase)[0].items():
        callback = struct.unpack_from('<I', ram, node + 0x10)[0]
        pos = [round(v, 5) for v in struct.unpack_from('<3f', ram, node + 0xB0)]
        want = row[name]
        assert callback == int(want['cb'], 16) and ram[node:node + 16].hex() == want['h'], \
            (phase, name, 'recorded owner is not at its node', hex(node))
        assert pos == [round(v, 5) for v in want['pos']], (phase, name, 'position', pos, want['pos'])


def source_gap(phase, capture):
    source = source_capture(phase)
    last = source['inputs'][-1]
    assert (last['buttons'], last['lx'], last['ly']) == (0, 127, 127), (phase, 'source pad is held')
    gap = capture['first_counter'] - source['last_counter']
    # 0: the beat's row 0 is its source's last row (the bed branch's a02_00)
    assert (1 <= gap <= 120) or (gap == 0 and BED and phase == 'a02_00'), (phase, 'source counter gap', gap)
    return gap


def command_rows(phase, capture):
    """The recorder's effective pad (level_smoke_area00.command_rows with this
    level's corrections): a late first input yields to a newer command in
    its window, as the original capture driver does. A beat whose first
    input comes later (a01r_s1: f10) holds the pad the session started
    with until then (route_capture: the pad as the session left it,
    cleared), which is the C driver's pad before its first command."""
    commands = capture['inputs']
    # The bed branch's a04_04 -> a04_05 join: a04_04's last row sets a stick,
    # then neutral. The canonical recording's neutral reaches a04_05's row 1
    # (m1F1 2); on this recording neither reaches it: row 1 keeps row 0's stick
    # (m1F1 1, the yaw unchanged) and the neutral comes at row 2. So the two
    # last commands go, row 0's stick carries, and a04_05 opens with the
    # neutral at f -1 (its row 1's command).
    if BED and phase == 'a04_04':
        commands = commands[:-2]
    corrections = PAD_CORRECTIONS.get(phase, [])
    assert commands and (commands[0]['f'] == 0 or not corrections)
    assert all(a['f'] <= b['f'] for a, b in zip(commands, commands[1:])), (phase, 'backward command')
    out, policy, sent, index = [], (0, 127, 127), None, 0
    frames = sorted({r['f'] for r in commands} | {f for _, f, n in corrections} |
                    {f + n for _, f, n in corrections})
    for frame in frames:
        changed = []
        while index < len(commands) and commands[index]['f'] == frame:
            r = commands[index]
            policy = (r['buttons'], r['lx'], r['ly'])
            changed.append(policy)
            index += 1
        for value in changed or [policy]:
            effective = value
            for kind, begin, duration in corrections:
                if begin <= frame < begin + duration:
                    if kind == 'drop' or (kind == 'late' and frame == begin):
                        effective = (0, 127, 127)
            if changed or effective != sent:
                out.append(dict(f=frame, buttons=effective[0], lx=effective[1], ly=effective[2]))
                sent = effective
    if BED and phase == 'a04_05':
        out.insert(0, dict(f=-1, buttons=0, lx=127, ly=127))
    return out


def checked_phases():
    """The phases with a recording in the selected branch: a04b_* only on
    the bed branch, whose a02_00 follows a02_s0 and which has no beam
    collapse."""
    return tuple(p for p in ROUTE_PHASES if (BED or not p.startswith('a04b_')) and
                 not (BED and p in ('a06_04', 'a06_05')))


def prepare_pads(out):
    out.mkdir(parents=True, exist_ok=True)
    manifest, count = [], 0
    for phase in checked_phases():
        capture = route_capture(phase)
        rows = command_rows(phase, capture)
        frames, gap = capture['frames'], source_gap(phase, capture)
        assert 0 < len(rows) <= 2048 and 1 <= frames <= 20000
        assert all(-1 <= r['f'] <= frames and 0 <= r['buttons'] <= 65535 and
                   0 <= r['lx'] <= 255 and 0 <= r['ly'] <= 255 for r in rows)
        header = f'EMA1 1 {frames} {gap}'
        if phase in a00.RETURN_TICKS:
            # version 2: the destination's state-0 row; the C driver keys
            # the commands after its native boundary to the recorded rows
            # (a02_00 holds the stick across its sub change)
            arrival = next(r['f'] for r in capture['rows'] if r['area4'] == a00.RETURN_AREA[phase] and
                           bytes.fromhex(r['slots'])[8:12] == bytes((3, 1, 0, 0)))
            header = f'EMA1 2 {frames} {gap} {arrival}'
        lines = [header] + [f"{r['f']} {r['buttons']:x} {r['lx']} {r['ly']}" for r in rows]
        (out / f'{phase}.pad').write_text('\n'.join(lines) + '\n')
        manifest.append(dict(phase=phase, reference=refs.label(),
            capture=str(capture_path(phase)), capture_sha256=hashlib.sha256(capture_path(phase).read_bytes()).hexdigest(),
            endpoint_row=frames, gap=gap, pad_changes=len(rows), native_path=phase_path(phase),
            command_delay_rows=2, corrections=PAD_CORRECTIONS.get(phase, []),
            certification='input preparation only; independent native capture comparison required'))
        count += len(rows)
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return count


def check_camera_end(tick, phase):
    path = capture_path(phase)
    capture = route_capture(phase)
    snapshot = json.loads(path.with_name('snapshot.json').read_text())
    assert snapshot['main_loop_counter'] == capture['last_counter'], (phase, 'camera snapshot counter differs')
    ram = path.with_name('eeMemory.bin').read_bytes()
    assert hashlib.sha256(ram).hexdigest() == snapshot['ee_sha256'], (phase, 'snapshot RAM digest differs')
    blocks = tick.get('camblk')
    # the camera block, the forward, D_00810690.. and the eye / target / up
    # quads D_008105D0.. (a01.CAMERA_BLOCKS: the tick log's camblk)
    assert blocks and len(blocks) == len(a01.CAMERA_BLOCKS), (phase, 'missing complete camera observation')
    for (address, size), value in zip(a01.CAMERA_BLOCKS, blocks):
        assert bytes.fromhex(value) == ram[address:address + size], (phase, 'ending camera differs', hex(address))


# The progress block in a beat's end RAM against the tick log's "blk"
# (em_scene_bindings.c log_route_block). The frame count D_00810750..53 is
# the run's own clock: the first level is navigated, not replayed.
BLOCK, BLOCK_SIZE = 0x810700, 0x640
BLOCK_CLOCK = range(0x50, 0x54)


def check_block_end(tick, phase):
    got = bytes.fromhex(tick.get('blk', ''))
    assert len(got) == BLOCK_SIZE, (phase, 'missing progress block observation')
    want = capture_path(phase).with_name('eeMemory.bin').read_bytes()[BLOCK:BLOCK + BLOCK_SIZE]
    differ = [i for i in range(BLOCK_SIZE) if got[i] != want[i] and i not in BLOCK_CLOCK]
    assert not differ, (phase, 'ending progress block differs',
                        [(hex(BLOCK + i), got[i], want[i]) for i in differ[:24]], len(differ))


def check_creature_end(tick, phase):
    """a04b_03's end: the creature event [3] spawned at the lift (callback
    0012E3A0, the tick log's a04c) equals its record in the end RAM."""
    observed = tick.get('a04c')
    assert observed and len(observed['owners']) == 1, (phase, 'missing creature observation')
    (address, value), = observed['owners'].items()
    node = int(address)
    ram = capture_path(phase).with_name('eeMemory.bin').read_bytes()
    want = [ram[node:node + 16].hex(), ram[node + 0xB0:node + 0xBC].hex(), ram[node + 0x1F0:node + 0x200].hex(),
            ram[node + 0x2DC:node + 0x2F0].hex(), struct.unpack_from('<I', ram, node + 0x10)[0]]
    assert value == want and want[4] == 0x0012E3A0, (phase, 'the creature differs', hex(node), value, want)
    return node


def compare_route_row(smoke, ticks, i, row, phase):
    """Every field a00's rows compare (player, ground, camera, task, request,
    message, fade, health, progress, locks, weapon bytes), the revisit's
    sixteen owners and D_00810760..63 (story760). D_008107DF (df) and
    D_008107E0 (e0) are bytes 7 and 8 of the compared D_008107D8 window, and
    D_0081075F byte 7 of story758."""
    tick, following = ticks[i:i + 2]
    p, o = smoke.port_view(ticks, i), smoke.orig_view(row)
    keys = ('spad', 'screen', 'msg', 'power', 'pos', 'yaw', 'eye', 'tgt')
    got, want = {k: p[k] for k in keys}, {k: o[k] for k in keys}
    post, player = bytes.fromhex(tick['post']), tick['player']
    got.update(cam=tick['cam4'], req=post[smoke.tsr.OFFSET[0x8106B0]:smoke.tsr.OFFSET[0x8106B0] + 10].hex(),
               area=post[smoke.tsr.OFFSET[0x810700]:smoke.tsr.OFFSET[0x810700] + 3].hex(),
               slot0=tuple(post[:5]), player=tuple(player[k] for k in (0, 1, 2, 3, 6)),
               ground=hex(player[5]), clock=round(smoke.f32(player[4]), 3), fade=following['fade8'],
               **a01.camera_vectors(tick, (phase, row['f'])))
    want.update(cam=row['cam_mode'], req=row['req'], area=row['area4'][:6],
                slot0=tuple(bytes.fromhex(row['slots'])[8:13]),
                player=(row['p5'], row['m1F0'], row['m1F1'], row['clip'], row['b2F3']),
                ground=row['ground'], clock=row['clock'], fade=row['fade'][:16],
                cam_eye=row['cam_eye'], cam_tgt=row['cam_tgt'], fwd=row['fwd'])
    owners, key = owners_of(phase)
    observed = tick.get(key)
    assert observed, (phase, row['f'], 'missing AREA01 revisit observations')
    got.update(hp=round(smoke.f32(observed['hp']), 3), progress=observed['progress'], s760=observed['s760'],
               locks=observed['locks'], wpn=observed['wpn'], d7dc=observed['progress'][0][8:10])
    want.update(hp=row['hp'], progress=[row['d2'], row['story758'], row['taken'], row['docs']],
                s760=row['story760'], locks=row['locks'], wpn=row['wpn'], d7dc=row['d7dc'])
    if key == 'a04' or key in LATER_KEYS:
        # AREA04's rows: D_00810764..67 (the event's end byte) and
        # D_00810830..3F (D_0081083B, the console's); D_008107E4 / E5 / E9 /
        # EA are bytes 12, 13, 17 and 18 of the compared D_008107D8 window.
        got.update(s764=observed.get('s764'), s830=observed.get('s830'))
        want.update(s764=row['story764'], s830=row['s830'])
    if key in LATER_KEYS:
        # AREA22's rows on: D_00810768..6B, D_00810857 (l857) and the counts
        # D_00810C87 / C88 (items 0x23, 0x24); D_00810766, D_00810845 and
        # D_00810847 (the a01u rows' s766, l845, l847) are bytes of the
        # compared story764 and locks.
        got.update(s768=observed.get('s768'), l857=observed.get('l857'), c87=observed.get('c87'),
                   c88=observed.get('c88'))
        want.update(s768=row['s768'], l857=row['l857'], c87=row['c87'], c88=row['c88'])
    if key == 'a06':
        # AREA06's rows: D_00810CCA (cca, 001C4760(7, 1)).
        got.update(cca=observed.get('cca'))
        want.update(cca=row['cca'])
    if key == 'a04r':
        # the a04b rows: D_0081076C..6F (s76c, flag 0x14). Their l84c is empty
        # (the recorder's lock span ends before it); check_block_end has it.
        got.update(s76c=observed.get('s76c'))
        want.update(s76c=row['s76c'])
    for name, address in owners.items():
        value = observed['owners'].get(str(address))
        assert value is not None, (phase, row['f'], name, 'missing canonical owner')
        rawpos = [round(v, 5) for v in struct.unpack('<3f', bytes.fromhex(value[1]))]
        got[name] = [value[0], rawpos, *value[2:]]
        r = row[name]
        want[name] = [r['h'], [round(v, 5) for v in r['pos']], r['s1F0'], r['t2DC'], int(r['cb'], 16)]
    differences = {k: (got[k], want[k]) for k in got if got[k] != want[k]}
    assert not differences, (phase, f'row {row["f"]}', f'tick {tick["tick"]}', differences)


def route_start(ticks, run, state, phase, capture):
    match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
    assert match, (phase, 'missing alignment')
    counter = int(match.group(1))
    source = source_of(phase)
    ends = state.get('area00_ends' if source == 'a00_10' else 'a01r_ends', {})
    source_counter = ends.get(source)
    assert source_counter is not None, (phase, 'source phase not checked')
    gap = source_gap(phase, capture)
    assert counter == source_counter + gap, (phase, 'source counter gap differs', counter, source_counter, gap)
    start = next(i for i, tick in enumerate(ticks) if tick['counter'] == counter)
    assert start == state['cursor'] + gap - 1, (phase, 'source gap skipped a tick')
    assert all(ticks[i]['counter'] == source_counter + i - state['cursor'] + 1
               for i in range(state['cursor'], start)), (phase, 'noncontiguous source gap')
    return start, counter


def check_route(smoke, ticks, run, state, phase):
    capture = route_capture(phase)
    rows = capture['rows']
    start, counter = route_start(ticks, run, state, phase, capture)
    if phase in a00.RETURN_TICKS:
        # door [16]'s AREA02 load: the gameplay rows, the loader's states (a
        # host wait may be shorter) and the fixed rows after AREA02 state 0
        pairs = a00.return_pairs(smoke, ticks, run, state, phase, start, rows)
    else:
        pairs = [(start + k, row) for k, row in enumerate(rows)]
    end = pairs[-1][0]
    assert end + 1 < len(ticks), (phase, 'missing post-frame tail')
    if phase not in a00.RETURN_TICKS:
        assert all(ticks[i]['counter'] == counter + i - start for i in range(start, end + 1)), \
            (phase, 'skipped native main-loop tick')
    for i, row in pairs:
        compare_route_row(smoke, ticks, i, row, phase)
    if phase not in ROW_LIMIT:
        check_camera_end(ticks[end], phase)
        if owners_of(phase)[1] in LATER_KEYS:
            check_block_end(ticks[end], phase)
    if phase == 'a04b_03':
        print(f'a04b_03: the creature at {check_creature_end(ticks[end], phase):#x} equals its end RAM record')
    state['cursor'] = end + 1
    state.setdefault('a01r_ends', {})[phase] = ticks[end]['counter']
    last = rows[-1]
    print(f'{phase}: PASS {len(rows)} native rows: player, ground, camera, task, request, message, fade, '
          f'health, progress (D_008107DF {last["df"]}, D_008107E0 {last["e0"]}, D_00810760 '
          f'{last["story760"][:2]}), locks, weapon bytes and {len(owners_of(phase)[0])} recorded owners; ending camera bytes '
          + ('and progress block ' if owners_of(phase)[1] in LATER_KEYS else '') + 'equal to saved RAM')


def prefix_report(smoke, ticks, run, phase):
    """After a failed run: the rows of `phase` matched exactly before the first
    difference or the end of the log. A diagnostic, never a PASS."""
    match = re.search(rf'^level smoke: {phase}: aligned counter=(\d+)', run, re.M)
    if not match:
        return None
    counter = int(match.group(1))
    start = next((i for i, t in enumerate(ticks) if t['counter'] == counter), None)
    if start is None:
        return None
    rows = route_capture(phase)['rows']
    exact, first = 0, None
    for k, row in enumerate(rows):
        if start + k + 1 >= len(ticks) or ticks[start + k]['counter'] != counter + k:
            break
        try:
            compare_route_row(smoke, ticks, start + k, row, phase)
        except AssertionError as e:
            first = e.args[0]
            break
        exact += 1
    return exact, len(rows), first
