#!/usr/bin/env python3
"""Check the exported AREA19 assets (assets/area19/, docs/AREA19_ASSETS.md)
against every recorded AREA19 capture, and load each file through the
port's own loader.

The target is AREA19 sub 0 (export_area19_common): the arrival
../Extermination/build/s87/route_a13/a13_05_shaft/ and the tenth level's
route_a19/a19_00_duct, a19_01_pickup_g2, a19_02_duct_back. Missing exports,
ELF, ISO, overlay or captures print SKIPPED and exit 0; once they exist the
a19 census delta is REQUIRED (a missing census fails).

The checks are the AREA13 lane's AREA19 checks
(tools/test_area13_assets_reference.py, imported UNCHANGED and re-pointed
at this lane's tree, captures and pins): the load map, level, background
absence, collision, cell directory (the ORIGINAL 001A2370 / 0x219F50 /
0019C6F0), tables, sound, ctx block (the ORIGINAL 001D8FD0 / 001D1C50), the
port loaders, its canary and its controls (docs/AREA13_ASSETS.md lists
each). This lane adds, over the four captures:
  captures  the AREA19 captures are exactly the pinned four, with their
            area bytes; every other a13 / a19 / a13b folder is excluded;
            level.json, cells.json, tables.json and banks.json were made
            over all four
  sub proof loaded_sub_proof.json = export_area13_common.loaded_sub_proof
            of the run's captures (every capture, both subs, no extra row),
            each capture's own sub fitting RAM over 100x better
  proofs    every capture's cells.json 'proofs' equal the rows the AREA13
            lane's verify_directory (the ORIGINAL 001A2370 / 0x219F50, the
            orphan rule) recomputed in this run: which node proves each hull
  pickup    the pickup g[2] (group 0x829E00 record 2, 00219550): its node
            is live before the Use and gone after it, every other group
            record's liveness unchanged; its hull uid is the one cells.json
            proves by 001A2370 from that node while it lives and as an
            orphan (equal to a derivation in another capture) once it is
            gone, and no other hull is an orphan
  census    the a19 census delta (../Extermination/build/s87/census/
            a19_delta.json): no sub-1 owner of the AREA13 lane's call-site
            census ran in the a19 beats, every SITES owner that ran is
            modelled, no other overlay and no unattributed hit, the load
            path 001FFCD0 did not run (one load across the four captures),
            the single pass A19 over a19_00 .. a19_02 with no beat missing
            or incomplete and every replay completed without error
  controls  a changed input per lane check; lane_checks itself is run on
            tampered copies of the report files and on changed captures
Default and EM_TEST_FULL=1: all four captures (about 7 to 9 s CPU).
"""
from __future__ import annotations

import copy
import json
import os
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19_common as A19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import test_area13_assets_reference as T13  # noqa: E402

A13, C = A19.A13, A19.C
E1, E2, T01, T = T13.E1, T13.E2, T13.T01, T13.T
FULL, MODE = T13.FULL, T13.MODE
FAILS, check = T13.FAILS, T13.check

A = Path(os.environ.get('EM_AREA19_ASSETS') or A19.OUT).resolve()
BUILD = C.ROOT / 'build/area19/assets/test'
CENSUS = C.DECOMP / 'build/s87/census/a19_delta.json'
# the default run takes all four captures (about 7 s CPU on the M1, inside
# the ~10 s budget); EM_TEST_FULL=1 runs the same set
QUICK = A19.CAPTURE_NAMES
AREA_BYTES = {'a13_05_shaft': (0x13, 0, 9), 'a19_00_duct': (0x13, 0, 8), 'a19_01_pickup_g2': (0x13, 0, 8),
              'a19_02_duct_back': (0x13, 0, 9)}
EXCLUDED = ['a04b_04_lift', 'a13_00_door8', 'a13_01_door14', 'a13_02_door17', 'a13_03_item27', 'a13_04_hatch',
            'a13b_00_ladder_up', 'a13b_01_door17', 'a13b_02_button15', 'a13b_03_door8', 'a13b_04_lift_call',
            'a13b_05_lift_ride', 'a13b_s0_roof_ladder']
