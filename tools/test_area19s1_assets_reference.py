#!/usr/bin/env python3
"""Check the AREA19S1 export (assets/area19s1/, docs/AREA19S1_ASSETS.md)
against the a19c sub-1 captures, load each file through the port's own
loader, and run the AREA19X review controls the AREA19X lane left open.

The target is AREA19 sub 1 (export_area19s1_common): the thirteenth level's
a19c_06_ladder959 and a19c_07_door52 (../Extermination/build/s87/
route_a19c/). The checked tree is a view: sub1/ and the placed side files
from assets/area19s1/, the side files sub1.json lists as identical from
assets/area19/. Missing exports, ELF, ISO, overlay, captures or census
print SKIPPED.

The checks are the AREA13 lane's AREA19 checks (tools/test_area13_assets_
reference.py, imported UNCHANGED: run_checks + its port loaders + canary),
re-pointed at the view, the two captures and this lane's pins, with
export_area19s1_common's sub-1 rules installed (its 0019C6F0 / 001A2370
owners, the freed-drum hulls, the fire's overlay-data words, 0x1E3D90's
unbound +0x44). Two of its functions are replaced for sub 1, by name in
that module: census_problems (the call-site census: its sub-0 form requires
the sub-1 owners to be absent) and canary_plants (its creature plant needs
the sub-0 creature 0x827DD0; here [36]'s and [35]'s matrices). This lane
adds:
  captures  exactly the two pinned captures, area bytes 13 01 07 / 13 01 01;
            a19c_00 .. a19c_05 AREA19 sub 0 (excluded); the reports made
            over both; D_0028A5A4's previous capture a19c_05
  cursor    D_0028A73C / D_0028A740 / D_0028A59C / D_0028A5A0 pinned; every
            top-list relocation word as in a19c_05 (the top block stays),
            every nested word its own formula
  split     sub1.json against both trees: every file of the tree's sub1/,
            side files identical (hash, not copied) or placed (the two
            reports), nothing else
  sub proof loaded_sub_proof.json = the recomputation, sub 1 over 100x
            better
  rules     flag calls pinned per capture; the freed-drum hulls exactly
            23, 29, 30, 32..36 in a19c_07 and none in a19c_06; cells.json
            proofs = the rows recomputed in this run; the chain 0x82E090
            facts the [34] rule reads; the fire window and 0x1E3D90's
            leftover +0x44 (= a19c_06's same slot)
  census    the a19c delta: the pass A19C over the eight beats, complete;
            001FFCD0 in a19c_06 only; sub 1's owners first in a19c_06, the
            callbacks 0x827B10 / 0x827B20 in a19c_07 only; the sub-0-only
            owners not in a19c_07; no unattributed or other-overlay hit
  area19x   the per-field controls of the [43] rule and of creature_nodes'
            bit 0 (AREA19X_ASSETS.md, review), on a19b_02 with the AREA19X
            lane's sub-0 state, tied to its exports (world_models.json,
            creature.json)
  controls  a changed input per lane check; lane_checks itself on tampered
            copies of the tree
Default and EM_TEST_FULL=1: both captures (they are the whole set).
"""
from __future__ import annotations

import copy
import hashlib
import json
import os
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_area19s1_common as S1  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target at sub 1)
import export_area19s1_split as SPLIT  # noqa: E402
import export_area19x_tables as XT  # noqa: E402  (creature_nodes / creature_problems: the AREA19X controls)
import test_area13_assets_reference as T13  # noqa: E402

S1.install()
A13, C, LV, TB, X19 = S1.A13, S1.C, S1.LV, S1.TB, S1.X19
E1, E2, T01, T = T13.E1, T13.E2, T13.T01, T13.T
FULL, MODE = T13.FULL, T13.MODE
FAILS, check = T13.FAILS, T13.check

OUT = Path(os.environ.get('EM_AREA19S1_ASSETS') or S1.OUT).resolve()
AREA = S1.A19_TREE.resolve()
BUILD = C.ROOT / 'build/area19s1/assets/test'
VIEW = BUILD / f'view-{os.getpid()}'
CENSUS = C.DECOMP / 'build/s87/census/a19c_delta.json'
X19_RELOAD = AREA / 'reload'                         # the AREA19X lane's export (its review controls)
GRID = 0x1879580                                     # D_0028A598 of sub 1 (sub 0: 0x1900500)
AREA_BYTES = {'a19c_06_ladder959': (0x13, 1, 7), 'a19c_07_door52': (0x13, 1, 1)}
CURSORS = (0x133C1C0, 0x162A580, 0x133C1C0, 0x162A580)   # D_0028A73C / 740 / 59C / 5A0
D_0028A59C, D_0028A5A0 = 0x28A59C, 0x28A5A0
# (placement nodes live, of them at their record's position and rotation)
# (the flames [40] and the fire [39] set their own position in state 0)
ROSTER_LIVE = {'a19c_06_ladder959': (52, 50), 'a19c_07_door52': (40, 38)}
MODEL_OWNERS = {'a19c_06_ladder959': 48, 'a19c_07_door52': 37}
# [40] 0x823780 (func_overlay_AREA19_00823740.c, NEARMISS; its .s stores the
# same bytes, its header says) sets +0x03 = 3 and +0x0D = 0 in state 0, which
# it leaves at once (to state 1, or 3)
FLAMES, FLAMES_FIELDS = 0x823780, {0x03: 3, 0x0D: 0}
FLAG_CALLS = {'a19c_06_ladder959': [[7, 1], [0x15, 1]], 'a19c_07_door52': [[7, 1], [0x15, 1], [8, 1]]}
FREED = {'a19c_06_ladder959': (), 'a19c_07_door52': (23, 29, 30, 32, 33, 34, 35, 36)}
NO_MODEL_SLOTS = {'a19c_06_ladder959': (), 'a19c_07_door52': (42, 43, 80, 81, 82, 84)}   # 0x1E3D90 nodes
SUB0_ONLY = (0x825C70, 0x827DD0)                     # [9] and the outdoor creature: the sub-0 owners
REPORTS = ('tables.json', 'loaded_sub_proof.json')
SUB1_OWNERS = (0x826C10, 0x827430, 0x826570, 0x8279E0, 0x829A70, 0x823D10, 0x827B60)
CALLBACKS_07 = (0x827B10, 0x827B20)
NOTE_CHAIN, NOTE_CALLBACK = 0x82E090, 0x827B20
# the AREA19X review controls
A19B_02 = X19.ROUTE_A19B / 'a19b_02_ladder1023'
SWAP_NODE, CREATURE_NODE = 0x7B3E50, 0x7BDCF0


def configure(view=None):
    """Point the AREA13 checker at this lane (idempotent)."""
    S1.install()
    T13.A = view or VIEW
    T13.BUILD = BUILD
    T13.CANARY = BUILD / f'canary-{os.getpid()}'
    T01.BUILD = BUILD
    T13.QUICK['area19'] = S1.CAPTURE_NAMES
    T13.EXCLUDED['area19'] = []
    T13.GRID['area19'] = GRID
    T13.ROSTER_LIVE.update(ROSTER_LIVE)
    T13.MODEL_OWNERS.update(MODEL_OWNERS)
    T13.census_problems = census_sites
    T13.canary_plants = canary_plants
    T13.field_problems = field_problems


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_view(out, area, view):
    """A tree of symlinks in assets/area19's layout: sub1/ and the placed
    side files from `out`, the identical side files from `area`. Returns
    sub1.json."""
    record = json.loads((out / 'sub1.json').read_text())
    if view.exists():
        shutil.rmtree(view)
    for rel in record['files']:
        (view / 'sub1' / rel).parent.mkdir(parents=True, exist_ok=True)
        (view / 'sub1' / rel).symlink_to(out / 'sub1' / rel)
    for name in record['side_placed']:
        (view / name).symlink_to(out / name)
    for name in record['side_identical']:
        (view / name).symlink_to(area / name)
    return record


