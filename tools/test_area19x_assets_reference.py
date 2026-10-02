#!/usr/bin/env python3
"""Check the AREA19X export (assets/area19/reload/ over assets/area19/,
docs/AREA19X_ASSETS.md) against the a19b captures, and load each file
through the port's own loader.

The target is AREA19 sub 0 in its second load (export_area19x_common): the
twelfth level's a19b_00_arrival, a19b_01_ledge and a19b_02_ladder1023
(../Extermination/build/s87/route_a19b/). The checked tree is a view: every
file reload.json lists as identical is the first load's file
(assets/area19/), every differing or added one the reload/ file. Missing
exports, ELF, ISO, overlay, captures or census print SKIPPED.

The checks are the AREA13 lane's AREA19 checks (tools/test_area13_assets_
reference.py, imported UNCHANGED, run_checks + its port loaders + canary,
re-pointed at the view, the three captures and this lane's pins, with
export_area19x_common's [43] rule installed): the load map, level,
collision, cell directory (the ORIGINAL 001A2370 / 0x219F50 / 0019C6F0),
tables, sound, ctx block (the ORIGINAL 001D8FD0 / 001D1C50). This lane adds:
  captures  exactly the three pinned captures, area bytes 13 00 0A; the
            eight a13d folders excluded; level.json, cells.json,
            tables.json, banks.json and creature.json made over all three;
            D_0028A5A4's previous capture a13d_07
  cursor    D_0028A73C / D_0028A740 and the bank bases D_0028A59C /
            D_0028A5A0 pinned in every capture and in a19_02 (the first
            load); every relocation word of both lists 0x1B80 above a19_02's
  split     reload.json accounts for every file of assets/area19/ (reload/
            aside) exactly once; identical files are absent from reload/ and
            hash as recorded; the differing binaries differ only in the
            pinned base words; the differing reports and the added files are
            the pinned sets; no other file
  sub proof loaded_sub_proof.json = export_area13_common.loaded_sub_proof of
            the captures (sub 0 fitting RAM over 100x better)
  rules     [43]'s model-0x25 rule holds exactly in a19b_02; cells.json's
            proofs = the rows the directory derivation recomputed in this run;
            no orphan; the pickup g[2] (taken in the first load at a19_01) is
            not live and its hull 49 is not moved in any capture, every other
            group record live as pinned
  creature  creature.json and id70.emsc / id71.emsc against RAM and the
            relocation words of every capture; the 0012E3A0 node of a19b_02
            (D_008106C0) bound to them (export_area19x_tables.creature_problems)
  census    the a19b delta: the pass A19B over the three beats, replays
            complete; 001FFCD0, 0019C6F0 and 0x219F50 ran in a19b_00 only;
            no sub-1 owner, no 0x827B20; of the call-site owners only
            modelled ones ran; the only other-overlay hits are the four
            AREA13 hits at a19b_00 frame 1 (TWELFTH_LEVEL_ROUTE.md section 5)
  controls  a changed input per lane check; lane_checks itself on tampered
            copies of the reload tree
Default and EM_TEST_FULL=1: all three captures (they are the whole set).
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
import export_area19x_common as X19  # noqa: E402  (first: re-points the AREA13 lane's AREA19 target)
import export_area19x_split as SPLIT  # noqa: E402
import export_area19x_tables as XT  # noqa: E402
import test_area13_assets_reference as T13  # noqa: E402

X19.install()
A13, C, LV, TB = X19.A13, X19.C, X19.LV, X19.TB
E1, E2, T01, T = T13.E1, T13.E2, T13.T01, T13.T
FULL, MODE = T13.FULL, T13.MODE
FAILS, check = T13.FAILS, T13.check

RELOAD = Path(os.environ.get('EM_AREA19X_ASSETS') or X19.OUT).resolve()
BASE = X19.A19_TREE.resolve()
BUILD = C.ROOT / 'build/area19x/assets/test'
VIEW = BUILD / f'view-{os.getpid()}'
CENSUS = C.DECOMP / 'build/s87/census/a19b_delta.json'
FIRST_RAM = X19.FIRST_LAST / 'eeMemory.bin'          # a19_02: the first load's relocation words
GRID = 0x1900500                                     # D_0028A598 of the second load (a19_02: 0x18FE980)
SHIFT = X19.CURSOR - X19.FIRST_CURSOR                # 0x1B80
AREA_BYTES = {n: (0x13, 0, 0x0A) for n in X19.CAPTURE_NAMES}
D_0028A59C, D_0028A5A0 = 0x28A59C, 0x28A5A0
MODEL_BANK = (0x133A640, 0x133C1C0)                  # (first load, second load)
STATIC_BANK = (0x1629180, 0x162AD00)                 # = D_0028A740 in both loads
# (placement nodes live, of them at their record's position and rotation),
# model owners bound (the swapped [43] counted by its own rule)
# model owners bound (the swapped [43] counted by its own rule). In a19b_02
# placements 6 (behaviour 0x8250F0) and 35 (0x1E7D20) have left their
# record's position, and [43]'s child 0x1C5760 (model 0x1B) is gone: [43]'s
# swap sets the child's +4 = 3 and its own +0x2EC = 0
# (func_overlay_AREA19_00825590.c)
ROSTER_LIVE = {'a19b_00_arrival': (54, 53), 'a19b_01_ledge': (54, 53), 'a19b_02_ladder1023': (54, 51)}
MODEL_OWNERS = {'a19b_00_arrival': 41, 'a19b_01_ledge': 41, 'a19b_02_ladder1023': 40}
SWAP_CAPS = ('a19b_02_ladder1023',)                  # [43] after its swap (counter 0x1D 0 -> 3 at f1710)
CREATURE_NODES = {'a19b_00_arrival': [], 'a19b_01_ledge': [], 'a19b_02_ladder1023': ['0x7bdcf0']}
GROUP, PICKUP_G2 = 0x829E00, 2                       # AREA19_ASSETS.md: the pickup the a19_01 Use took
HULL_G2 = 49                                         # its hull uid (its node's +0x0F in a13_05, a19_00)
DEAD_GROUPS = (2, 29, 30)                            # group records with no live node (also in a19_02)
WORD_ROWS = {'sub0/level/static_bank.emsc': [['0x8', hex(STATIC_BANK[0]), hex(STATIC_BANK[1])],
                                              ['0xc', hex(STATIC_BANK[0]), hex(STATIC_BANK[1])]],
             'sub0/world_models.emwm': [['0x8', hex(MODEL_BANK[0]), hex(MODEL_BANK[1])]]}
REPORTS = ('loaded_sub_proof.json', 'sub0/cells.json', 'sub0/level/level.json', 'sub0/sfx/banks.json',
           'sub0/world_models.json', 'tables.json')
ADDED = ('sub0/creature/creature.json', 'sub0/creature/id70.emsc', 'sub0/creature/id71.emsc')
# the a19b census: where the loader and the directory writers ran
RAN_ONLY_IN_ARRIVAL = (0x1FFCD0, 0x19C6F0, 0x219F50)


def configure(view=None):
    """Point the AREA13 checker at this lane (idempotent)."""
    X19.install()
    T13.A = view or VIEW
    T13.BUILD = BUILD
    T13.CANARY = BUILD / f'canary-{os.getpid()}'
    T01.BUILD = BUILD
    T13.QUICK['area19'] = X19.CAPTURE_NAMES
    T13.EXCLUDED['area19'] = list(X19.EXCLUDED)
    T13.GRID['area19'] = GRID
    T13.ROSTER_LIVE.update(ROSTER_LIVE)
    T13.MODEL_OWNERS.update(MODEL_OWNERS)


configure()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_view(reload_tree, base, view):
    """A tree of symlinks: reload.json's identical files from `base`, its
    differing and added ones from `reload_tree`. Returns the record."""
    record = json.loads((reload_tree / 'reload.json').read_text())
    if view.exists():
        shutil.rmtree(view)
    for rel in record['identical']:
        (view / rel).parent.mkdir(parents=True, exist_ok=True)
        (view / rel).symlink_to(base / rel)
    for rel in list(record['differing']) + list(record['added']):
        (view / rel).parent.mkdir(parents=True, exist_ok=True)
        (view / rel).symlink_to(reload_tree / rel)
    return record


# ---------------------------------------------------------------------------
# This lane's checks


def capture_problems(every, tree, excluded=None):
    out = []
    names = tuple(c.name for c in every)
    if names != X19.CAPTURE_NAMES:
        out.append(f'AREA19 captures {names} != {X19.CAPTURE_NAMES}')
    for cap in every:
        got = tuple(cap.ram[0x810700:0x810703])
        if got != AREA_BYTES.get(cap.name):
            out.append(f'{cap.name}: area bytes {got} != {AREA_BYTES.get(cap.name)}')
    if excluded is None:
        excluded = [n for n, _a in A13.excluded_captures(X19.TARGET)]
    if sorted(excluded) != sorted(X19.EXCLUDED):
        out.append(f'excluded captures {sorted(excluded)} != {sorted(X19.EXCLUDED)}')
    try:
        reports = {
            'level.json': json.loads((tree / 'sub0/level/level.json').read_text())['captures'],
            'cells.json': [r['capture'] for r in json.loads((tree / 'sub0/cells.json').read_text())['cells']['captures']],
            'tables.json': json.loads((tree / 'tables.json').read_text())['captures'],
            'banks.json': json.loads((tree / 'sub0/sfx/banks.json').read_text())['captures'],
            'creature.json': json.loads((tree / 'sub0/creature/creature.json').read_text())['captures']}
        prev = json.loads((tree / 'sub0/level/level.json').read_text())['stale_d_0028a5a4']['previous_capture']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return out + [f'reports: {error!r}']
    for name, got in reports.items():
        if tuple(got) != X19.CAPTURE_NAMES:
            out.append(f'{name} was made over {got}, not the three captures')
    if LV.PREVIOUS.get('area19') != X19.PREVIOUS:
        out.append(f"export_area13_level.PREVIOUS['area19'] is {LV.PREVIOUS.get('area19')}, not {X19.PREVIOUS}")
    if prev != X19.PREVIOUS.name:
        out.append(f'level.json: D_0028A5A4 previous capture {prev} != {X19.PREVIOUS.name}')
    return out


def cursor_problems(caps, first_ram):
    """The cursors, bank bases and every relocation word (top and nested
    lists) against the first load's (`first_ram`: a19_02's RAM)."""
    out = []
    lists = A13.relocation_lists(caps[0])
    idents = [i for i, _o in lists['top']] + [i for i, _o in lists['nested']]
    for ram, name, k in [(c.ram, c.name, 1) for c in caps] + [(first_ram, 'a19_02', 0)]:
        want = (X19.CURSOR if k else X19.FIRST_CURSOR, STATIC_BANK[k], MODEL_BANK[k], STATIC_BANK[k])
        got = tuple(C.u32(ram, a) for a in (C.D_0028A73C, C.D_0028A740, D_0028A59C, D_0028A5A0))
        if got != want:
            out.append(f'{name}: D_0028A73C / D_0028A740 / D_0028A59C / D_0028A5A0 {[hex(x) for x in got]} '
                       f'!= {[hex(x) for x in want]}')
    for cap in caps:
        for ident in idents:
            at = A13.D_0028A490 + 4 * ident
            if C.u32(cap.ram, at) - C.u32(first_ram, at) != SHIFT:
                out.append(f'{cap.name}: D_0028A490[{ident:#x}] is not the first load\'s + {SHIFT:#x}')
    return out


def word_rows(new, old):
    """[[offset, old word, new word]] (hex) of every differing aligned word
    of two equal-length blobs (None for unequal lengths); scanned in 4 KB
    pieces, word by word only inside a differing piece."""
    if len(new) != len(old):
        return None
    out = []
    for lo in range(0, len(new), 0x1000):
        if new[lo:lo + 0x1000] == old[lo:lo + 0x1000]:
            continue
        for k in range(lo, min(lo + 0x1000, len(new) - 3), 4):
            if new[k:k + 4] != old[k:k + 4]:
                out.append([hex(k), hex(struct.unpack_from('<I', old, k)[0]), hex(struct.unpack_from('<I', new, k)[0])])
    return out


def split_problems(reload_tree, base, record=None):
    """reload.json against both trees."""
    out = []
    try:
        record = record or json.loads((reload_tree / 'reload.json').read_text())
        ident, diff, added = record['identical'], record['differing'], record['added']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return [f'reload.json: {error!r}']
    files = SPLIT.tree_files(base)
    if sorted(set(ident) | set(diff)) != sorted(files) or set(ident) & set(diff) or set(added) & set(files):
        out.append('reload.json does not list every file of the first load\'s tree exactly once')
    if tuple(record.get('captures', ())) != X19.CAPTURE_NAMES:
        out.append('reload.json captures')
    present = {p.relative_to(reload_tree).as_posix() for p in reload_tree.rglob('*') if p.is_file()}
    if present != set(diff) | set(added) | {'reload.json'}:
        out.append(f'reload/ holds {sorted(present - set(diff) - set(added) - {"reload.json"})}, lacks '
                   f'{sorted((set(diff) | set(added)) - present)}')
    for rel, digest in ident.items():
        if rel in files and sha(files[rel].read_bytes()) != digest:
            out.append(f'{rel}: identical file hash')
    if sorted(r for r in diff if r not in WORD_ROWS) != sorted(REPORTS):
        out.append(f'differing report files {sorted(r for r in diff if r not in WORD_ROWS)} != {sorted(REPORTS)}')
    if sorted(added) != sorted(ADDED):
        out.append(f'added files {sorted(added)} != {sorted(ADDED)}')
    for rel, row in added.items():
        if rel in present and sha((reload_tree / rel).read_bytes()) != row.get('sha256'):
            out.append(f'{rel}: added file hash')
    for rel, row in diff.items():
        if rel not in files or rel not in present:
            continue
        new, old = (reload_tree / rel).read_bytes(), files[rel].read_bytes()
        if sha(new) != row.get('sha256') or sha(old) != row.get('first_sha256'):
            out.append(f'{rel}: differing file hashes')
        if rel not in WORD_ROWS:
            continue
        got = word_rows(new, old)
        if got != WORD_ROWS[rel]:
            out.append(f'{rel}: differing words {got} != {WORD_ROWS[rel]}')
        elif row.get('words') != WORD_ROWS[rel]:
            out.append(f'{rel}: reload.json words {row.get("words")} != {WORD_ROWS[rel]}')
    return out


_SUB_PROOF = {}


def recomputed_sub_proof(caps):
    """export_area13_common.loaded_sub_proof of `caps` (it measures each
    capture on its own), memoised per capture on its RAM object (a changed
    RAM copy is a new object, kept alive with its entry)."""
    out = {}
    for c in caps:
        k = (c.name, id(c.ram))
        if k not in _SUB_PROOF:
            _SUB_PROOF[k] = (c, A13.loaded_sub_proof([c])[c.name])
        out[c.name] = _SUB_PROOF[k][1]
    return out


def sub_proof_problems(caps, proof):
    """loaded_sub_proof.json against export_area13_common.loaded_sub_proof."""
    out = []
    got = recomputed_sub_proof(caps)
    if sorted(proof) != sorted(c.name for c in caps):
        out.append(f'loaded_sub_proof.json captures {sorted(proof)}')
    for cap in caps:
        rows, sub = got[cap.name], cap.ram[0x810701]
        if sub != 0 or not all(rows[sub] * 100 < rows[s] for s in rows if s != sub):
            out.append(f'{cap.name}: its sub-{sub} map does not fit RAM far better ({rows})')
        if proof.get(cap.name) != {f'sub{s}': v for s, v in rows.items()}:
            out.append(f'{cap.name}: loaded_sub_proof.json {proof.get(cap.name)} != {rows}')
    return out


_ROWS = {}


def recording_verify_directory(original):
    """Wrap export_area13_level.verify_directory to keep the rows of each call."""
    def wrapped(elf, disc, caps):
        rows, problems, calls = original(elf, disc, caps)
        _ROWS[tuple(c.name for c in caps)] = copy.deepcopy(rows)
        return rows, problems, calls
    return wrapped


def group_live(ram, roster):
    """[bool] per record of the first group (GROUP), by test_area01's match_nodes."""
    _places, groups = T13.roster_records(roster)
    address, records = groups[0]
    if address != GROUP:
        raise ValueError(f'first group {address:#x}, not {GROUP:#x}')
    sources = [(f'g[{i}]', *T.spawn_fields_group(r)) for i, r in enumerate(records)]
    return [bool(r['nodes']) for r in T.match_nodes(ram, sources)]