# (placement nodes live, of them at their record's position and rotation),
# and model owners bound: the pickup g[2] is a group record, neither count moves
ROSTER_LIVE = {n: (54, 53) for n in A19.CAPTURE_NAMES}
MODEL_OWNERS = {n: 41 for n in A19.CAPTURE_NAMES}
GROUP = 0x829E00                     # AREA19 sub 0's first deferred group (001B6910's list)
PICKUP_G2 = 2                        # its record 2: the pickup the a19_01 Use takes
G2_LIVE = {'a13_05_shaft': True, 'a19_00_duct': True, 'a19_01_pickup_g2': False, 'a19_02_duct_back': False}


def configure():
    """Point the AREA13 checker at this lane (idempotent)."""
    A19.install()
    T13.A = A
    T13.BUILD = BUILD
    T13.CANARY = BUILD / f'canary-{os.getpid()}'
    T01.BUILD = BUILD
    T13.QUICK['area19'] = QUICK
    T13.EXCLUDED['area19'] = EXCLUDED
    T13.ROSTER_LIVE.update(ROSTER_LIVE)
    T13.MODEL_OWNERS.update(MODEL_OWNERS)


configure()


# ---------------------------------------------------------------------------
# This lane's checks


def capture_problems(every, tree=None):
    """The AREA19 captures are the pinned four with their area bytes, and the
    exporters' reports were made over all four."""
    out = []
    names = tuple(c.name for c in every)
    if names != A19.CAPTURE_NAMES:
        out.append(f'AREA19 captures {names} != {A19.CAPTURE_NAMES}')
    for cap in every:
        got = tuple(cap.ram[0x810700:0x810703])
        if got != AREA_BYTES.get(cap.name):
            out.append(f'{cap.name}: area bytes {got} != {AREA_BYTES.get(cap.name)}')
    tree = tree or A
    sub = tree / 'sub0'
    try:
        reports = {
            'level.json': json.loads((sub / 'level/level.json').read_text())['captures'],
            'cells.json': [r['capture'] for r in json.loads((sub / 'cells.json').read_text())['cells']['captures']],
            'tables.json': json.loads((tree / 'tables.json').read_text())['captures'],
            'banks.json': json.loads((sub / 'sfx/banks.json').read_text())['captures']}
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return out + [f'reports: {error!r}']
    for name, got in reports.items():
        if tuple(got) != A19.CAPTURE_NAMES:
            out.append(f'{name} was made over {got}, not the four captures')
    return out


_SUB_PROOF = {}


def recomputed_sub_proof(caps):
    """export_area13_common.loaded_sub_proof of `caps` (it measures each
    capture on its own), memoised per capture on its RAM object (a changed
    RAM copy is a new object)."""
    out = {}
    for c in caps:
        k = (c.name, id(c.ram))
        if k not in _SUB_PROOF:
            _SUB_PROOF[k] = (c, A13.loaded_sub_proof([c])[c.name])   # c keeps its RAM (and its id) alive
        out[c.name] = _SUB_PROOF[k][1]
    return out


def sub_proof_problems(caps, proof):
    """loaded_sub_proof.json against the recomputation over `caps`: every
    capture's row, every sub's count."""
    out = []
    got = recomputed_sub_proof(caps)
    if sorted(proof) != sorted(c.name for c in caps):
        out.append(f'loaded_sub_proof.json captures {sorted(proof)} != {sorted(c.name for c in caps)}')
    for cap in caps:
        rows, sub = got[cap.name], cap.ram[0x810701]
        if not all(rows[sub] * 100 < rows[s] for s in rows if s != sub):
            out.append(f'{cap.name}: its sub-{sub} map does not fit RAM far better ({rows})')
        want = {f'sub{s}': v for s, v in rows.items()}
        if proof.get(cap.name) != want:
            out.append(f'{cap.name}: loaded_sub_proof.json {proof.get(cap.name)} != {want}')
    return out


_DIRECTORY = {}


