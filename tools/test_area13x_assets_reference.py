#!/usr/bin/env python3
"""Check the AREA13X export (assets/area13/reload/ over assets/area13/,
docs/AREA13X_ASSETS.md) against the later AREA13 captures, and load each
file through the port's own loader.

The target is AREA13 sub 0 in its second load (export_area13x_common): the
a13b captures a13b_00 .. a13b_04, a13b_s0 and the eleventh level's a13c_00
.. a13c_06 (../Extermination/build/s87/route_a13b/, route_a13c/). The
checked tree is a view: every file reload.json lists as identical is the
a13 group's file (assets/area13/), every differing one the reload/ file.
Missing exports, ELF, ISO, overlay, captures or census print SKIPPED.

The checks are the AREA13 lane's (tools/test_area13_assets_reference.py,
imported UNCHANGED, run_checks + canary, re-pointed at the view, the 13
captures and this lane's pins; with export_area13x_common's three added
rules installed): the load map, level, background, collision, cell
directory (the ORIGINAL 001A2370 / 0x219F50 / 0019C6F0), tables, sound, ctx
block (the ORIGINAL 001D8FD0 / 001D1C50), the port loaders. This lane adds:
  captures  exactly the 13 pinned captures, their area / sub / entry bytes;
            a13b_05 the only excluded folder; level.json, cells.json,
            tables.json and banks.json made over all 13
  roster    placement nodes live / at their record's position pinned for
            all 13 captures, in every mode
  cursor    D_0028A73C = 0x133C1C0 and D_0028A740 = it + the resident length
            in every capture; every relocation word 0x1B80 above the a13
            group's (a13_04's RAM); the bank bases D_0028A59C / D_0028A5A0
            pinned in a13_04 and every capture
  split     reload.json accounts for every file of assets/area13/ (area19/
            aside) exactly once; identical files are absent from reload/ and
            hash as recorded; the differing binaries differ from the a13
            group's only in the pinned base words, each by +0x1B80, and
            reload.json's word rows equal WORD_ROWS (pinned here); the
            differing report files are the pinned five; no other file
  rules     cells.json's flag calls per capture = the pinned [47] sequence;
            its proofs = the rows the directory derivation recomputed in this
            run (knocked drums 7, 9, 13 from a13c_02's nodes in a13c_03 ..
            a13c_06); [45]'s model-0x12 rule holds exactly in a13c_03 ..
            a13c_06
  census    a13b: 001FFCD0 ran in a13b_00 and a13b_05 only, 0019C6F0 in
            a13b_00 only; a13c: 001FFCD0 did not run (no load), 0019C6F0 only
            in a13c_03; no other overlay and no unattributed hit in a13c
  controls  a changed input per lane check and per added rule, plus
            review_controls (pinned cases for the round-11 review survivors)
Default: a13b_00, a13c_02, a13c_03 (the reload, the countdown, the knocked
drums and [45]'s model); EM_TEST_FULL=1: all 13.
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
import export_area13x_common as X13  # noqa: E402  (first: re-points the AREA13 lane's AREA13 target)
import export_area13x_split as SPLIT  # noqa: E402
import test_area13_assets_reference as T13  # noqa: E402

X13.install()
A13, C, LV, TB = X13.A13, X13.C, X13.LV, X13.TB
E2, T01, T, L = T13.E2, T13.T01, T13.T, X13.L
FULL, MODE = T13.FULL, T13.MODE
FAILS, check = T13.FAILS, T13.check

RELOAD = Path(os.environ.get('EM_AREA13X_ASSETS') or X13.OUT).resolve()
BASE = X13.A13_TREE.resolve()
BUILD = C.ROOT / 'build/area13x/assets/test'
VIEW = BUILD / f'view-{os.getpid()}'
CENSUS_B = C.DECOMP / 'build/s87/census/a13b_delta.json'
CENSUS_C = C.DECOMP / 'build/s87/census/a13c_delta.json'
A13_04 = A13.ROUTE_A13 / 'a13_04_hatch/eeMemory.bin'      # the a13 group's relocation words
QUICK = ('a13b_00_ladder_up', 'a13c_02_battery', 'a13c_03_blast')
GRID = 0x17D39C0                                           # D_0028A598 of the second load
RESIDENT = 0x6E2800                                        # AREA13's resident length (AREA13_ASSETS.md)
SHIFT = X13.CURSOR - X13.FIRST_CURSOR                      # 0x1B80
AREA_BYTES = {'a13b_00_ladder_up': (0x0D, 0, 6), 'a13b_01_door17': (0x0D, 0, 9), 'a13b_02_button15': (0x0D, 0, 3),
              'a13b_03_door8': (0x0D, 0, 1), 'a13b_04_lift_call': (0x0D, 0, 1),
              'a13b_s0_roof_ladder': (0x0D, 0, 9), 'a13c_00_recharger': (0x0D, 0, 4),
              'a13c_01_to_machine': (0x0D, 0, 9), 'a13c_02_battery': (0x0D, 0, 9), 'a13c_03_blast': (0x0D, 0, 9),
              'a13c_04_cure': (0x0D, 0, 9), 'a13c_05_boom': (0x0D, 0, 9), 'a13c_06_south': (0x0D, 0, 9)}
LATE = ('a13c_03_blast', 'a13c_04_cure', 'a13c_05_boom', 'a13c_06_south')
# (placement nodes live, of them at their record's position and rotation)
ROSTER_LIVE = {n: (62, 61) for n in X13.CAPTURE_NAMES}
ROSTER_LIVE.update({'a13c_03_blast': (52, 50), 'a13c_04_cure': (52, 50), 'a13c_05_boom': (51, 49),
                    'a13c_06_south': (51, 49)})
MODEL_OWNERS = {n: (54 if n in LATE else 64) for n in X13.CAPTURE_NAMES}
# [47]'s last 0019C6F0 pair per capture (export_area13x_common.flag_calls)
FLAG_PAIRS = {n: ([[0x1F, 0], [0x20, 1]] if n in LATE else [[0x1F, 1], [0x20, 0]]) for n in X13.CAPTURE_NAMES}
DRUM_ORPHANS = {n: (7, 9, 13) for n in LATE}
DRUM_DONOR = 'a13c_02_battery'
# reload.json: the binaries whose words differ ({offset: shift}) and the reports
BINARY_WORDS = {'sub0/level/static_bank.emsc': {0x8: SHIFT, 0xC: SHIFT}, 'sub0/world_models.emwm': {0x8: SHIFT}}
# reload.json's word rows, pinned independently of export_area13x_split:
# [offset, a13 group word, second-load word]. The words are the model bank
# base D_0028A59C (world_models +8) and the static bank base D_0028A5A0
# (static_bank +8 base, +0xC entry); cursor_problems ties these values to
# the RAM of a13_04 and of every capture.
D_0028A59C, D_0028A5A0 = 0x28A59C, 0x28A5A0
MODEL_BANK = (0x133A640, 0x133C1C0)
STATIC_BANK = (0x1472640, 0x14741C0)
WORD_ROWS = {'sub0/level/static_bank.emsc': [['0x8', hex(STATIC_BANK[0]), hex(STATIC_BANK[1])],
                                              ['0xc', hex(STATIC_BANK[0]), hex(STATIC_BANK[1])]],
             'sub0/world_models.emwm': [['0x8', hex(MODEL_BANK[0]), hex(MODEL_BANK[1])]]}
REPORTS = ('sub0/cells.json', 'sub0/level/level.json', 'sub0/sfx/banks.json', 'sub0/world_models.json',
           'tables.json')


def configure(view=None):
    """Point the AREA13 checker at this lane (idempotent)."""
    X13.install()
    T13.A = view or VIEW
    T13.BUILD = BUILD
    T13.CANARY = BUILD / f'canary-{os.getpid()}'
    T01.BUILD = BUILD
    T13.QUICK['area13'] = QUICK
    T13.EXCLUDED['area13'] = list(X13.EXCLUDED)
    T13.GRID['area13'] = GRID
    T13.ROSTER_LIVE.update(ROSTER_LIVE)
    T13.MODEL_OWNERS.update(MODEL_OWNERS)
    T13.FIELD_RULES[X13.FLAG_OWNER] = rule_8293a0
    T13.FIELD_RULES[HATCH] = rule_826850


def rule_8293a0(ram, node, index, places):
    """[47]'s +0x0D (func_overlay_AREA13_00829360.c), the AREA13 lane's rule
    with the bit-0x40-set states added: state 1 with the bit clear, REC[0x2C];
    with it set and +0x28 != 0, REC[0x2C] (state 0 ran with the bit clear)
    or REC[4] (with it set); with +0x28 = 0, REC[4] (the restore ran).
    None (the record's own field) outside state 1."""
    if ram[node + 4] != 1 or index + 1 >= len(places):
        return None
    first, second = places[index][4], places[index + 1][4]
    if not ram[0x8107F4] & 0x40:
        return second
    if C.s16(ram, node + 0x28) != 0 and ram[node + 0x0D] in (first, second):
        return ram[node + 0x0D]
    return first


HATCH = 0x826850                                           # [62] / [63], func_overlay_AREA13_00826810.c


def rule_826850(ram, node, index, places):
    """The hatch's +0x0D (func_overlay_AREA13_00826810.c, byte-identical):
    its state 0 sets +0x0D = 0xD and goes to state 2 when its side's bit of
    D_00810839 is set (z > 1000: bit 0, else bit 1), i.e. a hatch opened
    before this load; one that opens in play (state 1) sets the model 0xD
    without changing +0x0D. So in state 2 with the side bit set +0x0D is 0xD
    or the record's; else the record's (None)."""
    side = 1 if not struct.unpack_from('<f', ram, node + 0xB8)[0] <= 1000.0 else 2
    if ram[node + 4] == 2 and ram[0x810839] & side and ram[node + 0x0D] in (0xD, places[index][4]):
        return ram[node + 0x0D]
    return None


configure()


def sha(data):
    return hashlib.sha256(data).hexdigest()


# ---------------------------------------------------------------------------
# The view


def build_view(reload_tree, base, view):
    """A tree of symlinks: reload.json's identical files from `base`, its
    differing ones from `reload_tree`. Returns the record."""
    record = json.loads((reload_tree / 'reload.json').read_text())
    if view.exists():
        shutil.rmtree(view)
    for rel in record['identical']:
        (view / rel).parent.mkdir(parents=True, exist_ok=True)
        (view / rel).symlink_to(base / rel)
    for rel in record['differing']:
        (view / rel).parent.mkdir(parents=True, exist_ok=True)
        (view / rel).symlink_to(reload_tree / rel)
    return record


# ---------------------------------------------------------------------------
# This lane's checks


def capture_problems(every, tree, excluded=None):
    """`excluded`: the excluded capture folders (default: what
    A13.excluded_captures finds on disk)."""
    out = []
    names = tuple(c.name for c in every)
    if names != X13.CAPTURE_NAMES:
        out.append(f'AREA13 captures {names} != {X13.CAPTURE_NAMES}')
    for cap in every:
        got = tuple(cap.ram[0x810700:0x810703])
        if got != AREA_BYTES.get(cap.name):
            out.append(f'{cap.name}: area bytes {got} != {AREA_BYTES.get(cap.name)}')
    if excluded is None:
        excluded = [n for n, _a in A13.excluded_captures(A13.AREA13)]
    excluded = sorted(excluded)
    if excluded != list(X13.EXCLUDED):
        out.append(f'excluded captures {excluded} != {list(X13.EXCLUDED)}')
    try:
        reports = {
            'level.json': json.loads((tree / 'sub0/level/level.json').read_text())['captures'],
            'cells.json': [r['capture'] for r in json.loads((tree / 'sub0/cells.json').read_text())['cells']['captures']],
            'tables.json': json.loads((tree / 'tables.json').read_text())['captures'],
            'banks.json': json.loads((tree / 'sub0/sfx/banks.json').read_text())['captures']}
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return out + [f'reports: {error!r}']
    for name, got in reports.items():
        if tuple(got) != X13.CAPTURE_NAMES:
            out.append(f'{name} was made over {got}, not the 13 captures')
    # D_0028A5A4's previous capture: the last one before the second load
    if LV.PREVIOUS.get('area13') != X13.PREVIOUS:
        out.append(f"export_area13_level.PREVIOUS['area13'] is {LV.PREVIOUS.get('area13')}, not {X13.PREVIOUS}")
    try:
        prev = json.loads((tree / 'sub0/level/level.json').read_text())['stale_d_0028a5a4']['previous_capture']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return out + [f'level.json stale_d_0028a5a4: {error!r}']
    if prev != X13.PREVIOUS.name:
        out.append(f'level.json: D_0028A5A4 previous capture {prev} != {X13.PREVIOUS.name}')
    return out


def cursor_problems(caps, first_ram):
    """D_0028A73C / D_0028A740 and every relocation word against the a13
    group's (`first_ram`: a13_04's RAM words)."""
    out = []
    lists = A13.relocation_lists(caps[0])['top']
    for cap in caps:
        ram = cap.ram
        if C.u32(ram, C.D_0028A73C) != X13.CURSOR:
            out.append(f'{cap.name}: D_0028A73C {C.u32(ram, C.D_0028A73C):#x} != {X13.CURSOR:#x}')
        if C.u32(ram, C.D_0028A740) != C.u32(ram, C.D_0028A73C) + RESIDENT:
            out.append(f'{cap.name}: D_0028A740 is not D_0028A73C + {RESIDENT:#x}')
        if (C.u32(ram, D_0028A59C), C.u32(ram, D_0028A5A0)) != (MODEL_BANK[1], STATIC_BANK[1]):
            out.append(f'{cap.name}: D_0028A59C / D_0028A5A0 are not the pinned second-load bank bases')
        for ident, _off in lists:
            at = A13.D_0028A490 + 4 * ident
            if C.u32(ram, at) - C.u32(first_ram, at) != SHIFT:
                out.append(f'{cap.name}: D_0028A490[{ident:#x}] is not the a13 group\'s + {SHIFT:#x}')
    if C.u32(first_ram, C.D_0028A73C) != X13.FIRST_CURSOR:
        out.append(f'a13 group: D_0028A73C {C.u32(first_ram, C.D_0028A73C):#x} != {X13.FIRST_CURSOR:#x}')
    if (C.u32(first_ram, D_0028A59C), C.u32(first_ram, D_0028A5A0)) != (MODEL_BANK[0], STATIC_BANK[0]):
        out.append('a13 group: D_0028A59C / D_0028A5A0 are not the pinned bank bases')
    return out


def split_problems(reload_tree, base, record=None):
    """reload.json against both trees."""
    out = []
    try:
        record = record or json.loads((reload_tree / 'reload.json').read_text())
        ident, diff = record['identical'], record['differing']
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        return [f'reload.json: {error!r}']
    files = SPLIT.tree_files(base)
    if sorted(set(ident) | set(diff)) != sorted(files) or set(ident) & set(diff):
        out.append('reload.json does not list every file of the a13 tree exactly once')
    if tuple(record.get('captures', ())) != X13.CAPTURE_NAMES:
        out.append('reload.json captures')
    present = {p.relative_to(reload_tree).as_posix() for p in reload_tree.rglob('*') if p.is_file()}
    if present != set(diff) | {'reload.json'}:
        out.append(f'reload/ holds {sorted(present - set(diff) - {"reload.json"})}, lacks '
                   f'{sorted(set(diff) - present)}')
    for rel, digest in ident.items():
        if rel in files and sha(files[rel].read_bytes()) != digest:
            out.append(f'{rel}: identical file hash')
    if sorted(r for r in diff if r not in BINARY_WORDS) != sorted(REPORTS):
        out.append(f'differing report files {sorted(r for r in diff if r not in BINARY_WORDS)} != {sorted(REPORTS)}')
    for rel, row in diff.items():
        if rel not in files or rel not in present:
            continue
        new, old = (reload_tree / rel).read_bytes(), files[rel].read_bytes()
        if sha(new) != row.get('sha256') or sha(old) != row.get('a13_sha256'):
            out.append(f'{rel}: differing file hashes')
        if rel not in BINARY_WORDS:
            continue
        want = BINARY_WORDS[rel]
        got = {k: struct.unpack_from('<I', new, k)[0] - struct.unpack_from('<I', old, k)[0]
               for k in range(0, len(new) - 3, 4) if new[k:k + 4] != old[k:k + 4]} if len(new) == len(old) else None
        if got != want:
            out.append(f'{rel}: differing words {got} != {want}')
        elif row.get('words') != WORD_ROWS[rel]:
            out.append(f'{rel}: reload.json words {row.get("words")} != {WORD_ROWS[rel]}')
    return out


_ROWS = {}


def recording_verify_directory(original):
    """Wrap X13.verify_directory to keep the rows of each call."""
    def wrapped(elf, disc, caps):
        rows, problems, calls = original(elf, disc, caps)
        _ROWS[tuple(c.name for c in caps)] = copy.deepcopy(rows)
        return rows, problems, calls
    return wrapped


def rule_problems(caps, record, rows):
    """cells.json flag calls = FLAG_PAIRS; proofs = the recomputed rows (the
    knocked drums exactly DRUM_ORPHANS, from DRUM_DONOR's nodes); [45]'s
    explicit model exactly in LATE."""
    out = []
    rec = record['cells']
    for name in X13.CAPTURE_NAMES:
        if rec['flag_calls'].get(name) != FLAG_PAIRS[name]:
            out.append(f'{name}: cells.json flag calls {rec["flag_calls"].get(name)} != {FLAG_PAIRS[name]}')
    if rows is None:
        return out + ['cells.json proofs: no recomputed directory rows']
    stored = {r['capture']: r.get('proofs') for r in rec['captures']}
    for r in rows:
        want = {str(u): p for u, p in r['proofs'].items()}
        if stored.get(r['capture']) != want:
            out.append(f'{r["capture"]}: cells.json proofs differ from the recomputed directory')
    for name, proofs in stored.items():
        drums = tuple(sorted(int(u) for u, p in (proofs or {}).items() if p.startswith('knocked drum')))
        if drums != DRUM_ORPHANS.get(name, ()):
            out.append(f'{name}: knocked-drum proofs for hulls {drums} != {DRUM_ORPHANS.get(name, ())}')
        if any(p.startswith('knocked drum') and f'as in {DRUM_DONOR})' not in p for p in (proofs or {}).values()):
            out.append(f'{name}: a knocked-drum proof from another capture than {DRUM_DONOR}')
    for cap in caps:
        seq = [a for _s, a in X13.explicit_model_nodes(cap.ram) if C.u32(cap.ram, a + 0x10) == X13.SEQUENCE_OWNER]
        if bool(seq) != (cap.name in LATE):
            out.append(f'{cap.name}: [45] after its sequence {bool(seq)}, pinned {cap.name in LATE}')
    return out


def census_problems(cb, cc):
    """The a13b / a13c census deltas: where the loader and 0019C6F0 ran."""
    out = []
    try:
        fb = {x['addr']: x for x in cb['functions']}
        fc = {x['addr']: x for x in cc['functions']}
        beats = lambda f, a: sorted(f[a]['beats']) if a in f else []
        if beats(fb, '0x1ffcd0') != ['a13b_00_ladder_up', 'a13b_05_lift_ride']:
            out.append(f'a13b: 001FFCD0 ran in {beats(fb, "0x1ffcd0")}')
        if beats(fb, '0x19c6f0') != ['a13b_00_ladder_up']:
            out.append(f'a13b: 0019C6F0 ran in {beats(fb, "0x19c6f0")}')
        if beats(fc, '0x1ffcd0'):
            out.append(f'a13c: 001FFCD0 ran in {beats(fc, "0x1ffcd0")}')
        if beats(fc, '0x19c6f0') != ['a13c_03_blast'] or fc['0x19c6f0'].get('first_frame') != 1731:
            out.append(f'a13c: 0019C6F0 ran in {beats(fc, "0x19c6f0")}')
        if cc['overlay_hits_other_overlay'] or cc['unattributed_hits']:
            out.append('a13c: a hit in another overlay or an unattributed hit')
        s = cc['summary']
        if s.get('passes') != ['A13C'] or s.get('beats_missing'):
            out.append(f'a13c census: passes {s.get("passes")}, beats missing {s.get("beats_missing")}')
        if [b['beat'] for b in cc['per_beat']] != [n for n in X13.CAPTURE_NAMES if n.startswith('a13c')]:
            out.append('a13c census: beats')
    except (KeyError, TypeError, AttributeError) as error:
        out.append(f'census: {error!r}')
    return out


def roster_pin_problems(every, blob, pins=None):
    """ROSTER_LIVE over all 13 captures in every mode (the AREA13 lane's
    roster check covers only the selected ones): placement nodes live / at
    their record's position, by test_area01's roster_nodes over the view's
    roster.emro (which the lane's check equals to the rebuild from the disc)."""
    out = []
    pins = ROSTER_LIVE if pins is None else pins
    for cap in every:
        _ok, live, _pairs = T01.roster_nodes(T, blob, cap.ram)
        if live != pins.get(cap.name):
            out.append(f'{cap.name}: roster live / placed {live} != pinned {pins.get(cap.name)}')
    return out


def lane_checks(every, caps, reload_tree, base, first_ram, cb, cc, rows):
    out = capture_problems(every, reload_tree)
    out += roster_pin_problems(every, (T13.A / 'sub0/roster.emro').read_bytes())
    out += cursor_problems(caps, first_ram)
    out += split_problems(reload_tree, base)
    try:
        record = json.loads((reload_tree / 'sub0/cells.json').read_text())
        out += rule_problems(caps, record, rows)
    except (OSError, KeyError, json.JSONDecodeError) as error:
        out.append(f'cells.json: {error!r}')
    out += census_problems(cb, cc)
    return out


# ---------------------------------------------------------------------------
# Controls


def lane_controls(K, caps, every, first_ram, cb, cc, rows):
    """A changed input per lane check and per added rule. Returns the count."""
    n = 0
    by = {c.name: c for c in every}

    def expect(problems, what, accept=False):
        nonlocal n
        n += 1
        check(bool(problems) != accept, f'control: {what} {"rejected" if accept else "missed"}')
    edited = lambda cap, edits: T13.ram_copy(cap, cap.name + '*', edits)

    def same(cap, edits):
        c = T13.ram_copy(cap, cap.name, edits)
        return c
    flip = T13.flip
    tmp = BUILD / f'ctl-{os.getpid()}'

    def tampered(change):
        """A real copy of the reload tree with `change(tree)` applied."""
        if tmp.exists():
            shutil.rmtree(tmp)
        shutil.copytree(RELOAD, tmp)
        change(tmp)
        return tmp
    try:
        # captures
        expect(capture_problems(every[:-1], RELOAD), 'the last capture missing')
        for name, at in (('a13c_04_cure', 0x810700), ('a13b_02_button15', 0x810702)):
            c = same(by[name], [flip(by[name].ram, at)])
            expect(capture_problems([c if x.name == name else x for x in every], RELOAD), f'{name} byte {at:#x}')
        for rel, k in (('sub0/level/level.json', 'captures'), ('tables.json', 'captures'),
                       ('sub0/sfx/banks.json', 'captures')):
            def drop(t, rel=rel, k=k):
                d = json.loads((t / rel).read_text())
                d[k] = d[k][:-1]
                (t / rel).write_text(json.dumps(d))
            expect(capture_problems(every, tampered(drop)), f'{rel} made over 12 captures')

        def drop_row(t):
            d = json.loads((t / 'sub0/cells.json').read_text())
            d['cells']['captures'] = d['cells']['captures'][1:]
            (t / 'sub0/cells.json').write_text(json.dumps(d))
        expect(capture_problems(every, tampered(drop_row)), 'cells.json without its first row')
        # cursor
        cap = caps[-1]
        expect(cursor_problems([edited(cap, [flip(cap.ram, C.D_0028A73C + 1, 0x10)])], first_ram),
               'D_0028A73C changed')
        expect(cursor_problems([edited(cap, [flip(cap.ram, C.D_0028A740 + 1, 0x10)])], first_ram),
               'D_0028A740 changed')
        ident = A13.relocation_lists(cap)['top'][-1][0]
        expect(cursor_problems([edited(cap, [flip(cap.ram, A13.D_0028A490 + 4 * ident + 1)])], first_ram),
               'the last relocation word changed')
        fr = bytearray(first_ram)
        fr[C.D_0028A73C + 1] ^= 1
        expect(cursor_problems([cap], bytes(fr)), 'the a13 group\'s cursor changed')
        # split
        expect(split_problems(RELOAD, BASE), 'reload.json as exported', accept=True)

        def ident_hash(t):
            d = json.loads((t / 'reload.json').read_text())
            k = sorted(d['identical'])[0]
            d['identical'][k] = '0' * 64
            (t / 'reload.json').write_text(json.dumps(d))
        expect(split_problems(tampered(ident_hash), BASE), 'an identical file\'s hash')

        def bank_byte(t):
            p = t / 'sub0/level/static_bank.emsc'
            b = bytearray(p.read_bytes())
            b[0x30] ^= 1
            p.write_bytes(bytes(b))
        expect(split_problems(tampered(bank_byte), BASE), 'a static-bank byte past the header')

        def bank_byte_recorded(t):
            p = t / 'sub0/level/static_bank.emsc'
            b = bytearray(p.read_bytes())
            b[0x30] ^= 1
            p.write_bytes(bytes(b))
            d = json.loads((t / 'reload.json').read_text())
            row = d['differing']['sub0/level/static_bank.emsc']
            row['sha256'] = sha(bytes(b))
            row['words'] = SPLIT.word_diffs(bytes(b), (BASE / 'sub0/level/static_bank.emsc').read_bytes())
            (t / 'reload.json').write_text(json.dumps(d))
        expect(split_problems(tampered(bank_byte_recorded), BASE), 'a static-bank byte, reload.json updated to match')

        def report_copy(t):
            shutil.copyfile(BASE / 'sub0/scene.txt', t / 'sub0/scene.txt')
            d = json.loads((t / 'reload.json').read_text())
            digest = d['identical'].pop('sub0/scene.txt')
            d['differing']['sub0/scene.txt'] = dict(sha256=digest, a13_sha256=digest)
            (t / 'reload.json').write_text(json.dumps(d))
        expect(split_problems(tampered(report_copy), BASE), 'scene.txt copied and listed as a differing report')

        def base_word(t):
            p = t / 'sub0/world_models.emwm'
            b = bytearray(p.read_bytes())
            struct.pack_into('<I', b, 8, struct.unpack_from('<I', b, 8)[0] + 0x10)
            p.write_bytes(bytes(b))
        expect(split_problems(tampered(base_word), BASE), 'the world-model base + 0x10')
        expect(split_problems(tampered(lambda t: (t / 'sub0/sfx/banks.json').unlink()), BASE),
               'a differing file missing')
        expect(split_problems(tampered(lambda t: shutil.copyfile(BASE / 'tables.json', t / 'scripts.emsc')), BASE),
               'an extra file in reload/')

        def ident_in(t):
            shutil.copyfile(BASE / 'sub0/roster.emro', t / 'sub0/roster.emro')
        expect(split_problems(tampered(ident_in), BASE), 'an identical file copied into reload/')

        def moved(t):
            d = json.loads((t / 'reload.json').read_text())
            d['differing']['sub0/roster.emro'] = d['identical'].pop('sub0/roster.emro')
            (t / 'reload.json').write_text(json.dumps(d))
        expect(split_problems(tampered(moved), BASE), 'an identical file listed as differing')

        def words(t):
            d = json.loads((t / 'reload.json').read_text())
            d['differing']['sub0/world_models.emwm']['words'] = []
            (t / 'reload.json').write_text(json.dumps(d))
        expect(split_problems(tampered(words), BASE), 'reload.json word list emptied')
        # the [47] rule (flag_calls), on a13c_02 (countdown) and a13c_03 (restored)
        c2, c3 = by['a13c_02_battery'], by['a13c_03_blast']
        node = T13.node_of(c2.ram, X13.FLAG_OWNER)
        rec = X13.PLACEMENTS + 0x28 * c2.ram[node + 0x9A]
        expect([] if X13.flag_calls(c2.ram) == ((0x1F, 1), (0x20, 0)) else ['x'], 'a13c_02 pair', accept=True)
        expect([] if X13.flag_calls(c3.ram) == ((0x1F, 0), (0x20, 1)) else ['x'], 'a13c_03 pair', accept=True)
        for what, edits in (('+0x28 = 0 with +0x0D = 0x13', [(node + 0x28, b'\0\0')]),
                            ('+0x0D = 0x15', [(node + 0x0D, b'\x15')]),
                            ('state 0', [(node + 4, b'\0')]),
                            ('REC[0x2C] = 0x14', [(rec + 0x2C, b'\x14')]),
                            ('REC[4] = 0x13', [(rec + 4, b'\x13')])):
            try:
                X13.flag_calls(edited(c2, edits).ram)
                expect([], f'flag_calls with {what}')
            except ValueError:
                expect(['refused'], f'flag_calls with {what}')
        hb = edited(c2, [(node + 0x0D, bytes([c2.ram[rec + 4]]))])     # state 0 ran with the bit set
        expect([] if X13.flag_calls(hb.ram) == ((0x1F, 0), (0x20, 1)) else ['x'],
               'flag_calls of a node spawned with the bit set', accept=True)
        cells = (T13.A / 'sub0/area13_cells.bin').read_bytes()
        expect(X13.verify_directory(K.elf, cells, [hb])[1], 'a13c_02 read as spawned with the bit set')
        node3 = T13.node_of(c3.ram, X13.FLAG_OWNER)
        expect(X13.verify_directory(K.elf, cells, [edited(c3, [(node3 + 0x0D, b'\x13'), (node3 + 0x28, b'\x05\0')])])[1],
               'a13c_03 read as still counting down')
        expect([] if X13.flag_calls(by['a13b_00_ladder_up'].ram) == X13._LANE_FLAG_CALLS(
            by['a13b_00_ladder_up'].ram) else ['x'], 'a13b_00: the lane\'s own rule', accept=True)
        # equivalence: on the reload arrival the lane's verify_directory and this one agree
        a = X13._LANE_VERIFY_DIRECTORY(K.elf, cells, [by['a13b_00_ladder_up']])
        b = X13.verify_directory(K.elf, cells, [by['a13b_00_ladder_up']])
        expect([] if a == b and not b[1] else ['x'], 'a13b_00: the lane\'s directory rule unchanged', accept=True)
        # knocked drums
        _c, hulls, _s = L.cell_directory(cells, 0)
        table = C.u32(c3.spad, L.SPAD_CELLS)
        expect(X13.verify_directory(K.elf, cells, [c2, c3])[1], 'a13c_02 + a13c_03', accept=True)
        expect(X13.verify_directory(K.elf, cells, [c3])[1], 'a13c_03 without its donor')
        s7 = hulls[7][0]
        for at, what in ((0x2C, 'an extent'), (0x18, 'its flag word'), (0x0, 'min x'), (0x20, 'centre x')):
            expect(X13.verify_directory(K.elf, cells, [c2, edited(c3, [flip(c3.ram, table + s7 + at, 0x10)])])[1],
                   f'drum hull 7 {what} changed')
        dn = T13.node_of(c2.ram, X13.DRUM, 7)
        expect(X13.verify_directory(K.elf, cells, [edited(c2, [(dn + 4, b'\x02')]), c3])[1],
               'the donor drum not at rest')
        expect(X13.verify_directory(K.elf, cells, [edited(c2, [(dn + 0x10, struct.pack('<I', 0x219550))]), c3])[1],
               'the donor node not a drum')
        expect(X13.verify_directory(K.elf, cells, [edited(c2, [(dn, b'\0')]), c3])[1],
               'the donor drum freed')
        c0 = by['a13b_00_ladder_up']
        img3, _u = X13.E02.derive_words(K.elf, cells, c3)
        d9 = T13.node_of(c2.ram, X13.DRUM, 9)
        expect([] if X13.knocked_drum_hull(K.elf, img3, c3, 7, hulls, c2, d9) == b'' else ['x'],
               'knocked_drum_hull of hull 7 run with the uid-9 drum (it writes another hull)', accept=True)
        expect(X13.verify_directory(K.elf, cells, [c0, edited(c2, [(dn + 0x10, struct.pack('<I', 0x219550))]), c3])[1],
               'the uid\'s last owner not a drum, a drum before it')
        expect(X13.verify_directory(K.elf, cells, [c0, c3])[1], 'a13c_03 with a13b_00 as the donor', accept=True)
        # [45] after its sequence
        seq = T13.node_of(c3.ram, X13.SEQUENCE_OWNER)
        expect([] if X13.explicit_model_problems(c3.ram, 'c') == [] else ['x'], '[45] as captured', accept=True)
        expect(T13.model_binding_problems((T13.A / 'sub0/world_models.emwm').read_bytes(),
                                          [edited(c3, [(seq + 5, b'\x03')])]), '[45] at +5 = 3')
        expect(X13.explicit_model_problems(edited(c3, [(seq + 0x0C, b'\x01')]).ram, 'c'), '[45] bone count of model 0x12')
        expect(X13.explicit_model_problems(edited(c3, [flip(c3.ram, seq + 0x44 + 1, 0x10)]).ram, 'c'), '[45] +0x44 changed')
        expect(X13.explicit_model_problems(edited(c3, [(seq + 0x110 + 8, bytes(4))]).ram, 'c'), '[45] a bone slot cleared')
        expect([] if not [a for _s, a in X13.explicit_model_nodes(edited(c3, [(seq + 0x0D, b'\x12')]).ram)
                          if a == seq] else ['x'], '[45] with +0x0D 0x12 left to the generic rule', accept=True)
        # the [47] field rule
        expect(T13.field_problems((T13.A / 'sub0/roster.emro').read_bytes(), c2.ram), 'a13c_02 [47] field', accept=True)
        expect(T13.field_problems((T13.A / 'sub0/roster.emro').read_bytes(),
                                  edited(c3, [(node3 + 0x0D, b'\x13')]).ram), 'a13c_03 [47] +0x0D 0x13 after the restore')
        expect(T13.field_problems((T13.A / 'sub0/roster.emro').read_bytes(),
                                  edited(c2, [(node + 0x0D, b'\x15')]).ram), 'a13c_02 [47] +0x0D 0x15')
        # the hatch field rule ([62] spawned opened after the reload)
        roster_blob = (T13.A / 'sub0/roster.emro').read_bytes()
        hatch = next(a for _s, a in T.pool_nodes(c0.ram) if C.u32(c0.ram, a + 0x10) == HATCH and c0.ram[a + 0x9A] == 62)
        expect(T13.field_problems(roster_blob, c0.ram), 'a13b_00 hatch fields', accept=True)
        expect(T13.field_problems(roster_blob, edited(c0, [(hatch + 0x0D, b'\x22')]).ram), 'the hatch +0x0D 0x22')
        expect(T13.field_problems(roster_blob, edited(c0, [(0x810839, b'\0')]).ram), 'the hatch 0xD without its bit')
        expect(T13.field_problems(roster_blob, edited(c0, [(hatch + 4, b'\x01')]).ram), 'the hatch 0xD in state 1')
        # rules / proofs
        rec_cells = json.loads((RELOAD / 'sub0/cells.json').read_text())
        expect(rule_problems(caps, rec_cells, rows), 'the rules as exported', accept=True)
        r2 = copy.deepcopy(rec_cells)
        r2['cells']['flag_calls']['a13c_02_battery'] = [[0x1F, 0], [0x20, 1]]
        expect(rule_problems(caps, r2, rows), 'a13c_02 recorded with the restored pair')
        r2 = copy.deepcopy(rec_cells)
        row = next(x for x in r2['cells']['captures'] if x['capture'] == 'a13c_03_blast')
        row['proofs']['13'] = 'orphan'
        expect(rule_problems(caps, r2, rows), 'a13c_03 drum 13 recorded as a bare orphan')
        expect(rule_problems(caps, rec_cells, None), 'no recomputed rows')
        r2, rows2 = copy.deepcopy(rec_cells), copy.deepcopy(rows)
        for x in r2['cells']['captures'] + rows2:
            if x['capture'] == 'a13c_03_blast':
                key13 = '13' if '13' in x['proofs'] else 13
                x['proofs'][key13] = 'orphan: equal to a derivation of it in another capture'
        expect(rule_problems(caps, r2, rows2), 'drum 13 proved as an ordinary orphan in both the record and the rows')
        expect(rule_problems([same(c3, [(seq + 5, b'\x02')])], rec_cells, rows),
               '[45] in a13c_03 before its sequence')
        # census
        expect(census_problems(cb, cc), 'the census as recorded', accept=True)
        for what, change in (
                ('001FFCD0 in a13c_02', lambda b, c: c['functions'].append(dict(addr='0x1ffcd0', beats=['a13c_02_battery']))),
                ('0019C6F0 in a13c_02', lambda b, c: next(x for x in c['functions'] if x['addr'] == '0x19c6f0')['beats'].append('a13c_02_battery')),
                ('0019C6F0 first at frame 1732', lambda b, c: next(x for x in c['functions'] if x['addr'] == '0x19c6f0').update(first_frame=1732)),
                ('0019C6F0 in a13b_01', lambda b, c: next(x for x in b['functions'] if x['addr'] == '0x19c6f0')['beats'].append('a13b_01_door17')),
                ('001FFCD0 not in a13b_05', lambda b, c: next(x for x in b['functions'] if x['addr'] == '0x1ffcd0').update(beats=['a13b_00_ladder_up'])),
                ('an unattributed a13c hit', lambda b, c: c['unattributed_hits'].append({})),
                ('a13c beat missing', lambda b, c: c['summary'].update(beats_missing=['a13c_06_south'])),
                ('a13c beat dropped', lambda b, c: c.update(per_beat=c['per_beat'][:-1]))):
            b2, c2_ = copy.deepcopy(cb), copy.deepcopy(cc)
            change(b2, c2_)
            expect(census_problems(b2, c2_), what)
        # lane_checks itself on a tampered reload tree
        expect(lane_checks(every, caps, tampered(ident_hash), BASE, first_ram, cb, cc, rows),
               'lane_checks with an identical hash changed')
        c3_ = copy.deepcopy(cc)
        c3_['unattributed_hits'].append({})
        expect(lane_checks(every, caps, RELOAD, BASE, first_ram, cb, c3_, rows), 'lane_checks with an a13c census hit')
        n += review_controls(K, every, caps, first_ram, cb, cc, rows, tampered, edited, same, flip, by)
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)
    return n