# ---------------------------------------------------------------------------
# Replacements inside the AREA13 checker (sub 1)


def census_sites(K, caps, placements=None, groups=None):
    """The AREA13 checker's call-site census for sub 1: every jal / j /
    address word of 001A2370, 0x219F50 and 0019C6F0 in the ELF and the
    module is a SITES entry inside its function; the owners this lane's
    exporter models are the sub-1 ones (export_area19s1_common) plus the
    pickup, drum and 0x219870 / 0x219F50; the rest are exactly SUB0_ONLY,
    with no live node in any capture and no sub-1 placement or group
    record; the callback 0x827B20 is named by chain 0x82E090 only, started
    by [34] only."""
    out = []
    text = K.elf[C.ELF_OFFSET:C.ELF_OFFSET + C.ELF_FILESZ]
    pinned = T13.SITES[K.t.name]
    for callee, owners in pinned.items():
        got = []
        for word in ((3 << 26) | (callee >> 2), (2 << 26) | (callee >> 2), callee):
            got += T13.word_sites(text, C.ELF_VADDR, word) + T13.word_sites(K.ov, C.u32(K.ov, 8), word)
        if sorted(got) != sorted(a for sites in owners.values() for a in sites):
            out.append(f'{callee:#x} call sites {[hex(a) for a in sorted(got)]} != the census')
        for owner, sites in owners.items():
            if not all(owner <= a < owner + T13.FUNC_SIZE[owner] for a in sites):
                out.append(f'{callee:#x}: a site of {owner:#x} lies outside it')
    modelled = {E1.PICKUP, E1.DRUM, E1.SCALED_CALLER, E1.SCALED_OWNER, S1.NOTE_34, *S1.SELF_MATRIX_OWNERS,
                *S1.FLAG_STATES, *T13.CALLBACKS[K.t.name]}
    owners = {o for owner in pinned.values() for o in owner}
    if owners - modelled != set(SUB0_ONLY):
        out.append(f'owners not modelled for sub 1 {sorted(hex(o) for o in owners - modelled)} != the sub-0 list')
    for cb, (chain, starter) in T13.CALLBACKS[K.t.name].items():
        if T13.chain_starters(K.t, chain) != {starter} or starter != S1.NOTE_34:
            out.append(f'callback {cb:#x}: chain {chain:#x} is started by '
                       f'{sorted(hex(x) for x in T13.chain_starters(K.t, chain))}, not only [34]')
    out += chain_problems(K.ov)
    for o in SUB0_ONLY:
        for cap in caps:
            if any(C.u32(cap.ram, a + 0x10) == o for _s, a in T.pool_nodes(cap.ram)):
                out.append(f'{cap.name}: a live node of the sub-0 owner {o:#x}')
        if placements is not None and (any(C.u32(r, 0x24) == o for r in placements) or
                                       any(C.u32(r, 0x28) == o for _a, recs in groups for r in recs)):
            out.append(f'the sub-1 roster names the sub-0 owner {o:#x}')
    return out


def field_problems(blob, ram):
    """The AREA13 checker's field_problems, with [40]'s own state-0 fields:
    a live [40] node past state 0 must hold FLAMES_FIELDS instead of its
    record's +0x03 / +0x0D (its +0x54 is still the record's)."""
    flames = {a for _s, a in T.pool_nodes(ram) if C.u32(ram, a + 0x10) == FLAMES and ram[a + 4]}
    out = [p for p in T13._ORIGINAL_FIELD_PROBLEMS(blob, ram)
           if not any(f'node {a:#x} lost its copied field +{off:#x}' in p for a in flames for off in FLAMES_FIELDS)]
    for a in flames:
        for off, want in FLAMES_FIELDS.items():
            if ram[a + off] != want:
                out.append(f'[40] node {a:#x}: +{off:#x} = {ram[a + off]:#x}, not its state-0 value {want:#x}')
    return out


def chain_problems(ov, chain=NOTE_CHAIN):
    """The facts export_area19s1_common's [34] rule reads from chain
    0x82E090: no record carries the jump bit (0x40000000), a record names
    the callback 0x827B20 before the stop record, and the stop record is
    op 7 with +0x14 = 0x46 (the flag-0x46 store measured at a19c_07 f565)."""
    olo, ohi = E2.E02.overlay_data_window(ov)
    data = ov[olo - C.OVERLAY_ARENA:ohi - C.OVERLAY_ARENA]
    try:
        recs = T.walk_chain(data, olo, chain)
    except SystemExit as error:
        return [f'chain {chain:#x}: {error}']
    words = [struct.unpack_from('<16I', data, r - olo) for r in recs]
    out = []
    if any(w[0] & 0x40000000 for w in words):
        out.append(f'chain {chain:#x}: a jump record')
    named = [k for k, w in enumerate(words) if NOTE_CALLBACK in w]
    if not named or named[-1] >= len(words) - 1:
        out.append(f'chain {chain:#x}: no record before the stop names {NOTE_CALLBACK:#x}')
    stop = words[-1]
    if not stop[0] & 0x80000000 or stop[0] & 0xFF != 7 or stop[5] != 0x46:
        out.append(f'chain {chain:#x}: the stop record is not op 7 on flag 0x46')
    return out


def canary_plants(K, cap, tree, tag):
    """The AREA13 checker's canary_plants with its sub-0 creature plant
    (the creature 0x827DD0 has no node in sub 1) replaced by sub 1's
    self-matrix owners [36] 0x826C10 and [35] 0x827430: a byte of each
    one's matrix (+0xD0) flipped, so its hull derivation must fail. The
    original runs with its creature lookup pointed at [36]; the byte its
    creature plant flips (an address read from [36]'s +0x11C) is restored
    and that section dropped."""
    saved = T13.node_of
    ram = cap.ram
    lift = saved(ram, 0x826C10)
    stray = C.u32(ram, lift + 0x11C) + 0x90 + 0x33
    T13.node_of = lambda r, behaviour, uid=None: (lift if behaviour == E1.CREATURES[K.t.name]
                                                  else saved(r, behaviour, uid))
    try:
        fake, want = T13._ORIGINAL_CANARY_PLANTS(K, cap, tree, tag)
    finally:
        T13.node_of = saved
    edited = bytearray(fake.ram)
    edited[stray] = ram[stray]
    want.pop('creature matrix')
    for behaviour in S1.SELF_MATRIX_OWNERS:
        node = saved(ram, behaviour)
        edited[node + 0xD0 + 0x33] ^= 0x04
        want[f'{behaviour:#x} matrix'] = f'{tag}: hull {ram[node + 0x0F]} differs and no owner derivation'
    fake.ram = bytes(edited)
    return fake, want