def recording_verify_directory(original):
    """A wrapper of export_area13_level.verify_directory that keeps the rows
    of each call (by capture names) for proof_text_problems; installed only
    around the AREA13 lane's run_checks."""
    def wrapped(elf, disc, caps):
        rows, problems, calls = original(elf, disc, caps)
        _DIRECTORY[tuple(c.name for c in caps)] = copy.deepcopy(rows)
        return rows, problems, calls
    return wrapped


def proof_text_problems(rows, record):
    """Every capture's `proofs` in cells.json equals the row the ORIGINAL
    001A2370 / 0x219F50 / orphan derivation (E1.verify_directory) produced
    over the run's captures: which node and behaviour proves each hull."""
    if rows is None:
        return ['cells.json proofs: no recomputed directory rows']
    out = []
    stored = {r['capture']: r.get('proofs') for r in record['cells']['captures']}
    got = {r['capture']: {str(u): p for u, p in r['proofs'].items()} for r in rows}
    if sorted(got) != sorted(stored):
        out.append(f'cells.json proof rows {sorted(stored)} != recomputed {sorted(got)}')
    for name, want in got.items():
        if stored.get(name) != want:
            diff = sorted(set(want) ^ set(stored.get(name) or {}) |
                          {u for u in want if (stored.get(name) or {}).get(u) != want[u]}, key=int)
            out.append(f'{name}: cells.json proofs differ from the recomputed directory at hulls {diff}')
    return out


def group_sources(roster):
    """[(label, fields, pos, rot)] of the first group's records (001B6660's
    copied fields, export_area01_tables.spawn_fields_group)."""
    _places, groups = T13.roster_records(roster)
    address, records = groups[0]
    if address != GROUP:
        raise ValueError(f'first group {address:#x}, not {GROUP:#x}')
    return [(f'g[{i}]', *T.spawn_fields_group(r)) for i, r in enumerate(records)]


def pickup_problems(caps, roster, record):
    """The pickup g[2]: live before the Use, gone after it, every other
    group record unchanged; its hull proved from its node while it lives
    and as an orphan once it is gone; no other orphan."""
    out = []
    try:
        sources = group_sources(roster)
    except (ValueError, IndexError, struct.error) as error:
        return [f'pickup: {error!r}']
    rows = {r['capture']: r['proofs'] for r in record['cells']['captures']}
    base, uid = None, None
    for cap in caps:
        live = [bool(r['nodes']) for r in T.match_nodes(cap.ram, sources)]
        want = G2_LIVE.get(T13.key(cap))
        if want is None or live[PICKUP_G2] != want:
            out.append(f'{cap.name}: g[{PICKUP_G2}] live {live[PICKUP_G2]}, not {want}')
        others = live[:PICKUP_G2] + live[PICKUP_G2 + 1:]
        if base is None:
            base = others
        elif others != base:
            out.append(f'{cap.name}: another group record changed liveness')
        proofs = rows.get(T13.key(cap), {})
        if live[PICKUP_G2]:
            slot = T.match_nodes(cap.ram, [sources[PICKUP_G2]])[0]['nodes'][0]['slot']
            node = T.POOL_BASE + slot * T.POOL_STRIDE            # its +0x10 is the record's 00219550
            u = cap.ram[node + 0x0F]
            uid = u if uid is None else uid
            if u != uid or proofs.get(str(u)) != f'001A2370(node {node:#x}, behaviour {E1.PICKUP:#x})':
                out.append(f'{cap.name}: hull {u} is not proved from the g[{PICKUP_G2}] node {node:#x}')
    for cap in caps:
        proofs = rows.get(T13.key(cap), {})
        orphans = sorted(int(u) for u, p in proofs.items() if p.startswith('orphan'))
        want = [] if G2_LIVE.get(T13.key(cap)) else ([uid] if uid is not None else ['the pickup hull'])
        if orphans != want:
            out.append(f'{cap.name}: orphan hulls {orphans} != {want}')
    return out