def review_controls(K, every, caps, first_ram, cb, cc, rows, tampered, edited, same, flip, by):
    """Pinned cases for the survivors of the round-11 review sweep (named
    R.. in docs/AREA13X_ASSETS.md, Mutation sweep). Each names the text the
    rejecting check must print, so a different check that happens to fire
    does not count. Returns the count."""
    n = 0

    def has(problems, text, what):
        nonlocal n
        n += 1
        check(any(text in p for p in problems), f'control: {what} missed (no problem naming {text!r}: {problems[:3]})')

    def ok(cond, what):
        nonlocal n
        n += 1
        check(cond, f'control: {what} rejected')

    def calls(ram):
        """X13.flag_calls, or the refusal as a value (so a control prints
        a failure instead of stopping the run)."""
        try:
            return X13.flag_calls(ram)
        except ValueError as error:
            return f'refused: {error}'
    c0, c2, c3, c4 = (by[k] for k in ('a13b_00_ladder_up', 'a13c_02_battery', 'a13c_03_blast', 'a13c_04_cure'))
    cells = (T13.A / 'sub0/area13_cells.bin').read_bytes()

    def set_json(t, rel, change, record_sha=True):
        """Change a reload/ JSON file; with record_sha, reload.json's
        sha256 of it follows (so only the content check can catch it)."""
        d = json.loads((t / rel).read_text())
        change(d)
        data = json.dumps(d).encode()
        (t / rel).write_bytes(data)
        if record_sha:
            r = json.loads((t / 'reload.json').read_text())
            r['differing'][rel]['sha256'] = sha(data)
            (t / 'reload.json').write_text(json.dumps(r))
    # R01: the bit test is 0x40 alone (a13c_03 has 0x74; 0x64 keeps 0x40, clears 0x10)
    ok(calls(edited(c3, [(0x8107F4, b'\x64')]).ram) == ((0x1F, 0), (0x20, 1)),
       'flag_calls of a13c_03 with D_008107F4 = 0x64')
    # R02: +0x28 is a halfword (0x100: its low byte 0)
    node2 = T13.node_of(c2.ram, X13.FLAG_OWNER)
    ok(calls(edited(c2, [(node2 + 0x28, struct.pack('<h', 0x100))]).ram) == ((0x1F, 1), (0x20, 0)),
       'flag_calls of a13c_02 with +0x28 = 0x100')
    # R06: [45]'s explicit model needs its state 1
    seq = T13.node_of(c3.ram, X13.SEQUENCE_OWNER)
    ok(seq not in [a for _s, a in X13.explicit_model_nodes(edited(c3, [(seq + 4, b'\x02')]).ram)],
       '[45] in state 2 left to the generic rule')
    # R09: the AREA13 lane's hatch rule stays in explicit_model_nodes (a hatch
    # opened in play keeps its record's +0x0D and carries model 0xD)
    hatch = next(a for _s, a in T.pool_nodes(c0.ram) if C.u32(c0.ram, a + 0x10) == HATCH and c0.ram[a + 0x9A] == 62)
    hrec = X13.PLACEMENTS + 0x28 * 62
    opened = edited(c0, [(hatch + 0x0D, bytes([c0.ram[hrec + 4]]))])
    ok(hatch in [a for _s, a in X13.explicit_model_nodes(opened.ram)] and
       not X13.explicit_model_problems(opened.ram, 'h'), 'the hatch opened in play (+0x0D the record\'s)')
    # R14: the uid-75 orphan of a13c_04 (quick mode has no a13c_04 otherwise)
    rows4, probs4, _c = X13.verify_directory(K.elf, cells, [c2, c4])
    row4 = next((r for r in rows4 if r['capture'] == c4.name), {})
    ok(not probs4 and row4.get('proofs', {}).get(75) == 'orphan: equal to a derivation of it in another capture',
       'a13c_02 + a13c_04: hull 75 an orphan equal to a13c_02\'s derivation')
    # R15: the drum donor is searched among EARLIER captures only
    dn7 = T13.node_of(c2.ram, X13.DRUM, 7)
    later = edited(c2, [(dn7 + 0x10, struct.pack('<I', 0x219550))])
    _r, pl, _c = X13.verify_directory(K.elf, cells, [c2, c3, later])
    ok(not [p for p in pl if p.startswith(c3.name + ':')], 'a13c_03 with a later capture whose uid 7 is no drum')
    # R17: D_0028A5A4's previous capture
    has(capture_problems(every, tampered(lambda t: set_json(t, 'sub0/level/level.json', lambda d: d[
        'stale_d_0028a5a4'].update(previous_capture='a04b_03_door')))), 'previous capture',
        'level.json naming another previous capture')
    saved = LV.PREVIOUS['area13']
    try:
        LV.PREVIOUS['area13'] = A13.ROUTE_A13 / 'a13_04_hatch'
        has(capture_problems(every, RELOAD), "PREVIOUS['area13']", 'PREVIOUS not re-pointed')
    finally:
        LV.PREVIOUS['area13'] = saved
    # R20: reload.json's word rows against the independent pins
    def shifted(t):
        r = json.loads((t / 'reload.json').read_text())
        for row in r['differing'].values():
            for w in row.get('words') or ():
                w[0] = hex(int(w[0], 16) + 4)
        (t / 'reload.json').write_text(json.dumps(r))
    has(split_problems(tampered(shifted), BASE), 'reload.json words', 'word rows at offset + 4')
    has(cursor_problems([edited(caps[-1], [flip(caps[-1].ram, D_0028A5A0 + 1, 0x10)])], first_ram),
        'bank bases', 'D_0028A5A0 changed')
    # R26: the excluded folders
    has(capture_problems(every, RELOAD, excluded=list(X13.EXCLUDED) + ['a13c_07_elsewhere']), 'excluded captures',
        'an extra excluded folder')
    # R29 / R30: split bookkeeping
    def both(t):
        r = json.loads((t / 'reload.json').read_text())
        r['identical']['tables.json'] = sha((BASE / 'tables.json').read_bytes())
        (t / 'reload.json').write_text(json.dumps(r))
    has(split_problems(tampered(both), BASE), 'exactly once', 'tables.json listed as identical and differing')
    def zero(t):
        r = json.loads((t / 'reload.json').read_text())
        r['differing']['sub0/level/level.json']['sha256'] = '0' * 64
        (t / 'reload.json').write_text(json.dumps(r))
    has(split_problems(tampered(zero), BASE), 'differing file hashes', 'level.json sha256 zeroed')
    # R31: the knocked-drum donor
    rec_cells = json.loads((RELOAD / 'sub0/cells.json').read_text())
    r2, rows2 = copy.deepcopy(rec_cells), copy.deepcopy(rows)
    for x in r2['cells']['captures'] + rows2:
        for u, p in list(x['proofs'].items()):
            if p.startswith('knocked drum'):
                x['proofs'][u] = p.replace(f'as in {DRUM_DONOR})', 'as in a13b_00_ladder_up)')
    has(rule_problems(caps, r2, rows2), 'another capture than', 'the drum donor a13b_00 in the record and the rows')
    # R32 / R33: census
    c_ = copy.deepcopy(cc)
    c_['overlay_hits_other_overlay'] = [dict(addr='0x823500')]
    has(census_problems(cb, c_), 'another overlay', 'an a13c hit in another overlay')
    c_ = copy.deepcopy(cc)
    c_['summary']['passes'] = ['A13C', 'X']
    has(census_problems(cb, c_), 'passes', 'a13c census passes [A13C, X]')
    # R35 / R36 / R37: lane_checks runs each component (the problem text names it)
    flipped = [same(x, [flip(x.ram, 0x810702)]) if x.name == 'a13b_02_button15' else x for x in every]
    has(lane_checks(flipped, caps, RELOAD, BASE, first_ram, cb, cc, rows), 'area bytes',
        'lane_checks with a13b_02\'s entry byte flipped')
    moved = [same(x, [flip(x.ram, C.D_0028A73C + 1, 0x10)]) if x is caps[-1] else x for x in caps]
    has(lane_checks(every, moved, RELOAD, BASE, first_ram, cb, cc, rows), 'D_0028A73C',
        'lane_checks with D_0028A73C changed')
    has(lane_checks(every, caps, tampered(lambda t: set_json(t, 'sub0/cells.json', lambda d: d['cells'][
        'flag_calls'].update(a13c_02_battery=[[0x1F, 0], [0x20, 1]]))), BASE, first_ram, cb, cc, rows),
        'cells.json flag calls', 'lane_checks with a13c_02\'s flag pair changed (sha recorded)')
    # R38: the roster pins of every capture, in quick mode too
    blob = (T13.A / 'sub0/roster.emro').read_bytes()
    pins = dict(ROSTER_LIVE, a13c_04_cure=(51, 50))
    has(roster_pin_problems(every, blob, pins), 'a13c_04_cure: roster', 'a13c_04 roster pinned (51, 50)')
    has(roster_pin_problems([same(c4, [(T13.node_of(c4.ram, X13.FLAG_OWNER), b'\0')])], blob), 'a13c_04_cure: roster',
        'a13c_04 with [47]\'s node freed')
    return n