T13._ORIGINAL_CANARY_PLANTS = T13.__dict__.get('_ORIGINAL_CANARY_PLANTS') or T13.canary_plants
T13._ORIGINAL_FIELD_PROBLEMS = T13.__dict__.get('_ORIGINAL_FIELD_PROBLEMS') or T13.field_problems
configure()


# ---------------------------------------------------------------------------
# This lane's checks


def capture_problems(every, tree, others=None):
    out = []
    names = tuple(c.name for c in every)
    if names != S1.CAPTURE_NAMES:
        out.append(f'AREA19 sub-1 captures {names} != {S1.CAPTURE_NAMES}')
    for cap in every:
        got = tuple(cap.ram[0x810700:0x810703])
        if got != AREA_BYTES.get(cap.name):
            out.append(f'{cap.name}: area bytes {got} != {AREA_BYTES.get(cap.name)}')
    if others is None:
        others = {}
        for n in S1.SUB0_CAPTURES:
            p = S1.ROUTE_A19C / n / 'eeMemory.bin'
            if p.exists():
                with open(p, 'rb') as f:
                    f.seek(0x810700)
                    head = f.read(2)
                    f.seek(C.OVERLAY_ARENA)
                    mod = f.read(8)
                others[n] = (tuple(head), mod[:4], C.u32(mod, 4))
    if sorted(others) != sorted(S1.SUB0_CAPTURES) or any(v != ((0x13, 0), b'MWo3', 0x10) for v in others.values()):
        out.append(f'a19c_00 .. a19c_05 are not all AREA19 sub 0 with the module resident: {others}')
    if sorted(n for n, _a in A13.excluded_captures(S1.TARGET)) != []:
        out.append('excluded captures among the sub-1 capture paths')
    try:
        reports = {
            'level.json': json.loads((tree / 'sub1/level/level.json').read_text())['captures'],
            'cells.json': [r['capture'] for r in json.loads((tree / 'sub1/cells.json').read_text())['cells']['captures']],
            'tables.json': json.loads((tree / 'tables.json').read_text())['captures'],
            'banks.json': json.loads((tree / 'sub1/sfx/banks.json').read_text())['captures']}
        level = json.loads((tree / 'sub1/level/level.json').read_text())
        prev, sub = level['stale_d_0028a5a4']['previous_capture'], level['sub']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return out + [f'reports: {error!r}']
    for name, got in reports.items():
        if tuple(got) != S1.CAPTURE_NAMES:
            out.append(f'{name} was made over {got}, not the two captures')
    if sub != 1:
        out.append(f'level.json sub {sub}')
    if LV.PREVIOUS.get('area19') != S1.PREVIOUS:
        out.append(f"export_area13_level.PREVIOUS['area19'] is {LV.PREVIOUS.get('area19')}, not {S1.PREVIOUS}")
    if prev != S1.PREVIOUS.name:
        out.append(f'level.json: D_0028A5A4 previous capture {prev} != {S1.PREVIOUS.name}')
    return out


def cursor_problems(caps, before_ram):
    """The cursors and bank bases pinned; every top-list relocation word
    equal to the capture before the load's (`before_ram`: a19c_05)."""
    out = []
    for cap in caps:
        got = tuple(C.u32(cap.ram, a) for a in (C.D_0028A73C, C.D_0028A740, D_0028A59C, D_0028A5A0))
        if got != CURSORS:
            out.append(f'{cap.name}: D_0028A73C / D_0028A740 / D_0028A59C / D_0028A5A0 {[hex(x) for x in got]}')
        for ident, _off in A13.relocation_lists(cap)['top']:
            at = A13.D_0028A490 + 4 * ident
            if C.u32(cap.ram, at) != C.u32(before_ram, at):
                out.append(f'{cap.name}: top-list D_0028A490[{ident:#x}] differs from a19c_05\'s')
    if C.u32(before_ram, C.D_0028A73C) != CURSORS[0] or C.u32(before_ram, C.D_0028A740) == CURSORS[1]:
        out.append('a19c_05: not the sub-0 load under the same top cursor')
    return out


def split_problems(out_tree, area, record=None):
    """sub1.json against the scratch-free trees: the files under
    out_tree/sub1 hash as recorded; side files identical to `area`'s or
    placed (exactly the two reports); nothing else under out_tree."""
    out = []
    try:
        record = record or json.loads((out_tree / 'sub1.json').read_text())
        files, placed, ident = record['files'], record['side_placed'], record['side_identical']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return [f'sub1.json: {error!r}']
    if tuple(record.get('captures', ())) != S1.CAPTURE_NAMES or record.get('sub') != 1:
        out.append('sub1.json captures / sub')
    if sorted(set(placed) | set(ident)) != sorted(SPLIT.SIDE) or set(placed) & set(ident):
        out.append('sub1.json does not list every side file exactly once')
    if sorted(placed) != sorted(REPORTS):
        out.append(f'placed side files {sorted(placed)} != {sorted(REPORTS)}')
    present = {p.relative_to(out_tree).as_posix() for p in out_tree.rglob('*') if p.is_file()}
    want = {f'sub1/{r}' for r in files} | set(placed) | {'sub1.json'}
    if present != want:
        out.append(f'assets/area19s1 holds {sorted(present - want)}, lacks {sorted(want - present)}')
    for rel, row in files.items():
        p = out_tree / 'sub1' / rel
        if p.exists() and sha(p.read_bytes()) != row.get('sha256'):
            out.append(f'sub1/{rel}: hash')
    for name, digest in ident.items():
        p = area / name
        if not p.exists() or sha(p.read_bytes()) != digest:
            out.append(f'{name}: not identical to assets/area19/\'s')
    for name, row in placed.items():
        p = out_tree / name
        if p.exists() and sha(p.read_bytes()) != row.get('sha256'):
            out.append(f'{name}: placed file hash')
        if (area / name).exists() and sha((area / name).read_bytes()) == row.get('sha256'):
            out.append(f'{name}: placed although identical to assets/area19/\'s')
    return out


_SUB_PROOF = {}


def sub_proof_problems(caps, proof):
    out = []
    got = {}
    for c in caps:
        k = (c.name, id(c.ram))
        if k not in _SUB_PROOF:
            _SUB_PROOF[k] = (c, A13.loaded_sub_proof([c])[c.name])
        got[c.name] = _SUB_PROOF[k][1]
    if sorted(proof) != sorted(c.name for c in caps):
        out.append(f'loaded_sub_proof.json captures {sorted(proof)}')
    for cap in caps:
        rows, sub = got[cap.name], cap.ram[0x810701]
        if sub != 1 or not all(rows[sub] * 100 < rows[s] for s in rows if s != sub):
            out.append(f'{cap.name}: its sub-{sub} map does not fit RAM far better ({rows})')
        if proof.get(cap.name) != {f'sub{s}': v for s, v in rows.items()}:
            out.append(f'{cap.name}: loaded_sub_proof.json {proof.get(cap.name)} != {rows}')
    return out


_ROWS = {}


def recording_verify_directory(original):
    """Wrap export_area19s1_common.verify_directory to keep each call's rows."""
    def wrapped(elf, disc, caps):
        rows, problems, calls = original(elf, disc, caps)
        _ROWS[tuple(c.name for c in caps)] = copy.deepcopy(rows)
        return rows, problems, calls
    return wrapped