def census_problems(census):
    """The a19 census delta against the AREA13 lane's call-site census."""
    out = []
    ran = {int(f['addr'], 16) for f in census.get('functions', [])}
    sites = T13.SITES['area19']
    owners = {o for owner in sites.values() for o in owner}
    modelled = {E1.PICKUP, E1.DRUM, E1.CREATURES['area19'], E1.SCALED_CALLER, E1.SCALED_OWNER,
                E1.FLAG_OWNERS['area19']}
    sub1 = sorted(hex(o) for o in ran & set(T13.SUB1['area19']))
    if sub1:
        out.append(f'census: sub-1 owners ran in the a19 beats: {sub1}')
    unmodelled = sorted(hex(o) for o in (ran & owners) - modelled - set(T13.SUB1['area19']))
    if unmodelled:
        out.append(f'census: unmodelled call-site owners (not sub-1) ran: {unmodelled}')
    if census.get('overlay_hits_other_overlay') or census.get('unattributed_hits'):
        out.append('census: hits outside the AREA19 overlay and the ELF')
    if 0x1FFCD0 in ran:
        out.append('census: the loader 001FFCD0 ran in the a19 beats')
    summary = census.get('summary', {})
    beats = list(A19.CAPTURE_NAMES[1:])
    if summary.get('passes') != ['A19'] or [b['beat'] for b in census.get('per_beat', [])] != beats:
        out.append('census: not the a19 pass over a19_00 .. a19_02')
    runs = summary.get('replay_runs', {})
    if summary.get('beats_missing') != [] or summary.get('beats_incomplete') != [] or \
            sorted(runs) != sorted(beats) or not all(r.get('completed') and r.get('error') is None
                                                     for r in runs.values()):
        out.append('census: a beat missing, incomplete or not replayed to completion')
    return out


# the files lane_checks reads from an export tree
LANE_FILES = ('sub0/level/level.json', 'sub0/cells.json', 'tables.json', 'sub0/sfx/banks.json',
              'loaded_sub_proof.json', 'sub0/roster.emro')


def lane_checks(every, caps, rows, tree=None, census_path=None):
    """Every lane check over `tree` (default the export tree); `rows` are
    the directory rows E1.verify_directory recomputed over `caps`. The
    census is required: a missing or unreadable census is a problem."""
    tree = tree or A
    census_path = census_path or CENSUS
    sub = tree / 'sub0'
    try:
        roster = (sub / 'roster.emro').read_bytes()
        record = json.loads((sub / 'cells.json').read_text())
        proof = json.loads((tree / 'loaded_sub_proof.json').read_text())
    except (OSError, ValueError) as error:
        return [f'lane inputs: {error!r}'], None
    P = capture_problems(every, tree)
    P += sub_proof_problems(caps, proof)
    P += proof_text_problems(rows, record)
    P += pickup_problems(caps, roster, record)
    try:
        census = json.loads(census_path.read_text())
    except (OSError, ValueError) as error:
        return P + [f'census (required; the decomp tool route_census.py writes it from the a19 captures): {error!r}'], None
    P += census_problems(census)
    return P, census