# ---------------------------------------------------------------------------


def main():
    need = [RELOAD / 'reload.json', BASE / 'sub0/level/level.json', C.ELF_PATH, C.ISO_PATH,
            A13.AREA13.overlay_path, A13_04, CENSUS_B, CENSUS_C] + \
        [X13.ROUTE_A13B / n / 'eeMemory.bin' for n in X13.CAPTURE_NAMES if n.startswith('a13b')] + \
        [X13.ROUTE_A13C / n / 'eeMemory.bin' for n in X13.CAPTURE_NAMES if n.startswith('a13c')]
    missing = [str(p) for p in need if not p.exists()]
    if missing:
        print('area13x assets reference: SKIPPED, missing local inputs:', missing)
        return 0
    t0 = time.time()
    try:
        build_view(RELOAD, BASE, VIEW)
    except (OSError, KeyError, json.JSONDecodeError) as error:
        print(f'area13x assets reference: FAIL (reload.json: {error!r})')
        return 1
    try:
        configure(VIEW)
        el = L.load_export_level()
        lib = T01.build_loaders()
        bg = T13.background_lib()
        K, caps = T13.target_setup('area13', el)
        print(f'area13x assets reference ({MODE}) AREA13 sub 0, second load: {len(caps)} of {len(K.every)} captures')
        loaded = T13.check_loaders(lib, bg, K.t, VIEW)
        for lname, ok in loaded.items():
            check(ok, f'port loader rejects {lname}')
        print(f'  loaders: {sum(loaded.values())}/{len(loaded)} files accepted by the port loaders')
        _ROWS.clear()
        LV.verify_directory = recording_verify_directory(X13.verify_directory)
        try:
            problems, V = T13.run_checks(K, caps)
        finally:
            X13.install()
        for p in problems:
            check(False, f'area13: {p}')
        lv = V.get('level', {})
        print(f"  AREA13 lane's checks: {lv.get('objects')} bank objects -> {lv.get('zones')} zone EMDL(s), "
              f"{lv.get('kicks')} level kicks; collision, cells, tables, sfx, ctx: {len(problems)} problems")
        rows = _ROWS.get(tuple(c.name for c in caps))
        with open(A13_04, 'rb') as f:
            first_ram = f.read(0x28A800)
        cb, cc = json.loads(CENSUS_B.read_text()), json.loads(CENSUS_C.read_text())
        lp = lane_checks(K.every, caps, RELOAD, BASE, first_ram, cb, cc, rows)
        for p in lp:
            check(False, f'lane: {p}')
        print(f'  lane checks (captures, roster pins, cursor, split, rules, census): {len(lp)} problems')
        if FAILS:
            print(f'  canary and controls: skipped, {len(FAILS)} check(s) already failed')
        else:
            T13.use(K)
            print(f'  canary: {T13.canary(K, lib, bg, caps[0])} sections each reported their planted difference')
            if not FAILS:
                print(f'  controls: {lane_controls(K, caps, K.every, first_ram, cb, cc, rows)} changed inputs, each '
                      'caught (or accepted where marked)')
    finally:
        if VIEW.exists():
            shutil.rmtree(VIEW)
    print(f'area13x assets reference: {"FAIL" if FAILS else "PASS"} ({len(FAILS)} failures, '
          f'{time.time() - t0:.1f} s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