def rule_problems(caps, record, rows, roster, first_ram=None):
    """[43]'s rule exactly in SWAP_CAPS (with no problem, its child pointer
    +0x2EC cleared); cells.json proofs = the recomputed rows; no orphan; the
    group records with no live node exactly DEAD_GROUPS (g[2] among them,
    and the same in the first load's a19_02 when `first_ram` is given) and
    hull 49 not moved."""
    out = []
    for cap in caps:
        nodes = X19.swap_nodes(cap.ram)
        if bool(nodes) != (cap.name in SWAP_CAPS):
            out.append(f'{cap.name}: [43] after its swap {bool(nodes)}, pinned {cap.name in SWAP_CAPS}')
        if any(C.u32(cap.ram, a + 0x2EC) for _s, a in nodes):
            out.append(f'{cap.name}: [43] after its swap still holds its child (+0x2EC)')
        out += X19.explicit_model_problems(cap.ram, cap.name)
    if first_ram is not None:
        try:
            dead = tuple(i for i, v in enumerate(group_live(first_ram, roster)) if not v)
        except (ValueError, IndexError, struct.error) as error:
            dead = repr(error)
        if dead != DEAD_GROUPS:
            out.append(f'a19_02: group records without a node {dead} != {DEAD_GROUPS}')
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
        if any(p.startswith('orphan') for p in proofs.values()):
            out.append(f'{cap.name}: an orphan hull')
        try:
            live = group_live(cap.ram, roster)
        except (ValueError, IndexError, struct.error) as error:
            out.append(f'{cap.name}: group records: {error!r}')
            continue
        dead = tuple(i for i, v in enumerate(live) if not v)
        if dead != DEAD_GROUPS or PICKUP_G2 not in dead:
            out.append(f'{cap.name}: group records without a node {dead} != {DEAD_GROUPS}')
        if str(HULL_G2) in proofs:
            out.append(f'{cap.name}: hull {HULL_G2} (the pickup g[2]\'s) moved')
    return out


