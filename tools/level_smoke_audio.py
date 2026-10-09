#!/usr/bin/env python3
"""The level smoke's sound check (chain step AUDIO; FIRST_LEVEL_AUDIT.md
section 1b item 1; LEVEL_SMOKE.md "The sound state").

The port's tick log carries, per tick, the EE sound state at the main-loop
top ("snd", em_scene_bindings.c log_sound): the stream cues D_00282178, the
lanes' bytes, the voice ring D_00281CF0, the looped-service tables
D_00281B70 / D_00281C30, the delayed cues D_00281F30, the voice records
D_0027CCC0 and the reaper's feedback D_002817C0. The decomp's audio captures
(../Extermination/build/s87/audio/<beat>/, docs/CAPTURES_AUDIO.md) hold the
same tables at every main-loop top of ten first-level beats of the original.
Nothing from them is copied or printed beyond compared values.

Two kinds of comparison:

- Row for row, where the level smoke already aligned a phase with the route
  stretch an audio beat repeats row for row (battery_ui = route 01,
  elevator_ride = route 04, roger_encounter = route 14): the port's tick
  i0 + k + 1 (its loop-top sample of the frame i0 + k) against the beat's
  row f0 + k, in the cues, the delayed cues, the voice ring, the ids in
  D_00281B70 / D_00281C30 and every voice keyed after the window's first row
  (its key-on row, state, note, release, sustain, priority, alloc,
  portamento fields, program, bank handle, and which voices one track keyed
  together), so each sound's key-on tick and its end (the reaper's reset,
  which follows the IOP's ENVX reply) is compared tick by tick.
- As sequences, where the capture is a separate run of a route beat
  (panel_power, cage_roof) or the port's stretch is not row-identical: the
  ordered sounds keyed in the stretch with each voice's life in ticks, and
  the ordered stream cues.

What the check leaves out, and why:
- voices keyed before the window: their tracks, voices and ages follow
  each run's own history (the navigation between the phases);
- the fans' 0x451 (00827630): its cycle follows the ticks since the room's
  spawn, which navigation changes (LEVEL_SMOKE.md "The security gun, its
  cable and the fan pair");
- the track and voice indices: 00119EA0 takes the lowest free track and
  00117428 the next free voice from the cursor, both of which hold the
  history's sounds; which voices one track keyed together is compared;
- the audible stages (reverb, interpolation, ADSR, levels): no WAV of the
  original exists (CAPTURES_AUDIO.md).
"""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
AUDIO = DECOMP / 'build/s87/audio'

# D_0027CCC0 offsets in EmSoundSample.voice order (em_stream_live.h).
VOICE_FIELDS = (0, 2, 6, 8, 0xA, 0xC, 0x1A, 0x1C, 0x1E, 0x20, 0x44, 0x4E, 0x60, 0x62, 0x64, 0x3E, 0x22)
STATE, NOTE, OWNER, RELEASE, SERIAL, SUSTAIN, KIND, AGE = range(8)
FREE = 0xFFFF
# Sounds whose timing follows each run's history (module docstring).
HISTORY_IDS = {0x451: 'the fans\' 00827630 cycle'}


def _layout(beat):
    lay = json.loads((AUDIO / beat / 'sound_layout.json').read_text())['sound_state.bin']
    blocks, at = [], 0
    for b in lay['blocks']:
        blocks.append((int(b['address'], 16), at, b['bytes']))
        at += b['bytes']
    return lay['record_bytes'], blocks