def rule_problems(caps, record, rows, before_ram):
    """Flag calls, freed drums, cells.json proofs, the fire and 0x1E3D90."""
    out = []
    for cap in caps:
        try:
            calls = [list(c) for c in S1.flag_calls(cap.ram)]
        except ValueError as error:
            calls = repr(error)
        if calls != FLAG_CALLS.get(cap.name):
            out.append(f'{cap.name}: flag calls {calls} != {FLAG_CALLS.get(cap.name)}')
        slots = tuple(s for s, a in T.pool_nodes(cap.ram) if C.u32(cap.ram, a + 0x10) in S1.NO_MODEL)
        if slots != NO_MODEL_SLOTS.get(cap.name):
            out.append(f'{cap.name}: 0x1E3D90 slots {slots} != {NO_MODEL_SLOTS.get(cap.name)}')
        for s in slots:
            a = T.POOL_BASE + s * T.POOL_STRIDE
            if C.u32(cap.ram, a + 0x44) != C.u32(before_ram, a + 0x44):
                out.append(f'{cap.name}: 0x1E3D90 slot {s} +0x44 is not the slot\'s a19c_06 binding')
    if rows is None:
        return out + ['cells.json proofs: no recomputed directory rows']
    stored = {r['capture']: r.get('proofs') for r in record['cells']['captures']}
    got = {r['capture']: {str(u): p for u, p in r['proofs'].items()} for r in rows}
    if sorted(got) != sorted(stored):
        out.append(f'cells.json proof rows {sorted(stored)} != recomputed {sorted(got)}')
    for name, want in got.items():
        if stored.get(name) != want:
            out.append(f'{name}: cells.json proofs differ from the recomputed directory')
    for cap in caps:
        proofs = stored.get(cap.name) or {}
        freed = tuple(sorted(int(u) for u, p in proofs.items() if p == S1.FREED_PROOF))
        if freed != FREED.get(cap.name):
            out.append(f'{cap.name}: freed-drum hulls {freed} != {FREED.get(cap.name)}')
        if any(p.startswith('orphan') or p in ('underived',) for p in proofs.values()):
            out.append(f'{cap.name}: an orphan or underived hull')
    return out


def census_problems(census):
    out = []
    try:
        funcs = {int(f['addr'], 16): f for f in census['functions']}
        beats = [b['beat'] for b in census['per_beat']]
        s = census['summary']
        if s.get('passes') != ['A19C'] or beats != list(S1.SUB0_CAPTURES + S1.CAPTURE_NAMES):
            out.append('census: not the pass A19C over the eight beats')
        runs = s.get('replay_runs', {})
        if isinstance(runs, str):
            runs = {}
        if s.get('beats_missing') != [] or s.get('beats_incomplete') != [] or sorted(runs) != sorted(beats) or \
                not all(r.get('completed') and r.get('error') is None for r in runs.values()):
            out.append('census: a beat missing, incomplete or not replayed to completion')
        b = lambda a: funcs.get(a, {}).get('beats') or []
        if b(0x1FFCD0) != ['a19c_06_ladder959']:
            out.append(f'census: 001FFCD0 ran in {b(0x1FFCD0)}, not a19c_06 alone')
        for o in SUB1_OWNERS:
            if b(o) != list(S1.CAPTURE_NAMES):
                out.append(f'census: the sub-1 owner {o:#x} ran in {b(o)}')
        for o in CALLBACKS_07:
            if b(o) != ['a19c_07_door52']:
                out.append(f'census: the callback {o:#x} ran in {b(o)}')
        for o in SUB0_ONLY:
            if 'a19c_07_door52' in b(o):
                out.append(f'census: the sub-0 owner {o:#x} ran in a19c_07')
        if 'a19c_07_door52' not in b(E1.DRUM):
            out.append('census: the drum 0x156620 did not run in a19c_07')
        if census['unattributed_hits'] or census['overlay_hits_other_overlay']:
            out.append('census: unattributed or other-overlay hits')
    except (KeyError, TypeError, AttributeError, ValueError) as error:
        out.append(f'census: {error!r}')
    return out


def lane_checks(every, caps, out_tree, area, before_ram, census, rows, tree=None):
    tree = tree or T13.A
    out = capture_problems(every, tree)
    out += cursor_problems(caps, before_ram)
    out += split_problems(out_tree, area)
    try:
        record = json.loads((tree / 'sub1/cells.json').read_text())
        proof = json.loads((tree / 'loaded_sub_proof.json').read_text())
    except (OSError, ValueError) as error:
        return out + [f'lane inputs: {error!r}']
    out += sub_proof_problems(caps, proof)
    six = next((c.ram for c in every if c.name == 'a19c_06_ladder959'), before_ram)
    out += rule_problems(caps, record, rows, six)
    out += census_problems(census)
    return out


# ---------------------------------------------------------------------------
# The AREA19X review controls ([43]'s rule per field, creature_nodes' bit 0)


class Sub0:
    """The AREA19X lane's sub-0 state for the duration (swap_nodes reads
    the target's sub)."""

    def __enter__(self):
        S1.uninstall()

    def __exit__(self, *exc):
        configure(T13.A)


def area19x_problems(cap):
    """The AREA19X rule checks on a19b_02 against its exports: the [43] node
    recorded in reload/sub0/world_models.json is X19.swap_nodes' and passes
    X19.explicit_model_problems; the creature node recorded in
    creature.json is XT.creature_nodes' and passes XT.creature_problems."""
    out = []
    try:
        wm = json.loads((X19_RELOAD / 'sub0/world_models.json').read_text())
        crt = json.loads((X19_RELOAD / 'sub0/creature/creature.json').read_text())
        record = crt['ids']['0x70']['model']
        want_swap = wm['explicit_models'][cap.key if hasattr(cap, 'key') else cap.name]
        want_creature = crt['nodes'][cap.key if hasattr(cap, 'key') else cap.name]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return [f'area19x exports: {error!r}']
    with Sub0():
        swaps = [hex(a) for _s, a in X19.swap_nodes(cap.ram)]
        if swaps != want_swap:
            out.append(f'[43] nodes {swaps} != world_models.json {want_swap}')
        out += X19.explicit_model_problems(cap.ram, cap.name)
        nodes = [hex(a) for _s, a in XT.creature_nodes(cap.ram)]
        if nodes != want_creature:
            out.append(f'creature nodes {nodes} != creature.json {want_creature}')
        out += XT.creature_problems(cap, record)
    return out