def lane_controls(K, every, caps, census, rows):
    """One changed input per lane check; lane_checks itself on tampered
    copies of the report files. Returns the count."""
    n = 0

    def expect(problems, what, accept=False):
        nonlocal n
        n += 1
        check(bool(problems) != accept, f'control area19: {what} {"rejected" if accept else "missed"}')
    by = {c.name: c for c in every}
    first = by['a13_05_shaft']
    lone = by['a19_01_pickup_g2']
    sub = A / 'sub0'
    roster = (sub / 'roster.emro').read_bytes()
    record = json.loads((sub / 'cells.json').read_text())
    proof = json.loads((A / 'loaded_sub_proof.json').read_text())
    sources = group_sources(roster)
    slot = T.match_nodes(first.ram, [sources[PICKUP_G2]])[0]['nodes'][0]['slot']
    node = T.POOL_BASE + slot * T.POOL_STRIDE
    uid = first.ram[node + 0x0F]
    # captures: each one's area bytes, the set
    expect(capture_problems(every[:-1]), 'the last capture missing')
    for cap in every:
        for at in (0x810701, 0x810702):
            changed = [T13.ram_copy(c, c.name, [(at, bytes([c.ram[at] ^ 1]))]) if c is cap else c for c in every]
            expect(capture_problems(changed), f'{cap.name} with byte {at:#x} changed')
    # lane_checks over a copy of the report files, each edited in turn
    tree = BUILD / f'reports-{os.getpid()}'

    def lane(edit=None, rel=None, census_path=None, rows_=rows, caps_=caps):
        if tree.exists():
            shutil.rmtree(tree)
        for f in LANE_FILES:
            d = tree / f
            d.parent.mkdir(parents=True, exist_ok=True)
            d.write_bytes((A / f).read_bytes())
        if edit is not None:
            q = tree / rel
            data = json.loads(q.read_text())
            edit(data)
            q.write_text(json.dumps(data))
        return lane_checks(every, caps_, rows_, tree, census_path)[0]

    def cells_row(name):
        return lambda d: next(r for r in d['cells']['captures'] if r['capture'] == name)

    def set_proof(name, u, text):
        return lambda d: cells_row(name)(d)['proofs'].__setitem__(str(u), text)
    try:
        expect(lane(), 'the copied reports', accept=True)
        for rel, edit in (('sub0/level/level.json', lambda d: d.__setitem__('captures', d['captures'][:1])),
                          ('sub0/cells.json', lambda d: d['cells'].__setitem__('captures', d['cells']['captures'][:1])),
                          ('tables.json', lambda d: d.__setitem__('captures', d['captures'][:1])),
                          ('sub0/sfx/banks.json', lambda d: d.__setitem__('captures', d['captures'][:1]))):
            expect(lane(edit, rel), f'{rel} made over a13_05 alone')
        for cap in every:
            for s_ in ('sub0', 'sub1'):
                expect(lane(lambda d, c=cap.name, k=s_: d[c].__setitem__(k, d[c][k] + 1), 'loaded_sub_proof.json'),
                       f'loaded_sub_proof.json {cap.name} {s_} + 1')
        expect(lane(lambda d: d.__setitem__('extra', d[first.name]), 'loaded_sub_proof.json'),
               'loaded_sub_proof.json with an extra capture')
        # the proof text of every hull: 49 in a19_02 as derived, 50 in a19_02
        # as an orphan, 47 in a19_00 from another node, 23 in a13_05 as underived
        expect(lane(set_proof('a19_02_duct_back', 50, 'orphan: equal to a derivation of it in another capture'),
                    'sub0/cells.json'), 'a19_02 hull 50 recorded as an orphan')
        a00 = cells_row('a19_00_duct')(record)['proofs']
        expect(lane(set_proof('a19_00_duct', 47, a00['47'].replace('node 0x', 'node 0x1')), 'sub0/cells.json'),
               'a19_00 hull 47 recorded from another node')
        expect(lane(set_proof('a13_05_shaft', 23, 'underived'), 'sub0/cells.json'), 'a13_05 hull 23 recorded as underived')
        expect(lane(lambda d: cells_row('a19_01_pickup_g2')(d)['proofs'].pop(str(uid)), 'sub0/cells.json'),
               f'a19_01 hull {uid} without a proof')
        expect(lane(rows_=None), 'no recomputed directory rows')
        # the pickup through lane_checks: g[0] freed in a19_01 (RAM only;
        # cells.json and the directory rows unchanged)
        g0 = T.match_nodes(first.ram, [sources[0]])[0]['nodes'][0]['slot']
        n0 = T.POOL_BASE + g0 * T.POOL_STRIDE
        expect(lane(caps_=[T13.ram_copy(c, c.name, [(n0, b'\0')]) if c is lone else c for c in caps]),
               'lane checks with g[0] freed in a19_01')
        # the census: required
        expect(lane(census_path=tree / 'a19_delta_absent.json'), 'the census missing')
    finally:
        if tree.exists():
            shutil.rmtree(tree)
    # the sub proof's own rule
    other = T13.ram_copy(first, 'c', [(0x810701, b'\x01')])
    expect(sub_proof_problems([other], {'c': proof[first.name]}), 'a capture read as sub 1')
    # the pickup
    pick = [c for c in caps if c.name in ('a13_05_shaft', 'a19_01_pickup_g2')]
    expect(pickup_problems(pick, roster, record), 'the pickup as captured', accept=True)
    expect(pickup_problems([T13.ram_copy(first, first.name, [(node, b'\0')])], roster, record),
           'g[2] freed before the Use')
    revived = T13.ram_copy(lone, lone.name, [(node, first.ram[node:node + T.POOL_STRIDE])])
    expect(pickup_problems([revived], roster, record), 'g[2] live after the Use')
    # another group record freed with g[2]: below it, just above it, the last
    for i in (0, 1, PICKUP_G2 + 1, len(sources) - 1):
        hit = T.match_nodes(first.ram, [sources[i]])[0]['nodes']
        if not hit:
            continue
        ni = T.POOL_BASE + hit[0]['slot'] * T.POOL_STRIDE
        expect(pickup_problems([first, T13.ram_copy(lone, lone.name, [(ni, b'\0')])], roster, record),
               f'g[{i}] freed with g[2]')
    rec2 = copy.deepcopy(record)
    row = next(r for r in rec2['cells']['captures'] if r['capture'] == lone.name)
    row['proofs'][str(uid)] = f'001A2370(node {node:#x}, behaviour {E1.PICKUP:#x})'
    expect(pickup_problems(pick, roster, rec2), 'the taken pickup\'s hull recorded as derived')
    rec4 = copy.deepcopy(record)
    row = next(r for r in rec4['cells']['captures'] if r['capture'] == first.name)
    row['proofs'][str(uid)] = f'001A2370(node {node + T.POOL_STRIDE:#x}, behaviour {E1.PICKUP:#x})'
    expect(pickup_problems(pick, roster, rec4), 'the live pickup\'s hull recorded from another node')
    rec3 = copy.deepcopy(record)
    row = next(r for r in rec3['cells']['captures'] if r['capture'] == lone.name)
    other_uid = next(u for u in row['proofs'] if int(u) != uid)
    row['proofs'][other_uid] = 'orphan: equal to a derivation of it in another capture'
    expect(pickup_problems(pick, roster, rec3), 'a second orphan recorded')
    # the orphan proof itself (the AREA13 lane's verify_directory over this
    # lane's captures): the taken pickup's hull without a deriving capture,
    # with one (accepted), and its bytes changed
    cells = (sub / 'area19_cells.bin').read_bytes()
    _c, hulls, _s = T13.L.cell_directory(cells, 0)
    table = C.u32(first.spad, T13.L.SPAD_CELLS)
    expect(E1.verify_directory(K.elf, cells, [lone])[1], f'the orphan {uid} without a deriving capture')
    expect(E1.verify_directory(K.elf, cells, [first, lone])[1], f'the orphan {uid} with a13_05', accept=True)
    s_, _e, _f = hulls[uid]
    changed = T13.ram_copy(lone, lone.name, [T13.flip(lone.ram, table + s_ + 0x24, 0x10)])
    expect(E1.verify_directory(K.elf, cells, [first, changed])[1], f'the orphan {uid} bytes changed')
    # the census
    expect(census_problems(census), 'the a19 census as recorded', accept=True)
    edits = [(f'the sub-1 owner {o:#x} ran', lambda d, o=o: d['functions'].append(dict(addr=hex(o))))
             for o in T13.SUB1['area19']]
    edits += [
        # the 0019C6F0 callback 0x827B20 of chain 0x82E090 (started only by sub-1 [34])
        ('the callback 0x827B20 ran', lambda d: d['functions'].append(dict(addr=hex(0x827B20)))),
        ('the loader ran', lambda d: d['functions'].append(dict(addr=hex(0x1FFCD0)))),
        ('a hit in another overlay', lambda d: d['overlay_hits_other_overlay'].append('0x823600')),
        ('an unattributed hit', lambda d: d['unattributed_hits'].append('0x00500000')),
        ('another pass', lambda d: d['per_beat'].pop()),
        ('a second pass name', lambda d: d['summary']['passes'].append('A13B')),
        ('a beat missing', lambda d: d['summary']['beats_missing'].append('a19_01_pickup_g2')),
        ('a beat incomplete', lambda d: d['summary']['beats_incomplete'].append('a19_01_pickup_g2')),
        ('a replay not completed', lambda d: d['summary']['replay_runs']['a19_02_duct_back'].__setitem__(
            'completed', False)),
        ('a replay error', lambda d: d['summary']['replay_runs']['a19_00_duct'].__setitem__('error', 'x')),
        ('a replay absent', lambda d: d['summary']['replay_runs'].pop('a19_01_pickup_g2'))]
    for what, edit in edits:
        d = copy.deepcopy(census)
        edit(d)
        expect(census_problems(d), f'census: {what}')
    return n