def creature_file_problems(caps, tree):
    """creature.json and the two images against every capture."""
    out = []
    folder = tree / 'sub0/creature'
    try:
        rep = json.loads((folder / 'creature.json').read_text())
        rows = rep['ids']
        model = rows['0x70']['model']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return [f'creature.json: {error!r}']
    if sorted(rows) != ['0x70', '0x71']:
        out.append(f'creature.json ids {sorted(rows)}')
    for key, row in rows.items():
        ident = int(key, 16)
        try:
            blob = (folder / f'id{ident:02x}.emsc').read_bytes()
        except OSError as error:
            out.append(f'id{ident:02x}.emsc: {error!r}')
            continue
        magic, version, base, entry, length = struct.unpack_from('<4s4I', blob, 0)
        data = blob[20:]
        if (magic, version, entry, length) != (b'EMSC', 1, base, len(data)) or base != int(row['address'], 16) or \
                len(data) != row['bytes'] or sha(data) != row['sha256']:
            out.append(f'id{ident:02x}.emsc: header / length / hash differ from creature.json')
        for cap in caps:
            if C.u32(cap.ram, A13.D_0028A490 + 4 * ident) != base:
                out.append(f'{cap.name}: D_0028A490[{ident:#x}] != {base:#x}')
            if cap.ram[base:base + len(data)] != data:
                out.append(f'{cap.name}: id {ident:#x} bytes differ from RAM')
        if ident == XT.MODEL_ID:
            try:
                rec = XT.W.model_record(data, 0, ident)
            except SystemExit as error:
                out.append(f'id70: {error}')
                continue
            if (rec[0], rec[2], rec[4]) != (model.get('blocks'), model.get('bones'), model.get('bytes')):
                out.append(f'id70: model record {rec[:5]} != creature.json')
    for cap in caps:
        nodes = [hex(a) for _s, a in XT.creature_nodes(cap.ram)]
        if nodes != CREATURE_NODES.get(cap.name) or rep.get('nodes', {}).get(cap.name) != nodes:
            out.append(f'{cap.name}: creature nodes {nodes} (pinned {CREATURE_NODES.get(cap.name)})')
        if hex(C.u32(cap.ram, 0x8106C0)) != (nodes[0] if nodes else '0x0'):
            out.append(f'{cap.name}: D_008106C0 {C.u32(cap.ram, 0x8106C0):#x} is not the creature node')
        out += XT.creature_problems(cap, model)
    return out