def beat_rows(beat):
    """The capture's rows: the sound tables in the tick log's form, plus the
    route row."""
    size, blocks = _layout(beat)
    data = (AUDIO / beat / 'sound_state.bin').read_bytes()
    routes = [json.loads(line)['route'] for line in (AUDIO / beat / 'frames.jsonl').open()]
    rows = []
    for i in range(len(data) // size):
        rec = data[i * size:(i + 1) * size]

        def rd(addr, n, rec=rec):
            for base, off, length in blocks:
                if base <= addr and addr + n <= base + length:
                    return rec[off + addr - base:off + addr - base + n]
            raise KeyError(hex(addr))

        w = lambda a: struct.unpack('<i', rd(a, 4))[0]
        lane = rd(0x282154, 8)
        vo = {}
        for v in range(48):
            r = rd(0x27CCC0 + 0x6A * v, 0x6A)
            f = tuple(struct.unpack_from('<H', r, o)[0] for o in VOICE_FIELDS)
            if f[STATE] or f[OWNER] != FREE or f[16] != FREE:
                vo[v] = f
        rows.append({
            'cue': [w(0x282178 + 4 * k) for k in range(3)],
            'lane': [struct.unpack('b', lane[k:k + 1])[0] for k in range(5)] + [lane[7]],
            'ring': [w(0x281CF0 + 4 * k) for k in range(16)],
            'rh': struct.unpack('b', rd(0x275B30, 1))[0], 'rt': struct.unpack('b', rd(0x275B34, 1))[0],
            'b70': sorted(w(0x281B70 + 4 * k) for k in range(48) if w(0x281B70 + 4 * k) != -1),
            'c30': sorted(w(0x281C30 + 4 * k) for k in range(48) if w(0x281C30 + 4 * k) != -1),
            'f30': [list(struct.unpack('<4i', rd(0x281F30 + 16 * k, 16))) for k in range(10)],
            'vo': vo, 'envx': {v: w(0x2817C0 + 4 * v) for v in range(48)},
            'route': routes[i] if i < len(routes) else None,
        })
    return rows


def port_row(snd):
    if snd is None:
        return None
    vo = {x[0]: tuple(x[1:]) for x in snd['vo']}
    return {'cue': snd['cue'], 'lane': snd['lane'], 'ring': snd['ring'], 'rh': snd['rh'], 'rt': snd['rt'],
            'b70': sorted(w for _, w in snd['b70']), 'c30': sorted(w for _, w in snd['c30']),
            'f30': snd['f30'], 'vo': vo, 'envx': {v: w for v, w in snd['envx']},
            'tr': {k: i for k, i in snd['tr']}}


def identity(f):
    """A voice record's fields other than its index, owner track and age."""
    return (f[STATE], f[NOTE], f[RELEASE], f[SUSTAIN], f[KIND]) + tuple(f[8:])


def signature(f):
    return (f[NOTE], f[15], f[16])   # note, program, bank handle


class Ids:
    """The sound id of each port voice (the track's registry entry at its
    key-on, from the tick log's "tr"), and the voice signatures they give,
    to recognise the excluded sounds in the capture."""

    def __init__(self):
        self.by_signature = {}

    def learn(self, port_rows):
        for row in port_rows:
            if not row:
                continue
            for f in row['vo'].values():
                if f[KIND] == 2 and f[AGE] == 0 and f[OWNER] in row['tr']:
                    self.by_signature.setdefault(signature(f), set()).add(row['tr'][f[OWNER]])

    def history(self, f):
        return bool(self.by_signature.get(signature(f), set()) & set(HISTORY_IDS))


def window_groups(row, k, ids):
    """The voices keyed after the window's first row (k = the row's index in
    the window): {(key-on index, owner): sorted identities}, as a sorted
    list of (key-on index, identities) so the owner's number drops out."""
    groups = {}
    for f in row['vo'].values():
        if f[KIND] != 2 or f[OWNER] == FREE:
            continue
        born = k - f[AGE]
        if born < 1 or ids.history(f):
            continue
        groups.setdefault((born, f[OWNER]), []).append(identity(f))
    return sorted((born, tuple(sorted(v))) for (born, _), v in groups.items())


def _end_skew(pg, og, k, port, orig, ids):
    """1 when the only difference of row k is a sound the reaper reset in
    one run and the next tick in the other: groups both rows held at k - 1
    (unchanged), every voice still keyed (state 1: no stop), and gone from
    both at k + 1. The reaper's input is the IOP's ENVX reply, sampled at the
    IOP driver's own tick (every 64 H-lines), whose phase against the frame
    drifts by 13 half-lines a field from where each run's IOP timer started
    (the original's: its boot, the title's dwell included; the port's SFX
    side: the New Game's area load): a sample's end near one of those ticks
    lands one exchange apart (IOP_STREAM.md, SFX_SEQUENCER.md "Where the
    driver runs"). Hardware timing, not the code's (PORT_PROFILES.md)."""
    if k < 1 or k + 1 >= len(port):
        return 0
    ponly = [g for g in pg if g not in og]
    oonly = [g for g in og if g not in pg]
    extra = ponly or oonly
    if (ponly and oonly) or not extra:
        return 0
    before = (window_groups(port[k - 1], k - 1, ids), window_groups(orig[k - 1], k - 1, ids))
    after = (window_groups(port[k + 1], k + 1, ids), window_groups(orig[k + 1], k + 1, ids))
    for g in extra:
        if g not in before[0] or g not in before[1] or g in after[0] or g in after[1]:
            return 0
        if any(v[0] != 1 for v in g[1]):
            return 0
    return 1


def compare_rows(port, orig, ids, what):
    """Row for row: port[k] and orig[k] for every k. Returns (voices keyed,
    resets one tick apart)."""
    assert len(port) == len(orig) and port, (what, 'empty or uneven window', len(port), len(orig))
    keyed = skews = 0
    for k, (p, o) in enumerate(zip(port, orig)):
        assert p is not None, (what, 'no sound sample in the port at window row', k)
        for key in ('cue', 'f30', 'ring', 'rh', 'rt', 'b70', 'c30'):
            assert p[key] == o[key], (what, f'row {k}', key, p[key], o[key])
        pg, og = window_groups(p, k, ids), window_groups(o, k, ids)
        if pg != og:
            assert _end_skew(pg, og, k, port, orig, ids), \
                (what, f'row {k}', 'the voices keyed in the window differ', pg, og)
            skews += 1
        keyed += sum(len(g) for born, g in og if born == k)
    return keyed, skews


def same_sound(a, b):
    """Two sounds of sequence(): the same voices, each with the same life,
    or one tick apart when it ended unstopped (_end_skew)."""
    if len(a) != len(b):
        return False
    for (ia, ea, sa), (ib, eb, sb) in zip(a, b):
        if ia != ib or sa != sb:
            return False
        if ea != eb and not (ea is not None and eb is not None and sa is None and abs(ea - eb) == 1):
            return False
    return True


def sequence(rows, start, end, ids):
    """The sounds keyed in rows[start:end] (each with its voices' lives in
    rows, None when still live at the end) and the stream cues' changes."""
    sounds, cues = [], []
    live = {}
    prev_cue = None
    for k in range(start, end):
        row = rows[k]
        if row is None:
            continue
        if row['cue'] != prev_cue:
            cues.append(tuple(row['cue']))
            prev_cue = row['cue']
        seen = set()
        for v, f in row['vo'].items():
            if f[KIND] != 2 or f[OWNER] == FREE or ids.history(f):
                continue
            born = k - f[AGE]
            if born <= start:
                continue
            key = (v, born)
            seen.add(key)
            if key not in live:
                live[key] = {'born': born, 'id': identity(f)[1:], 'owner': f[OWNER], 'end': None,
                             'stopped': None}
            if f[STATE] == 0 and live[key]['stopped'] is None:
                live[key]['stopped'] = k - born
        for key, s in live.items():
            if key not in seen and s['end'] is None:
                s['end'] = k - s['born']
    groups = {}
    for s in live.values():
        groups.setdefault((s['born'], s['owner']), []).append((s['id'], s['end'], s['stopped']))
    for (born, _), voices in sorted(groups.items()):
        sounds.append((born, tuple(sorted(voices, key=lambda x: (x[0], x[1] is None, x[1] or 0)))))
    return sounds, cues


# ------------------------------------------------------------ the check

def _selector(route_row):
    return route_row['spad'][2:4]


def own_scan(rows, near):
    """The capture's first row at or after `near` - 8 whose selector byte
    0x70003B8D is set after a row with it clear (the scan, as
    scan_alignment finds it in the route rows)."""
    return next(k for k in range(max(1, near - 8), len(rows))
                if _selector(rows[k]['route']) != '00' and _selector(rows[k - 1]['route']) == '00')


def cue_anchor(rows, start, base):
    """The first row at or after `start` whose lane-0 cue is `base` again
    after a row without it (the status close's 001FAE70(1))."""
    return next(k for k in range(start + 1, len(rows)) if rows[k]['cue'][0] == base and rows[k - 1]['cue'][0] != base)


def _port_anchor(P, start, base):
    return next(i for i in range(start + 1, len(P)) if P[i] and P[i - 1] and P[i]['cue'][0] == base
                and P[i - 1]['cue'][0] != base)


def _rows(P, O, i_line, f_row, n, what):
    """Port lines i_line.. against capture rows f_row.. (n rows)."""
    assert f_row >= 0 and f_row + n <= len(O) and i_line + n <= len(P), (what, 'window outside the logs')
    return [P[i_line + k] for k in range(n)], [O[f_row + k] for k in range(n)]


def check(ticks, run, state):
    """Every window the run's phases recorded (state['audio']), against the
    audio captures. Returns the report line."""
    windows = state.get('audio', [])
    if not windows:
        return 'none of this run\'s phases has an audio capture'
    assert AUDIO.is_dir(), f'audio captures missing: {AUDIO} (decomp docs/CAPTURES_AUDIO.md)'
    P = [port_row(t.get('snd')) for t in ticks]
    ids = Ids()
    ids.learn(P)
    beats, out = {}, []
    for w in windows:
        O = beats.setdefault(w['beat'], beat_rows(w['beat']))
        what = f'{w["phase"]} ({w["beat"]})'
        if w['mode'] == 'rows':
            # The port's loop-top sample of frame i0 + k is line i0 + k + 1.
            # A capture that is a separate run of the route beat is aligned
            # on its own scan row near the route's.
            f0 = own_scan(O, w['near']) if 'near' in w else w['f0']
            w = dict(w, f0=f0, n=min(w['n'], len(O) - f0))
            port, orig = _rows(P, O, w['i0'] + 1, w['f0'], w['n'], what)
            keyed, skews = compare_rows(port, orig, ids, what)
            # The voices keyed in the window live on the sound thread's
            # field clock, whatever the frames do after it (a page load at
            # host speed): their records row for row to their ends.
            k, n = w['n'], w['n']
            mine = lambda row, j: [g for g in window_groups(row, j, ids) if g[0] < n]
            while w['f0'] + k + 1 < len(O) and w['i0'] + 2 + k < len(P):
                p, o = P[w['i0'] + 1 + k], O[w['f0'] + k]
                pg, og = mine(p, k), mine(o, k)
                if pg != og:
                    # A reset one tick apart (_end_skew), on the window's own voices.
                    pp, op = mine(P[w['i0'] + k], k - 1), mine(O[w['f0'] + k - 1], k - 1)
                    pn, on = mine(P[w['i0'] + 2 + k], k + 1), mine(O[w['f0'] + k + 1], k + 1)
                    extra = [g for g in pg if g not in og] + [g for g in og if g not in pg]
                    assert all(g in pp and g in op and g not in pn and g not in on and
                               all(v[0] == 1 for v in g[1]) for g in extra) and \
                        not ([g for g in pg if g not in og] and [g for g in og if g not in pg]), \
                        (what, f'row {k} after the window', 'the window\'s voices differ', pg, og)
                    skews += 1
                if not og and not pg:
                    break
                k += 1
            line = (f'{w["beat"]} f{w["f0"]}..f{w["f0"] + w["n"] - 1} row for row ({keyed} voices keyed'
                    f'{f", {skews} reset one tick apart" if skews else ""})')
            if k > n:
                line += f' and its voices to their ends (f{w["f0"] + k - 1})'
            if w.get('after_status'):
                # After the page module's load (host speed drops its busy
                # polls): from the status close's music cue to the beat's
                # end, row for row on that cue.
                base = orig[0]['cue'][0] if orig[0]['cue'][0] else 25
                fo = cue_anchor(O, w['f0'] + w['n'] - 1, base)
                ip = _port_anchor(P, w['i0'] + w['n'], base)
                back = min(w['after_status'], fo - (w['f0'] + w['n']))
                port, orig = _rows(P, O, ip - back, fo - back, len(O) - fo + back, what + ' after the status')
                k2, s2 = compare_rows(port, orig, ids, what + ' after the status')
                line += (f', then f{fo - back}..f{len(O) - 1} on the status close\'s cue ({k2} voices keyed'
                         f'{f", {s2} reset one tick apart" if s2 else ""})')
            out.append(line)
        elif w['mode'] == 'sequence':
            fo = own_scan(O, w['near']) if w.get('near') is not None else w['f0']
            n = min(w['n'], len(O) - fo)
            ps, pc = sequence(P, w['i0'] + 1, w['i0'] + 1 + n, ids)
            os_, oc = sequence(O, fo, fo + n, ids)
            assert pc == oc, (what, 'the stream cues\' sequence differs', pc, os_ and oc)
            assert len(ps) == len(os_) and all(same_sound(a[1], b[1]) for a, b in zip(ps, os_)), \
                (what, 'the sounds keyed in order differ', ps[:8], os_[:8])
            drift = sorted({b - a for (a, _), (b, _) in zip(os_, ps)})
            out.append(f'{w["beat"]} from f{fo}: {len(os_)} sounds and {len(oc)} cue states in order, each '
                       f'voice\'s life equal (key-on rows {"equal" if drift in ([], [0]) else "offset " + str(drift)})')
        elif w['mode'] == 'opening':
            out.append(check_opening(ticks, P, O, ids, what))
        elif w['mode'] == 'loops':
            # A designed beat played closed loop from the port's own state
            # (its rand() stream, and so the footsteps' variants and the
            # flinch's clip, are the run's own): the looped services row for
            # row from the beat's first row, and the sustained voices the
            # window starts with (the flame's 0x413), their fields but the age.
            n = min(w['n'], len(O))
            port, orig = _rows(P, O, w['i0'] + 1, 0, n, what)
            for k, (p, o) in enumerate(zip(port, orig)):
                assert p is not None, (what, 'no sound sample in the port at row', k)
                for key in ('cue', 'b70', 'c30', 'f30'):
                    assert p[key] == o[key], (what, f'row {k}', key, p[key], o[key])
                held = lambda r: sorted(identity(f) for f in r['vo'].values()
                                        if f[KIND] == 2 and f[SUSTAIN] == 1 and f[AGE] > k)
                assert held(p) == held(o), (what, f'row {k}', 'the sustained voices differ', held(p), held(o))
            steps = lambda rows: sum(1 for k, r in enumerate(rows) for f in r['vo'].values()
                                     if f[KIND] == 2 and f[AGE] == 0 and f[15] == 1 and f[16] == 0 and k)
            out.append(f'{w["beat"]} f0..f{n - 1}: the looped services and the sustained voices row for row '
                       f'({len(set(orig[0]["b70"]))} looped id(s); footstep surface key-ons {steps(port)} in the '
                       f'port, {steps(orig)} in the capture)')
        else:
            raise AssertionError((what, 'unknown audio window mode', w['mode']))
    return '; '.join(out)


def check_opening(ticks, P, O, ids, what):
    """The opening, from the AREA11 start (the capture's beat segment) to
    first control + 60: the stream cues' changes in order; the lane-0 cue
    back to the area's on the frame whose camera leaves the opening's top
    mode 3, in both; no sound keyed but the fans'. The stream request's
    distance to the timeline's start is the disc's (host speed, or the PS2
    drive with the switch on: IOP_STREAM.md): reported."""
    first = next(k for k, r in enumerate(O) if r['route']['cam_mode'][:2] == '03')
    o_end = max(k for k, r in enumerate(O) if r['route']['cam_mode'][:2] == '03')
    o_start = next(k for k in range(first, 0, -1) if O[k]['cue'][0] == 25 and O[k - 1]['cue'][0] != 25)
    cams = [t.get('cam4', '00')[:2] for t in ticks]
    p_first = cams.index('03')
    p_end = max(i for i in range(p_first, len(cams)) if cams[i] == '03' and i < p_first + 3000)
    p_start = next(i for i in range(p_first, 0, -1) if P[i] and P[i - 1] and P[i]['cue'][0] == 25
                   and P[i - 1]['cue'][0] != 25)
    # The capture's row k is the loop top after its frame k (its camera
    # mode too); the port's line i the loop top before frame i, its cam4
    # the frame's own: the frame leaving mode 3 is o_end + 1 / p_end + 1.
    assert O[o_end + 1]['cue'][0] == 25 and O[o_end]['cue'][0] != 25, (what, 'capture: the cue at the end', o_end)
    assert P[p_end + 2]['cue'][0] == 25 and P[p_end + 1]['cue'][0] != 25, \
        (what, 'the lane-0 cue does not return on the frame the opening\'s camera ends', p_end,
         [P[i]['cue'] for i in range(p_end - 1, p_end + 4)])
    n_o, n_p = o_end + 61 - o_start, p_end + 62 - p_start
    ps, pc = sequence(P, p_start, p_start + n_p, ids)
    os_, oc = sequence(O, o_start, o_start + n_o, ids)
    assert pc == oc, (what, 'the stream cues\' sequence differs', pc, oc)
    assert [s[1] for s in ps] == [s[1] for s in os_], (what, 'sounds keyed in the opening', ps[:6], os_[:6])
    req_o = next(k for k in range(o_start, o_end) if O[k]['cue'][0] not in (0, 25))
    req_p = next(i for i in range(p_start, p_end) if P[i]['cue'][0] not in (0, 25))
    return (f'opening: the cues {" -> ".join(str(c[0]) for c in oc)} in order, the area cue back on the frame the '
            f'camera leaves mode 3 (capture f{o_end + 1}, port line {p_end + 2}); the timeline\'s mode 3 '
            f'{o_end - first + 1} rows in the capture, {p_end - p_first + 1} in the port; the stream request '
            f'{first - req_o} rows before it in the capture, {p_first - req_p} in the port; '
            f'{len(os_)} sounds keyed besides the fans\' in both')