# ---------------------------------------------------------------------------


def main():
    need = [A / 'sub0/level/level.json', A / 'tables.json', A / 'sub0/cells.json', A / 'sub0/sfx/banks.json',
            A / 'loaded_sub_proof.json', C.ELF_PATH, C.ISO_PATH, A19.TARGET.overlay_path] + \
        [p / 'eeMemory.bin' for p in (A19.ARRIVAL, A19.ROUTE_A19 / 'a19_00_duct')]
    missing = [str(p) for p in need if not p.exists()]
    if missing:
        print('area19 assets reference: SKIPPED, missing local inputs:', missing)
        return 0
    t0 = time.time()
    el = T13.L.load_export_level()
    lib = T01.build_loaders()
    bg = T13.background_lib()
    K, caps = T13.target_setup('area19', el)
    print(f'area19 assets reference ({MODE}) {K.t.label} sub 0: {len(caps)} of {len(K.every)} captures '
          f'({", ".join(c.name for c in caps)})')
    loaded = T13.check_loaders(lib, bg, K.t, A)
    for lname, ok in loaded.items():
        check(ok, f'area19: port loader rejects {lname}')
    files = [k for k in loaded if k not in ('cells refused (bit 29)', 'cells (bit 29 cleared)')]
    print(f'  loaders: {sum(loaded[k] for k in files)}/{len(files)} files accepted by the port loaders; the cells '
          f'file refused (bit 29): {loaded.get("cells refused (bit 29)")}, its bit-29-cleared copy loads: '
          f'{loaded.get("cells (bit 29 cleared)")}')
    original = E1.verify_directory
    E1.verify_directory = recording_verify_directory(original)
    try:
        problems, V = T13.run_checks(K, caps)
    finally:
        E1.verify_directory = original
    rows = _DIRECTORY.get(tuple(c.name for c in caps))
    for p in problems:
        check(False, f'area19: {p}')
    lv = V.get('level', {})
    print(f"  load map + level: {lv.get('objects')} bank objects -> {lv.get('zones')} zone EMDL(s), "
          f"{lv.get('kicks')} level kicks; collision, cells, tables, sfx, ctx: {len(problems)} problems")
    lp, census = lane_checks(K.every, caps, rows)
    for p in lp:
        check(False, f'area19 lane: {p}')
    print(f'  lane: captures, sub proof, cells.json proofs, pickup g[2], census: {len(lp)} problems')
    if FAILS:
        print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
    else:
        T13.use(K)
        print(f'  canary: {T13.canary(K, lib, bg, caps[0])} sections each reported their planted difference')
        if not FAILS:
            print(f'  controls (AREA13 lane): {T13.controls(K, caps)} changed inputs, each caught '
                  '(or accepted where marked)')
        if not FAILS:
            T13.use(K)
            print(f'  controls (this lane): {lane_controls(K, K.every, caps, census, rows)} changed inputs, each caught '
                  '(or accepted where marked)')
    for f in FAILS:
        print('  FAIL:', f)
    print(f'area19 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