def area19x_controls(expect):
    """One control per field of the [43] rule's bone check
    (X19.bone_rest_problems) and per bit of creature_nodes' mask."""
    if not A19B_02.exists() or not (X19_RELOAD / 'sub0/world_models.json').exists():
        print('  area19x review controls: SKIPPED (a19b_02 or the AREA19X export missing)')
        return 0
    cap = C.Capture(A19B_02)
    edited = lambda edits: T13.ram_copy(cap, cap.name, edits)
    n0 = expect.count
    expect(area19x_problems(cap), 'area19x: a19b_02 as captured', accept=True)
    bone = lambda k: C.u32(cap.ram, SWAP_NODE + 0x110 + 4 * k)
    for k, off, what in ((0, 0x64, '+0x64 low byte'), (0, 0x65, '+0x64 high byte'), (1, 0x88, 'scale x'),
                         (1, 0x8A, 'scale y'), (1, 0x8D, 'scale z (high byte)'), (2, 0x70, 'the first zero word'),
                         (2, 0x87, 'the last zero byte'), (3, 0x00, 'the matrix\'s first byte'),
                         (3, 0x3F, 'the matrix\'s last byte')):
        expect(area19x_problems(edited([T13.flip(cap.ram, bone(k) + off)])), f'area19x: [43] bone {k} {what}')
    expect(area19x_problems(edited([(SWAP_NODE + 0x110 + 4 * 3, bytes(4))])), 'area19x: [43] bone slot 3 cleared')
    expect(area19x_problems(edited([(CREATURE_NODE + 0x0D, b'\x80')])),
           'area19x: the creature +0x0D without bit 0 (0x80)')
    expect(area19x_problems(edited([(CREATURE_NODE + 0x0D, b'\x01')])),
           'area19x: the creature +0x0D without bit 0x80 (0x01)')
    with Sub0():
        got = [a for _s, a in XT.creature_nodes(edited([(CREATURE_NODE + 0x0D, b'\xff')]).ram)]
    expect([] if got == [CREATURE_NODE] else ['not selected'], 'area19x: the creature +0x0D 0xFF (bits 0x81 set)',
           accept=True)
    return expect.count - n0


# ---------------------------------------------------------------------------
# Controls


class Expect:
    def __init__(self):
        self.count = 0

    def __call__(self, problems, what, accept=False):
        self.count += 1
        check(bool(problems) != accept, f'control: {what} {"rejected" if accept else "missed"}')