def census_problems(census):
    out = []
    try:
        funcs = {int(f['addr'], 16): f for f in census['functions']}
        names = list(X19.CAPTURE_NAMES)
        s = census['summary']
        if s.get('passes') != ['A19B'] or [b['beat'] for b in census['per_beat']] != names:
            out.append('census: not the pass A19B over the three beats')
        runs = s.get('replay_runs', {})
        if s.get('beats_missing') != [] or s.get('beats_incomplete') != [] or sorted(runs) != sorted(names) or \
                not all(r.get('completed') and r.get('error') is None for r in runs.values()):
            out.append('census: a beat missing, incomplete or not replayed to completion')
        for a in RAN_ONLY_IN_ARRIVAL:
            if funcs.get(a, {}).get('beats') != ['a19b_00_arrival']:
                out.append(f'census: {a:#x} ran in {funcs.get(a, {}).get("beats")}, not a19b_00 alone')
        sub1 = sorted(hex(o) for o in set(funcs) & (set(T13.SUB1['area19']) | set(T13.CALLBACKS['area19'])))
        if sub1:
            out.append(f'census: sub-1 owners / their callback ran: {sub1}')
        owners = {o for owner in T13.SITES['area19'].values() for o in owner}
        modelled = {E1.PICKUP, E1.DRUM, E1.CREATURES['area19'], E1.SCALED_CALLER, E1.SCALED_OWNER,
                    E1.FLAG_OWNERS['area19']}
        unmodelled = sorted(hex(o) for o in (set(funcs) & owners) - modelled - set(T13.SUB1['area19']))
        if unmodelled:
            out.append(f'census: unmodelled call-site owners ran: {unmodelled}')
        if census['unattributed_hits']:
            out.append('census: unattributed hits')
        other = census['overlay_hits_other_overlay']
        if len(other) != 4 or any((h.get('overlay_id'), h.get('beat'), h.get('frame')) != (10, 'a19b_00_arrival', 1)
                                  for h in other):
            out.append('census: other-overlay hits beyond the four AREA13 hits at a19b_00 frame 1')
    except (KeyError, TypeError, AttributeError, ValueError) as error:
        out.append(f'census: {error!r}')
    return out