def lane_controls(caps, every, before_ram, census, rows, expect):
    by = {c.name: c for c in every}
    six, seven = by['a19c_06_ladder959'], by['a19c_07_door52']
    edited = lambda cap, edits: T13.ram_copy(cap, cap.name, edits)
    tmp = BUILD / f'ctl-{os.getpid()}'
    tview = BUILD / f'ctlview-{os.getpid()}'
    n0 = expect.count

    def tampered(change):
        for d in (tmp, tview):
            if d.exists():
                shutil.rmtree(d)
        shutil.copytree(OUT, tmp)
        change(tmp)
        try:
            build_view(tmp, AREA, tview)
        except (OSError, KeyError, json.JSONDecodeError):
            pass
        return tmp

    def lane(change, census_=None):
        r = tampered(change)
        return lane_checks(every, caps, r, AREA, before_ram, census if census_ is None else census_, rows, tview)

    def edit_json(rel, fn):
        def change(tree):
            p = tree / rel
            d = json.loads(p.read_text())
            fn(d)
            p.write_text(json.dumps(d))
            rec = json.loads((tree / 'sub1.json').read_text())
            if rel.startswith('sub1/'):
                rec['files'][rel[5:]]['sha256'] = sha(p.read_bytes())
            else:
                rec['side_placed'][rel]['sha256'] = sha(p.read_bytes())
            (tree / 'sub1.json').write_text(json.dumps(rec))
        return change
    record = json.loads((OUT / 'sub1/cells.json').read_text())
    try:
        # captures
        expect(capture_problems(every[:-1], T13.A), 'the last capture missing')
        for at in (0x810700, 0x810701, 0x810702):
            expect(capture_problems([edited(c, [T13.flip(c.ram, at)]) if c is seven else c for c in every], T13.A),
                   f'a19c_07 with byte {at:#x} changed')
        others = {n: ((0x13, 0), b'MWo3', 0x10) for n in S1.SUB0_CAPTURES}
        expect(capture_problems(every, T13.A, others), 'the sub-0 captures as read', accept=True)
        bad = dict(others, a19c_05_door27=((0x13, 1), b'MWo3', 0x10))
        expect(capture_problems(every, T13.A, bad), 'a19c_05 read as sub 1')
        expect(capture_problems(every, T13.A, {k: v for k, v in others.items() if k != 'a19c_00_bar840'}),
               'a19c_00 missing')
        saved = LV.PREVIOUS['area19']
        LV.PREVIOUS['area19'] = X19.PREVIOUS
        try:
            expect(capture_problems(every, T13.A), "PREVIOUS['area19'] not re-pointed")
        finally:
            LV.PREVIOUS['area19'] = saved
        for rel, path in (('sub1/level/level.json', ('captures',)), ('sub1/cells.json', ('cells', 'captures')),
                          ('tables.json', ('captures',)), ('sub1/sfx/banks.json', ('captures',))):
            def cut(d, path=path):
                for k in path[:-1]:
                    d = d[k]
                d[path[-1]] = d[path[-1]][:-1]
            expect(lane(edit_json(rel, cut)), f'{rel} made over one capture')
        expect(lane(edit_json('sub1/level/level.json', lambda d: d['stale_d_0028a5a4'].__setitem__(
            'previous_capture', 'a19b_02_ladder1023'))), 'level.json naming another previous capture')
        expect(lane(edit_json('sub1/level/level.json', lambda d: d.__setitem__('sub', 0))), 'level.json sub 0')
        # cursor
        expect(cursor_problems(caps, before_ram), 'the cursors as captured', accept=True)
        for at in (C.D_0028A73C, C.D_0028A740, D_0028A59C, D_0028A5A0):
            expect(cursor_problems([edited(six, [(at, struct.pack('<I', C.u32(six.ram, at) + 0x10))])], before_ram),
                   f'a19c_06 word {at:#x} + 0x10')
        last = A13.relocation_lists(six)['top'][-1][0]
        at = A13.D_0028A490 + 4 * last
        expect(cursor_problems([edited(seven, [(at, struct.pack('<I', C.u32(seven.ram, at) + 0x40))])], before_ram),
               'the last top-list relocation word moved')
        expect(cursor_problems(caps, six.ram), 'a19c_06 read as the capture before the load')
        # split
        expect(split_problems(OUT, AREA), 'the placement as exported', accept=True)

        def flip_file(rel, at=0x40):
            def change(tree):
                p = tree / rel
                b = bytearray(p.read_bytes())
                b[at] ^= 1
                p.write_bytes(bytes(b))
            return change
        expect(split_problems(tampered(flip_file('sub1/level/static_bank.emsc')), AREA), 'a static-bank byte')
        expect(split_problems(tampered(lambda t: (t / 'sub1/world_models.emwm').unlink()), AREA), 'a sub1 file missing')
        expect(split_problems(tampered(lambda t: (t / 'extra.bin').write_bytes(b'x')), AREA), 'an extra file')
        expect(split_problems(tampered(lambda t: shutil.copyfile(AREA / 'scripts.emsc', t / 'scripts.emsc')), AREA),
               'an identical side file copied in')
        expect(split_problems(tampered(lambda t: (t / 'tables.json').unlink()), AREA), 'a placed report missing')

        def side_as_placed(tree):
            rec = json.loads((tree / 'sub1.json').read_text())
            rec['side_placed']['scripts.emsc'] = dict(sha256=rec['side_identical'].pop('scripts.emsc'))
            shutil.copyfile(AREA / 'scripts.emsc', tree / 'scripts.emsc')
            (tree / 'sub1.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(side_as_placed), AREA), 'an identical side file listed as placed')

        def bad_ident(tree):
            rec = json.loads((tree / 'sub1.json').read_text())
            rec['side_identical']['spawn_table.emsp'] = '0' * 64
            (tree / 'sub1.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(bad_ident), AREA), 'an identical side file hash')

        def drop_side(tree):
            rec = json.loads((tree / 'sub1.json').read_text())
            rec['side_identical'].pop('overlay_data.emsc')
            (tree / 'sub1.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(drop_side), AREA), 'a side file not listed')

        def report_as_identical(tree):
            rec = json.loads((tree / 'sub1.json').read_text())
            rec['side_placed'].pop('tables.json')
            rec['side_identical']['tables.json'] = sha((AREA / 'tables.json').read_bytes())
            (tree / 'tables.json').unlink()
            (tree / 'sub1.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(report_as_identical), AREA), 'tables.json listed as identical (sub 0\'s)')
        # sub proof
        proof = json.loads((OUT / 'loaded_sub_proof.json').read_text())
        expect(sub_proof_problems(caps, proof), 'the sub proof as exported', accept=True)
        for cap in caps:
            bad = copy.deepcopy(proof)
            bad[cap.name]['sub0'] += 1
            expect(sub_proof_problems(caps, bad), f'loaded_sub_proof.json {cap.name} sub0 + 1')
        expect(sub_proof_problems([edited(six, [(0x810701, b'\x00')])], {six.name: proof[six.name]}),
               'a capture read as sub 0')
        # rules
        expect(rule_problems(caps, record, rows, six.ram), 'the rules as exported', accept=True)
        lamp = T13.node_of(six.ram, 0x826570)
        lift = T13.node_of(six.ram, 0x826C10)
        door = T13.node_of(six.ram, 0x829A70)
        note = T13.node_of(six.ram, S1.NOTE_34)
        expect(rule_problems([edited(six, [(lamp + 4, b'\x02')])], record, rows, six.ram), '[38] in state 2')
        expect(rule_problems([edited(six, [(lift + 4, b'\x02')])], record, rows, six.ram), '[36] in state 2')
        expect(rule_problems([edited(six, [(lift + 4, b'\x00')])], record, rows, six.ram), '[36] in state 0')
        expect(rule_problems([edited(six, [(door + 4, b'\x02')])], record, rows, six.ram), '[46] in state 2')
        expect(rule_problems([edited(six, [(note + 5, b'\x01')])], record, rows, six.ram), '[34] with its chain running')
        expect(rule_problems([edited(six, [(note + 4, b'\x03')])], record, rows, six.ram), '[34] in state 3')
        expect(rule_problems([edited(six, [(note, b'\x00')])], record, rows, six.ram),
               '[34] freed without flag 0x46')
        expect(rule_problems([edited(seven, [(S1.FLAG_46, b'\x01')])], record, rows, six.ram),
               'a19c_07 with flag 0x46 = 1')

        def calls_are(cap, want, what):
            try:
                got = [list(c) for c in S1.flag_calls(cap.ram)]
            except ValueError as error:
                got = repr(error)
            expect([] if got == want else [f'{got}'], f'flag calls: {what}', accept=True)
        calls_are(edited(six, [(lift + 4, b'\x01')]), [[7, 1], [0x15, 1]], '[36] in state 1: (0x15, 1)')
        calls_are(edited(six, [(lamp + 4, b'\x00')]), [[0x15, 1]], '[38] in state 0: no call')
        calls_are(edited(six, [(door + 4, b'\x02')]), [[7, 1], [0x15, 1], [5, 0]], '[46] in state 2: (5, 0)')
        calls_are(edited(six, [(note + 4, b'\x03')]), [[7, 1], [0x15, 1], [8, 1]], '[34] in state 3: (8, 1)')
        calls_are(edited(six, [(note, b'\x00'), (S1.FLAG_46, b'\xff')]), [[7, 1], [0x15, 1], [8, 1]],
                  '[34] freed with flag 0x46 = 0xFF: (8, 1)')
        calls_are(edited(six, [(0x810701, b'\x00')]), [], 'a19c_06 read as sub 0: the AREA13 lane\'s rule (not '
                  'its target sub: no call)')
        flames = T13.node_of(six.ram, FLAMES)
        expect(field_problems((OUT / 'sub1/roster.emro').read_bytes(), six.ram), '[40] fields as captured', accept=True)
        for off in FLAMES_FIELDS:
            expect(field_problems((OUT / 'sub1/roster.emro').read_bytes(),
                                  edited(six, [T13.flip(six.ram, flames + off)]).ram), f'[40] +{off:#x} changed')
        expect(field_problems((OUT / 'sub1/roster.emro').read_bytes(), edited(six, [T13.flip(six.ram, flames + 0x54)]).ram),
               '[40] +0x54 changed')
        two = T13.node_of(seven.ram, 0x826570)
        free = next(s for s in range(T.POOL_SLOTS) if not seven.ram[T.POOL_BASE + s * T.POOL_STRIDE])
        nf = T.POOL_BASE + free * T.POOL_STRIDE
        expect(rule_problems([edited(seven, [(nf, seven.ram[two:two + T.POOL_STRIDE])])], record, rows, six.ram),
               'two [38] nodes')
        try:
            S1.flag_calls(edited(seven, [(nf, seven.ram[two:two + T.POOL_STRIDE])]).ram)
            refused = []
        except ValueError:
            refused = ['refused']
        expect(refused, 'flag calls with two [38] nodes refused (ValueError)')
        sub0 = edited(six, [(0x810701, b'\x00')])
        expect([] if S1.owner_matrix(sub0.ram, lift) is None else ['a matrix'],
               'owner_matrix of [36] in a capture read as sub 0: the AREA13 lane\'s (none)', accept=True)
        expect([] if S1.owner_matrix(six.ram, lift) == lift + 0xD0 else ['not node + 0xD0'],
               'owner_matrix of [36]: node + 0xD0', accept=True)
        extra = T.POOL_BASE + 85 * T.POOL_STRIDE
        fx80 = T.POOL_BASE + 80 * T.POOL_STRIDE
        node85 = bytearray(seven.ram[fx80:fx80 + T.POOL_STRIDE])
        node85[0x44:0x48] = six.ram[extra + 0x44:extra + 0x48]
        expect(rule_problems([edited(seven, [(extra, bytes(node85))])], record, rows, six.ram),
               'a seventh 0x1E3D90 node (slot 85, its +0x44 as in a19c_06)')
        fx = T.POOL_BASE + 42 * T.POOL_STRIDE
        expect(rule_problems([edited(seven, [(fx + 0x44, struct.pack('<I', C.u32(seven.ram, fx + 0x44) + 0x40))])],
                             record, rows, six.ram), 'a 0x1E3D90 node with another +0x44')
        expect(rule_problems(caps, record, None, six.ram), 'no recomputed directory rows')
        rec1 = copy.deepcopy(record)
        rec1['cells']['captures'][0]['proofs']['44'] = '001A2370(node 0x7add60, behaviour 0x827430)'
        expect(rule_problems(caps, rec1, rows, six.ram), 'cells.json naming another owner for hull 44')
        rec2 = copy.deepcopy(record)
        rec2['cells']['captures'][1]['proofs'].pop('23')
        expect(rule_problems(caps, rec2, rows, six.ram), 'cells.json without the freed hull 23')
        rows2 = copy.deepcopy(rows)
        rows2[1]['proofs'].pop(23)
        expect(rule_problems(caps, rec2, rows2, six.ram), 'hull 23 not freed in record and rows')
        rec3, rows3 = copy.deepcopy(record), copy.deepcopy(rows)
        rec3['cells']['captures'][0]['proofs']['24'] = S1.FREED_PROOF
        rows3[0]['proofs'][24] = S1.FREED_PROOF
        expect(rule_problems(caps, rec3, rows3, six.ram), 'a freed hull in a19c_06 (record and rows)')
        rec4, rows4 = copy.deepcopy(record), copy.deepcopy(rows)
        rec4['cells']['captures'][0]['proofs']['24'] = 'orphan: equal to a derivation of it in another capture'
        rows4[0]['proofs'][24] = 'orphan: equal to a derivation of it in another capture'
        expect(rule_problems(caps, rec4, rows4, six.ram), 'a19c_06 hull 24 an orphan in record and rows')
        # the freed-drum rule itself (export_area19s1_common.freed_drum_problems)
        elf = C.read_elf()
        table = C.u32(seven.spad, LV.L.SPAD_CELLS)
        disc = (OUT / 'sub1/area19_cells.bin').read_bytes()
        words = E1.E02.derive_words(elf, disc, seven)[0]
        _n, hulls, _z = LV.L.cell_directory(disc, 0)
        expect(S1.freed_drum_problems(elf, words, seven, 23, hulls), 'freed hull 23 as captured', accept=True)
        expect(S1.freed_drum_problems(elf, words, seven, 40, hulls), 'hull 40 ([35]: live, not a 0x827B60 uid)')
        expect(S1.freed_drum_problems(elf, words, six, 23, hulls), 'hull 23 in a19c_06 (unmoved)')
        expect(S1.freed_drum_problems(elf, words, edited(seven, [(S1.COUNTER_46, b'\x00')]), 23, hulls),
               'a freed hull with D_0081081E = 0')
        expect(S1.freed_drum_problems(elf, words, edited(seven, [(0x810701, b'\x00')]), 23, hulls),
               'a freed hull outside sub 1')
        s23, _e23, _f = hulls[23]
        _w = sorted(S1.E02L.written_words(elf, words, seven, 23, hulls))
        unwritten = next(k for k in range(0, _e23 - s23, 4) if k not in _w)
        expect(S1.freed_drum_problems(elf, words, edited(seven, [T13.flip(seven.ram, table + s23 + unwritten)]), 23,
                                      hulls), 'a freed hull with a word 001A2370 does not write changed')
        n30 = next(a for _s, a in T.pool_nodes(six.ram) if six.ram[a + 0x0F] == 23)
        live = edited(seven, [(nf, six.ram[n30:n30 + T.POOL_STRIDE])])
        expect(S1.freed_drum_problems(elf, words, live, 23, hulls), 'hull 23 with a live node carrying its uid')
        expect(S1.freed_drum_problems(elf, words, seven, 9, hulls), 'hull 9 (a freed 0x827B60 uid, not moved)')
        n40 = next(a for _s, a in T.pool_nodes(seven.ram) if seven.ram[a + 0x0F] == 40)
        expect(S1.freed_drum_problems(elf, words, edited(seven, [(n40, b'\x00')]), 40, hulls),
               'hull 40 with [35] freed (not a 0x827B60 uid)')
        bad7 = edited(seven, [T13.flip(seven.ram, table + s23 + unwritten)])
        expect(S1.verify_directory(elf, disc, [six, bad7])[1], 'the directory wrapper on a freed hull with a word '
               '001A2370 does not write changed')
        # the fire's words (export_area19s1_common.writer_problems)
        fire = T13.node_of(six.ram, S1.FIRE)
        lo, hi = S1.FIRE_LO, S1.FIRE_HI
        module = T01.disc_reader()(lo, hi - lo)
        expect(S1.writer_problems(six.ram, lo, hi, module, S1.FIRE), 'the fire window as captured', accept=True)
        for at in list(S1.FIRE_WORDS) + list(S1.FIRE_CONSTANTS):
            expect(S1.writer_problems(edited(six, [T13.flip(six.ram, at + 2)]).ram, lo, hi, module, S1.FIRE),
                   f'the fire word {at:#x} changed')
        expect(S1.writer_problems(edited(six, [T13.flip(six.ram, 0x82B008)]).ram, lo, hi, module, S1.FIRE),
               'a word between the fire\'s words changed')
        expect(S1.writer_problems(edited(six, [(fire + 4, b'\x02')]).ram, lo, hi, module, S1.FIRE), 'the fire in state 2')
        expect(S1.writer_problems(edited(six, [(fire + 0x214, struct.pack('<f', 0.5))]).ram, lo, hi, module, S1.FIRE),
               'the fire\'s size 0.5')
        expect(S1.writer_problems(edited(six, [(0x8107F9, bytes([six.ram[0x8107F9] | 0x80]))]).ram, lo, hi, module,
                                  S1.FIRE), 'D_008107F9 bit 7 set')
        expect(S1.writer_problems(edited(six, [(0x810702, b'\x02')]).ram, lo, hi, module, S1.FIRE), 'entry 2')
        expect(S1.writer_problems(edited(six, [(fire, b'\x00')]).ram, lo, hi, module, S1.FIRE), 'the fire freed')
        expect(S1.writer_problems(edited(six, [(lo, module)]).ram, lo, hi, module, S1.FIRE),
               'the window as the module bytes (no fire needed)', accept=True)
        # chain facts
        ov = C.read_overlay()
        expect(chain_problems(ov), 'chain 0x82E090 as on the disc', accept=True)
        olo = E2.E02.overlay_data_window(ov)[0]
        recs = T.walk_chain(ov[olo - C.OVERLAY_ARENA:], olo, NOTE_CHAIN)
        for k, (off, val, what) in enumerate(((0, 0x40000000, 'a jump bit (to the next record)'),
                                               (0x14, 0x45, 'the stop on flag 0x45'), (0, 0x6, 'the stop op 6'))):
            o = bytearray(ov)
            r = recs[-1] if k else recs[1]
            at = r - C.OVERLAY_ARENA + off
            struct.pack_into('<I', o, at, (C.u32(ov, at) | val) if k == 0 else
                             (val if off else (C.u32(ov, at) & ~0xFF) | val))
            if k == 0:
                struct.pack_into('<I', o, at + 4, recs[2])
            expect(chain_problems(bytes(o)), f'chain 0x82E090 with {what}')
        cb = next(r for r in recs if struct.pack('<I', NOTE_CALLBACK) in ov[r - C.OVERLAY_ARENA:r - C.OVERLAY_ARENA + 0x40])
        o = bytearray(ov)
        k = ov.find(struct.pack('<I', NOTE_CALLBACK), cb - C.OVERLAY_ARENA)
        struct.pack_into('<I', o, k, 0x827B30)
        expect(chain_problems(bytes(o)), 'chain 0x82E090 without the 0x827B20 record')
        # census
        expect(census_problems(census), 'the census as recorded', accept=True)
        edits = [
            ('001FFCD0 also in a19c_07', lambda d: next(f for f in d['functions'] if f['addr'] == '0x1ffcd0')[
                'beats'].append('a19c_07_door52')),
            ('[36] absent', lambda d: d['functions'].remove(next(f for f in d['functions'] if f['addr'] == '0x826c10'))),
            ('the fire also in a19c_05', lambda d: next(f for f in d['functions'] if f['addr'] == '0x823d10')[
                'beats'].insert(0, 'a19c_05_door27')),
            ('0x827B20 in a19c_06', lambda d: next(f for f in d['functions'] if f['addr'] == '0x827b20')[
                'beats'].insert(0, 'a19c_06_ladder959')),
            ('[9] in a19c_07', lambda d: next(f for f in d['functions'] if f['addr'] == '0x825c70')[
                'beats'].append('a19c_07_door52')),
            ('the drum not in a19c_07', lambda d: next(f for f in d['functions'] if f['addr'] == '0x156620')[
                'beats'].remove('a19c_07_door52')),
            ('an unattributed hit', lambda d: d['unattributed_hits'].append('0x00500000')),
            ('an other-overlay hit', lambda d: d['overlay_hits_other_overlay'].append(dict(overlay_id=10))),
            ('a second pass name', lambda d: d['summary']['passes'].append('A19B')),
            ('a beat dropped', lambda d: d['per_beat'].pop()),
            ('a beat missing', lambda d: d['summary']['beats_missing'].append('a19c_07_door52')),
            ('a replay error', lambda d: d['summary']['replay_runs']['a19c_06_ladder959'].__setitem__('error', 'x')),
            ('a replay not completed', lambda d: d['summary']['replay_runs']['a19c_07_door52'].__setitem__(
                'completed', False))]
        for what, fn in edits:
            d = copy.deepcopy(census)
            fn(d)
            expect(census_problems(d), f'census: {what}')
        # lane_checks itself on a tampered tree
        expect(lane(lambda t: None), 'lane_checks on an untouched copy', accept=True)
        hit = copy.deepcopy(census)
        hit['unattributed_hits'].append('0x00500000')
        expect(lane(lambda t: None, hit), 'lane_checks with an unattributed census hit')
        expect(lane(edit_json('loaded_sub_proof.json', lambda d: d['a19c_07_door52'].__setitem__('sub1', 10 ** 6))),
               'lane_checks with loaded_sub_proof.json a19c_07 sub1 = 10^6')
        expect(lane(edit_json('sub1/cells.json', lambda d: d['cells']['captures'][1]['proofs'].__setitem__(
            '23', 'orphan: equal to a derivation of it in another capture'))), 'lane_checks with an orphan 23')
        expect(lane(flip_file('sub1/area19_cells.bin', 0x10)), 'lane_checks with a cells byte (sub1.json stale)')
    finally:
        for d in (tmp, tview):
            if d.exists():
                shutil.rmtree(d)
    return expect.count - n0


def census_site_controls(K, caps, expect):
    """census_sites (the replacement inside the AREA13 checker) on changed
    inputs."""
    n0 = expect.count
    expect(census_sites(K, caps), 'the call-site census as captured', accept=True)
    six = caps[0]
    lamp = T13.node_of(six.ram, 0x826570)
    fake = T13.ram_copy(six, six.name, [(lamp + 0x10, struct.pack('<I', 0x825C70))])
    expect(census_sites(K, [fake]), 'a live node of the sub-0 owner [9]')
    places, groups = T13.roster_records((T13.A / 'sub1/roster.emro').read_bytes())
    p2 = [bytearray(r) for r in places]
    struct.pack_into('<I', p2[0], 0x24, 0x827DD0)
    expect(census_sites(K, caps, [bytes(r) for r in p2], groups), 'a sub-1 placement of the creature 0x827DD0')
    saved = dict(S1.FLAG_STATES)
    S1.FLAG_STATES.pop(0x829A70)
    try:
        expect(census_sites(K, caps), '[46] not modelled')
    finally:
        S1.FLAG_STATES.clear()
        S1.FLAG_STATES.update(saved)
    saved_cb = T13.CALLBACKS['area19']
    T13.CALLBACKS['area19'] = {0x827B20: (0x82E090, 0x826570)}
    try:
        expect(census_sites(K, caps), 'the callback started by [38]')
    finally:
        T13.CALLBACKS['area19'] = saved_cb
    return expect.count - n0


# ---------------------------------------------------------------------------


def main():
    need = [OUT / 'sub1.json', AREA / 'tables.json', C.ELF_PATH, C.ISO_PATH, S1.TARGET.overlay_path, CENSUS,
            S1.PREVIOUS / 'eeMemory.bin'] + [S1.ROUTE_A19C / n / 'eeMemory.bin' for n in S1.CAPTURE_NAMES]
    missing = [str(p) for p in need if not p.exists()]
    if missing:
        print('area19s1 assets reference: SKIPPED, missing local inputs:', missing)
        return 0
    t0 = time.time()
    try:
        build_view(OUT, AREA, VIEW)
    except (OSError, KeyError, json.JSONDecodeError) as error:
        print(f'area19s1 assets reference: FAIL (sub1.json: {error!r})')
        return 1
    try:
        configure(VIEW)
        el = T13.L.load_export_level()
        lib = T01.build_loaders()
        bg = T13.background_lib()
        K, caps = T13.target_setup('area19', el)
        print(f'area19s1 assets reference ({MODE}) AREA19 sub 1: {len(caps)} of {len(K.every)} captures')
        loaded = T13.check_loaders(lib, bg, K.t, VIEW)
        for lname, ok in loaded.items():
            check(ok, f'port loader rejects {lname}')
        files = [k for k in loaded if k not in ('cells refused (bit 29)', 'cells (bit 29 cleared)')]
        print(f'  loaders: {sum(loaded[k] for k in files)}/{len(files)} files accepted by the port loaders; the cells '
              f'file refused (bit 29): {loaded.get("cells refused (bit 29)")}, its bit-29-cleared copy loads: '
              f'{loaded.get("cells (bit 29 cleared)")}')
        _ROWS.clear()
        original = LV.verify_directory
        LV.verify_directory = recording_verify_directory(original)
        try:
            problems, V = T13.run_checks(K, caps)
        finally:
            LV.verify_directory = original
        for p in problems:
            check(False, f'area19 sub 1: {p}')
        lv = V.get('level', {})
        print(f"  AREA13 lane's AREA19 checks (sub 1): {lv.get('objects')} bank objects -> {lv.get('zones')} zone "
              f"EMDL(s), {lv.get('kicks')} level kicks; collision, cells, tables, sfx, ctx: {len(problems)} problems")
        rows = _ROWS.get(tuple(c.name for c in caps))
        before = (S1.PREVIOUS / 'eeMemory.bin').read_bytes()
        census = json.loads(CENSUS.read_text())
        lp = lane_checks(K.every, caps, OUT, AREA, before, census, rows)
        for p in lp:
            check(False, f'lane: {p}')
        print(f'  lane checks (captures, cursor, split, sub proof, rules, census): {len(lp)} problems')
        expect = Expect()
        x = area19x_controls(expect)
        print(f'  area19x review controls: {x} ([43] per field, creature_nodes per bit), each caught (or accepted)')
        if FAILS:
            print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
        else:
            T13.use(K)
            print(f'  canary: {T13.canary(K, lib, bg, caps[0])} sections each reported their planted difference')
            if not FAILS:
                n = lane_controls(caps, K.every, before, census, rows, expect)
                n += census_site_controls(K, caps, expect)
                print(f'  controls: {n} changed inputs, each caught (or accepted where marked)')
    finally:
        if VIEW.exists():
            shutil.rmtree(VIEW)
    print(f'area19s1 assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