def lane_checks(every, caps, reload_tree, base, first_ram, census, rows, tree=None):
    """Every lane check; `tree` the view (default T13.A)."""
    tree = tree or T13.A
    out = capture_problems(every, tree)
    out += cursor_problems(caps, first_ram)
    out += split_problems(reload_tree, base)
    try:
        record = json.loads((tree / 'sub0/cells.json').read_text())
        proof = json.loads((tree / 'loaded_sub_proof.json').read_text())
        roster = (tree / 'sub0/roster.emro').read_bytes()
    except (OSError, ValueError) as error:
        return out + [f'lane inputs: {error!r}']
    out += sub_proof_problems(caps, proof)
    out += rule_problems(caps, record, rows, roster, first_ram)
    out += creature_file_problems(caps, tree)
    out += census_problems(census)
    return out


# ---------------------------------------------------------------------------
# Controls


def lane_controls(K, caps, every, first_ram, census, rows):
    """A changed input per lane check. Returns the count."""
    n = 0
    by = {c.name: c for c in every}
    arrival, swap = by['a19b_00_arrival'], by['a19b_02_ladder1023']

    def expect(problems, what, accept=False):
        nonlocal n
        n += 1
        check(bool(problems) != accept, f'control: {what} {"rejected" if accept else "missed"}')
    edited = lambda cap, edits: T13.ram_copy(cap, cap.name, edits)
    tmp = BUILD / f'ctl-{os.getpid()}'
    tview = BUILD / f'ctlview-{os.getpid()}'

    def tampered(change):
        """A real copy of the reload tree with `change(tree)` applied, and its view."""
        for d in (tmp, tview):
            if d.exists():
                shutil.rmtree(d)
        shutil.copytree(RELOAD, tmp)
        change(tmp)
        build_view(tmp, BASE, tview)
        return tmp

    def lane(change, census_=None):
        r = tampered(change)
        return lane_checks(every, caps, r, BASE, first_ram, census if census_ is None else census_, rows, tview)

    def edit_json(rel, fn):
        def change(tree):
            p = tree / rel
            d = json.loads(p.read_text())
            fn(d)
            p.write_text(json.dumps(d))
            rec = json.loads((tree / 'reload.json').read_text())
            table = rec['differing'] if rel in rec['differing'] else rec['added']
            table[rel]['sha256'] = sha(p.read_bytes())
            (tree / 'reload.json').write_text(json.dumps(rec))
        return change
    record = json.loads((RELOAD / 'sub0/cells.json').read_text())
    roster = (BASE / 'sub0/roster.emro').read_bytes()
    try:
        # captures
        expect(capture_problems(every[:-1], T13.A), 'the last capture missing')
        for at in (0x810700, 0x810701, 0x810702):
            expect(capture_problems([edited(c, [(at, bytes([c.ram[at] ^ 1]))]) if c is swap else c for c in every],
                                    T13.A), f'a19b_02 with byte {at:#x} changed')
        expect(capture_problems(every, T13.A, list(X19.EXCLUDED) + ['a13c_06_south']), 'an extra excluded folder')
        saved = LV.PREVIOUS['area19']
        LV.PREVIOUS['area19'] = A13.ROUTE_A13 / 'a13_04_hatch'
        try:
            expect(capture_problems(every, T13.A), "PREVIOUS['area19'] not re-pointed")
        finally:
            LV.PREVIOUS['area19'] = saved
        for rel, path in (('sub0/level/level.json', ('captures',)), ('sub0/cells.json', ('cells', 'captures')),
                          ('tables.json', ('captures',)), ('sub0/sfx/banks.json', ('captures',)),
                          ('sub0/creature/creature.json', ('captures',))):
            def cut(d, path=path):
                for k in path[:-1]:
                    d = d[k]
                d[path[-1]] = d[path[-1]][:-1]
            expect(lane(edit_json(rel, cut)), f'{rel} made over two captures')
        expect(lane(edit_json('sub0/level/level.json', lambda d: d['stale_d_0028a5a4'].__setitem__(
            'previous_capture', 'a13_04_hatch'))), 'level.json naming another previous capture')
        # cursor
        expect(cursor_problems(caps, first_ram), 'the cursors as captured', accept=True)
        for at in (C.D_0028A73C, C.D_0028A740, D_0028A59C, D_0028A5A0):
            expect(cursor_problems([edited(arrival, [(at, struct.pack('<I', C.u32(arrival.ram, at) + 0x10))])],
                                   first_ram), f'a19b_00 word {at:#x} + 0x10')
        last = A13.relocation_lists(arrival)['nested'][-1][0]
        at = A13.D_0028A490 + 4 * last
        expect(cursor_problems([edited(swap, [(at, struct.pack('<I', C.u32(swap.ram, at) + 0x40))])], first_ram),
               'the last nested relocation word')
        fr = bytearray(first_ram)
        struct.pack_into('<I', fr, C.D_0028A73C, X19.CURSOR)
        expect(cursor_problems(caps, bytes(fr)), 'the first load read with the second cursor')
        # split
        expect(split_problems(RELOAD, BASE), 'the split as exported', accept=True)

        def flip_byte(rel, at=0x40):
            def change(tree):
                p = tree / rel
                b = bytearray(p.read_bytes())
                b[at] ^= 1
                p.write_bytes(bytes(b))
            return change
        expect(split_problems(tampered(flip_byte('sub0/level/static_bank.emsc')), BASE), 'a static-bank byte')

        def flip_and_record(tree):
            flip_byte('sub0/level/static_bank.emsc')(tree)
            rec = json.loads((tree / 'reload.json').read_text())
            rec['differing']['sub0/level/static_bank.emsc']['sha256'] = sha(
                (tree / 'sub0/level/static_bank.emsc').read_bytes())
            (tree / 'reload.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(flip_and_record), BASE), 'a static-bank byte with reload.json updated')
        expect(split_problems(tampered(lambda t: (t / 'sub0/world_models.emwm').unlink()), BASE),
               'a differing file missing')
        expect(split_problems(tampered(lambda t: (t / 'extra.bin').write_bytes(b'x')), BASE), 'an extra file')
        expect(split_problems(tampered(lambda t: shutil.copyfile(BASE / 'sub0/area19.emcl', t / 'sub0/area19.emcl')),
                              BASE), 'an identical file copied into reload/')

        def as_added(tree):
            rec = json.loads((tree / 'reload.json').read_text())
            rec['added']['sub0/scene.txt'] = dict(sha256=sha((BASE / 'sub0/scene.txt').read_bytes()))
            rec['identical'].pop('sub0/scene.txt')
            shutil.copyfile(BASE / 'sub0/scene.txt', tree / 'sub0/scene.txt')
            (tree / 'reload.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(as_added), BASE), 'scene.txt listed as added')

        def bad_ident(tree):
            rec = json.loads((tree / 'reload.json').read_text())
            rec['identical']['sub0/roster.emro'] = '0' * 64
            (tree / 'reload.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(bad_ident), BASE), 'an identical file hash')

        def words_shifted(tree):
            rec = json.loads((tree / 'reload.json').read_text())
            rec['differing']['sub0/world_models.emwm']['words'] = [['0xc', '0x133a640', '0x133c1c0']]
            (tree / 'reload.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(words_shifted), BASE), 'reload.json word rows at offset + 4')
        expect(split_problems(tampered(lambda t: (t / 'sub0/creature/id71.emsc').unlink()), BASE),
               'an added file missing')

        def extra_added(tree):
            (tree / 'sub0/creature/extra.bin').write_bytes(b'x')
            rec = json.loads((tree / 'reload.json').read_text())
            rec['added']['sub0/creature/extra.bin'] = dict(sha256=sha(b'x'), bytes=1)
            (tree / 'reload.json').write_text(json.dumps(rec))
        expect(split_problems(tampered(extra_added), BASE), 'an extra file listed as added')
        # sub proof
        proof = json.loads((RELOAD / 'loaded_sub_proof.json').read_text())
        expect(sub_proof_problems(caps, proof), 'the sub proof as exported', accept=True)
        for cap in caps:
            bad = copy.deepcopy(proof)
            bad[cap.name]['sub1'] += 1
            expect(sub_proof_problems(caps, bad), f'loaded_sub_proof.json {cap.name} sub1 + 1')
        expect(sub_proof_problems([edited(arrival, [(0x810701, b'\x01')])], {arrival.name: proof[arrival.name]}),
               'a capture read as sub 1')
        # rules
        expect(rule_problems(caps, record, rows, roster), 'the rules as exported', accept=True)
        node = X19.swap_nodes(swap.ram)[0][1]
        model = X19.model_address(swap.ram, X19.SWAP_MODEL)
        expect(rule_problems([edited(swap, [(node + 4, b'\x01')])], record, rows, roster), '[43] back in state 1')
        expect(X19.explicit_model_problems(edited(swap, [(node + 0x44, struct.pack('<I', model + 0x40))]).ram, 's'),
               '[43] +0x44 another model')
        expect(X19.explicit_model_problems(edited(swap, [(node + 0x0C, b'\x02')]).ram, 's'),
               '[43] bone count reset to model 0x25\'s')
        b2 = C.u32(swap.ram, node + 0x110 + 8)
        expect(X19.explicit_model_problems(edited(swap, [T13.flip(swap.ram, b2 + 0x20)]).ram, 's'),
               '[43] bone 2 not model 0x25\'s third rest record')
        expect(X19.explicit_model_problems(edited(swap, [(node + 0x110 + 12, bytes(4))]).ram, 's'),
               '[43] a bone slot cleared')
        child = C.u32(by['a19b_01_ledge'].ram, node + 0x2EC)
        expect(rule_problems([edited(swap, [(node + 0x2EC, struct.pack('<I', child))])], record, rows, roster),
               '[43] after its swap still holding its child')
        expect(rule_problems(caps, record, rows, roster) +
               X19.explicit_model_problems(edited(swap, [(node + 0x0D, b'\x25')]).ram, 's'),
               '[43] spawned with the bit set (+0x0D 0x25: the generic rule)', accept=True)
        expect(X19.swap_nodes(edited(swap, [(0x810700, b'\x0d')]).ram), '[43]\'s rule outside AREA19 (area byte 0x0D)',
               accept=True)
        expect(rule_problems(caps, record, None, roster), 'no recomputed directory rows')
        rec2 = copy.deepcopy(record)
        rec2['cells']['captures'][1]['proofs']['49'] = 'orphan: equal to a derivation of it in another capture'
        expect(rule_problems(caps, rec2, rows, roster), 'a19b_01 hull 49 recorded as an orphan')
        rows2 = copy.deepcopy(rows)
        rows2[1]['proofs'][49] = 'orphan: equal to a derivation of it in another capture'
        expect(rule_problems(caps, rec2, rows2, roster), 'hull 49 an orphan in both record and rows')
        derived = '001A2370(node 0x7a5c20, behaviour 0x219550)'
        rec5, rows5 = copy.deepcopy(record), copy.deepcopy(rows)
        rec5['cells']['captures'][2]['proofs']['49'] = derived
        rows5[2]['proofs'][49] = derived
        expect(rule_problems(caps, rec5, rows5, roster), 'hull 49 derived in both record and rows')
        orphan = 'orphan: equal to a derivation of it in another capture'
        rec6, rows6 = copy.deepcopy(record), copy.deepcopy(rows)
        rec6['cells']['captures'][1]['proofs']['47'] = orphan
        rows6[1]['proofs'][47] = orphan
        expect(rule_problems(caps, rec6, rows6, roster), 'hull 47 an orphan in both record and rows')
        rec3 = copy.deepcopy(record)
        k = next(u for u in rec3['cells']['captures'][0]['proofs'])
        rec3['cells']['captures'][0]['proofs'][k] = 'underived'
        expect(rule_problems(caps, rec3, rows, roster), 'a19b_00 a hull recorded as underived')
        # g[2] alive (a pickup record copied over a free slot) / g[0] freed
        _p, groups = T13.roster_records(roster)
        sources = [(f'g[{i}]', *T.spawn_fields_group(r)) for i, r in enumerate(groups[0][1])]
        g0 = T.match_nodes(arrival.ram, [sources[0]])[0]['nodes'][0]['slot']
        n0 = T.POOL_BASE + g0 * T.POOL_STRIDE
        expect(rule_problems([edited(arrival, [(n0, b'\0')])], record, rows, roster), 'g[0] freed in a19b_00')
        first = C.Capture(X19.A19.ARRIVAL)                 # a13_05: g[2] live (first load)
        g2 = T.match_nodes(first.ram, [sources[PICKUP_G2]])[0]['nodes'][0]['slot']
        n2 = T.POOL_BASE + g2 * T.POOL_STRIDE
        free = next(s for s in range(T.POOL_SLOTS) if not arrival.ram[T.POOL_BASE + s * T.POOL_STRIDE])
        nf = T.POOL_BASE + free * T.POOL_STRIDE
        expect(rule_problems([edited(arrival, [(nf, first.ram[n2:n2 + T.POOL_STRIDE])])], record, rows, roster),
               'g[2] live in a19b_00')
        # creature
        expect(creature_file_problems(caps, T13.A), 'the creature files as exported', accept=True)
        cn = int(CREATURE_NODES['a19b_02_ladder1023'][0], 16)
        for off, what in ((0x44, '+0x44'), (0x40, '+0x40')):
            expect(creature_file_problems([edited(swap, [(cn + off, struct.pack('<I', C.u32(swap.ram, cn + off) + 0x10))])],
                                          T13.A), f'the creature {what} moved')
        expect(creature_file_problems([edited(swap, [(cn + 0x0C, b'\x1d')])], T13.A), 'the creature bone count 29')
        expect(creature_file_problems([edited(swap, [(cn + 0x0D, b'\x01')])], T13.A),
               'the creature +0x0D without bit 0x80 (another model id)')
        expect(creature_file_problems([edited(swap, [(0x8106C0, bytes(4))])], T13.A), 'D_008106C0 cleared')
        expect(creature_file_problems([edited(swap, [(cn + 0x0D, b'\x01'), (0x8106C0, bytes(4))])], T13.A),
               'the creature unbound and D_008106C0 cleared together')
        expect(lane(edit_json('sub0/creature/creature.json', lambda d: d['nodes'].__setitem__(
            'a19b_02_ladder1023', []))), 'creature.json without the a19b_02 node')
        expect(lane(edit_json('sub0/creature/creature.json', lambda d: d['ids']['0x71'].__setitem__(
            'sha256', '0' * 64))), 'creature.json with another id 0x71 hash')
        base70 = C.u32(swap.ram, A13.D_0028A490 + 4 * 0x70)
        expect(creature_file_problems([edited(swap, [T13.flip(swap.ram, base70 + 0x100)])], T13.A),
               'an id 0x70 byte changed in RAM')
        expect(lane(edit_json('sub0/creature/creature.json', lambda d: d['ids']['0x70']['model'].__setitem__(
            'bones', 29))), 'creature.json with 29 bones')

        def flip_id70(tree):
            p = tree / 'sub0/creature/id70.emsc'
            b = bytearray(p.read_bytes())
            b[0x200] ^= 1
            p.write_bytes(bytes(b))
            rec = json.loads((tree / 'reload.json').read_text())
            rec['added']['sub0/creature/id70.emsc']['sha256'] = sha(bytes(b))
            (tree / 'reload.json').write_text(json.dumps(rec))
        expect(lane(flip_id70), 'id70.emsc byte changed (reload.json updated)')
        # census
        expect(census_problems(census), 'the census as recorded', accept=True)
        edits = [(f'the sub-1 owner {o:#x} ran', lambda d, o=o: d['functions'].append(dict(addr=hex(o))))
                 for o in T13.SUB1['area19']]
        edits += [
            ('the callback 0x827B20 ran', lambda d: d['functions'].append(dict(addr=hex(0x827B20)))),
            ('001FFCD0 also in a19b_02', lambda d: next(f for f in d['functions'] if f['addr'] == '0x1ffcd0')[
                'beats'].append('a19b_02_ladder1023')),
            ('0019C6F0 absent', lambda d: d['functions'].remove(next(f for f in d['functions']
                                                                     if f['addr'] == '0x19c6f0'))),
            ('0x219F50 in a19b_01', lambda d: next(f for f in d['functions'] if f['addr'] == '0x219f50')[
                'beats'].append('a19b_01_ledge')),
            ('an unattributed hit', lambda d: d['unattributed_hits'].append('0x00500000')),
            ('a fifth other-overlay hit', lambda d: d['overlay_hits_other_overlay'].append(
                dict(d['overlay_hits_other_overlay'][0]))),
            ('an other-overlay hit at frame 2', lambda d: d['overlay_hits_other_overlay'][0].__setitem__('frame', 2)),
            ('a second pass name', lambda d: d['summary']['passes'].append('A13D')),
            ('a beat dropped', lambda d: d['per_beat'].pop()),
            ('a beat missing', lambda d: d['summary']['beats_missing'].append('a19b_01_ledge')),
            ('a replay error', lambda d: d['summary']['replay_runs']['a19b_00_arrival'].__setitem__('error', 'x')),
            ('a replay not completed', lambda d: d['summary']['replay_runs']['a19b_02_ladder1023'].__setitem__(
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
        expect(lane(edit_json('loaded_sub_proof.json', lambda d: d['a19b_01_ledge'].__setitem__('sub0', 1))),
               'lane_checks with loaded_sub_proof.json a19b_01 sub0 = 1')
        expect(lane(edit_json('sub0/cells.json', lambda d: d['cells']['captures'][2]['proofs'].__setitem__(
            '49', 'orphan: equal to a derivation of it in another capture'))), 'lane_checks with an orphan 49')
    finally:
        for d in (tmp, tview):
            if d.exists():
                shutil.rmtree(d)
    return n


# ---------------------------------------------------------------------------


def main():
    need = [RELOAD / 'reload.json', BASE / 'sub0/level/level.json', C.ELF_PATH, C.ISO_PATH,
            X19.TARGET.overlay_path, FIRST_RAM, CENSUS] + \
        [X19.ROUTE_A19B / n / 'eeMemory.bin' for n in X19.CAPTURE_NAMES]
    missing = [str(p) for p in need if not p.exists()]
    if missing:
        print('area19x assets reference: SKIPPED, missing local inputs:', missing)
        return 0
    t0 = time.time()
    try:
        build_view(RELOAD, BASE, VIEW)
    except (OSError, KeyError, json.JSONDecodeError) as error:
        print(f'area19x assets reference: FAIL (reload.json: {error!r})')
        return 1
    try:
        configure(VIEW)
        el = T13.L.load_export_level()
        lib = T01.build_loaders()
        bg = T13.background_lib()
        K, caps = T13.target_setup('area19', el)
        print(f'area19x assets reference ({MODE}) AREA19 sub 0, second load: {len(caps)} of {len(K.every)} captures')
        loaded = T13.check_loaders(lib, bg, K.t, VIEW)
        for name in ('id70.emsc', 'id71.emsc'):
            f = getattr(lib, 'em_script_image_load')
            f.restype = T13.CT.c_int
            loaded[name] = f(T13.CT.create_string_buffer(16 << 20), str(VIEW / 'sub0/creature' / name).encode()) == 1
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
            check(False, f'area19: {p}')
        lv = V.get('level', {})
        print(f"  AREA13 lane's AREA19 checks: {lv.get('objects')} bank objects -> {lv.get('zones')} zone EMDL(s), "
              f"{lv.get('kicks')} level kicks; collision, cells, tables, sfx, ctx: {len(problems)} problems")
        rows = _ROWS.get(tuple(c.name for c in caps))
        first_ram = FIRST_RAM.read_bytes()
        census = json.loads(CENSUS.read_text())
        lp = lane_checks(K.every, caps, RELOAD, BASE, first_ram, census, rows)
        for p in lp:
            check(False, f'lane: {p}')
        print(f'  lane checks (captures, cursor, split, sub proof, rules, creature, census): {len(lp)} problems')
        if FAILS:
            print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
        else:
            T13.use(K)
            print(f'  canary: {T13.canary(K, lib, bg, caps[0])} sections each reported their planted difference')
            if not FAILS:
                print(f'  controls: {lane_controls(K, caps, K.every, first_ram, census, rows)} changed inputs, each '
                      'caught (or accepted where marked)')
    finally:
        if VIEW.exists():
            shutil.rmtree(VIEW)
    print(f'area19x assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
