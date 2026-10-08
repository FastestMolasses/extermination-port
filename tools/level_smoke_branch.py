"""level_smoke_branch.py - the BRANCH side runs against the recordings.

The level smoke's side phases br_* (src/game/em_level_smoke_test.c
"branches"; LEVEL_SMOKE.md "The BRANCH side runs"; audit 1b item 16) play the
decomp capture lane BRANCH's closed-loop policies (route_capture.py
br_beat_*) on the port. A run and a recording start from different places
and their policies react to their own states (the walks, the stance at a
press, how many presses start nothing are navigation input), so each beat is
compared window by window, each window aligned on its event and then
compared row for row with the recording (decomp docs/CAPTURES_C10.md
"BRANCH"; build/c10/branch/<beat>/trace.json):

  a take        (br_00, 01, 07, 09, 10, 12) aligned on the use scan
                (0x70003B8D leaves 0): the selector, camera byte, letterbox,
                message, power and D_008106B0/B1, +5 / +1F0 / +1F1 and,
                from the take clip on, the clip and its clock row for row
                to the take's request (B0 != 0), which comes on the same row,
                or (the camera target's settle from a different stance, as
                route 01's battery) two rows after the settle's last target
                change in both, the settle ending on the item; aligned on the
                request: the same fields and the status block D_00810130
                (0x60 bytes) row for row to the page module's load (the busy
                byte D_00275BD8 set on the same row); the load at host speed
                no longer than the recording's (the page modules load at
                host speed under either drive mode); aligned on its end: the same row
                for row to the status close; aligned on the close: the fields, the
                taken bits of area 11, the six item counts and the item's
                record header (+0x00, +0x02..+0x0F; +0x01 is the camera's
                001B1630 visibility) row for row through control and
                AFTER_CONTROL rows;
  a ladder      (br_05 up, br_08 down, br_09's ladder A, br_11 up, br_13
                down) aligned on the grab (+1F0 0x15 / 0x16) through the
                hand-back and LADDER_AFTER rows: +5, +1F0, +1F1, clip,
                clock, ground and heading exactly; X/Z exactly; Y as the lift
                from the grab row on the ladder and exactly after it (as
                check_cage_ladders);
  a ledge climb (br_05's corridor box, br_11's raised pipe) aligned on +5 = 2
                (as check_climbs), and the step-off after it (check_fall);
  a box break   (br_04, br_06) aligned on the hit (the box's +0x36 set): the
                swing that hit from its +5 = 0x21 row through the hand-back
                (+5, +1F0, +1F1, clip, clock, ground), the hit on the same
                row of it, and every box r3..r6 (+0x00, +0x04, +0x05, +0x07,
                +0x0A, +0x36, +0xB0..+0xB8, +0xC0..+0xC8) row for row over
                BREAK_ROWS rows (r3's fall and break in br_04);
  the ride up   (br_02) aligned on the scan: as check_elevator (the window
                to the release and AFTER_RELEASE rows, the terminal record,
                the follow camera), D_0081083A's toggle on the same row;
  the decline   (br_03) aligned on the scan: the panel's script and request
                row for row to the page load, the load as a take's, then the
                prompt row for row to the Cross on No; aligned on the
                prompt's answer (the page back on its list): to the
                Triangle; aligned on the close: the cancel script 0x247DA0
                to the release and AFTER_RELEASE rows, power 0;
  Roger's talk  (br_14) aligned on the scan: compare_window (selector,
                camera byte, letterbox, message, power, placement, heading,
                the script's camera) to the release and AFTER_RELEASE rows,
                with Roger's +0x05 / +0x0B and script pc and the player's
                +5 / +1F0 / clip / clock row for row; the voice stream's
                read at host speed is shorter than the recording's, so the
                teardown and the rows after it are compared shifted by that
                difference.

The runs press their pads two ticks after deciding (em_level_smoke_test.c
br_frame), as the recordings' pads took effect three rows after their row
and the port's one. A ladder whose recording lost the stick for one row
(br_11) is aligned again on its dismount.

The run log's "beat <name> at tick N" lines slice the run into its beats.
"""
from __future__ import annotations

import json
import re
import struct
from pathlib import Path

DECOMP = Path(__file__).resolve().parents[2] / 'Extermination'
CAPTURES = DECOMP / 'build/c10/branch'
ITEMS = {'g0.1': 0x7A5930, 'g0.2': 0x7A5C20, 'g0.3': 0x7A5F10, 'g0.4': 0x7A6200, 'g0.5': 0x7A64F0,
         'g0.6': 0x7A67E0}
BOXES = {'r3': 0x7A7980, 'r4': 0x7A7C70, 'r5': 0x7A7F60, 'r6': 0x7A8250}
INV_TYPES = ('0x8', '0x10', '0x1b', '0x1e', '0x1f', '0x32')   # the tick log's br[1] order
AFTER_CONTROL = 30         # rows compared after control returns (route_capture settle 30)
LADDER_AFTER = 2
BREAK_ROWS = 200
LADDER_MODES = (0x15, 0x16, 0x17, 0x18)


def capture(beat):
    path = CAPTURES / beat / 'trace.json'
    assert path.exists(), f'BRANCH capture missing: {path} (decomp docs/CAPTURES_C10.md "BRANCH")'
    return json.loads(path.read_text())['rows']


def f32(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def beat_slices(ticks, run, side):
    """{beat: (first tick index, end index)} from the run log's beat lines."""
    marks = [(m.group(1), int(m.group(2))) for m in
             re.finditer(rf'^level smoke: {side}: beat (\S+) at tick (\d+)', run, re.M)]
    assert marks, (side, 'no beat lines in the run log')
    index = {t['tick']: i for i, t in enumerate(ticks)}
    out = {}
    for n, (beat, tick) in enumerate(marks):
        lo = index[tick]
        hi = index[marks[n + 1][1]] if n + 1 < len(marks) else len(ticks) - 1
        out[beat] = (lo, hi)
    return out


# ------------------------------------------------------------- the views

def ui_port(tick):
    rec = tick.get('aimrec')
    assert rec and rec[3], ('the tick log has no status block (EM_LOG_AIM_RECORDS=1)', tick['tick'])
    return rec[3]


def br_port(tick):
    taken, inv, recs = tick['br']
    return taken, inv, {r[0]: (bytes.fromhex(r[1]), bytes.fromhex(r[2])) for r in recs}


def br_orig(row):
    taken = int.from_bytes(bytes.fromhex(row['taken11'])[:2], 'little')
    inv = [row['inv_br'][k] for k in INV_TYPES]
    recs = {ITEMS[n]: (bytes.fromhex(v['a']), struct.pack('<3f', *v['B0'])) for n, v in row['items'].items()}
    for n, v in row['boxes'].items():
        if n in BOXES:
            a = bytes.fromhex(v['a'])
            recs[BOXES[n]] = (a, a[0xB0:0xD0])
    return taken, inv, recs


def ui_equal(port_hex, row, where):
    """The status block D_00810130 against the row's. The map item's page
    (its screen word 02 02) holds the player's map marker at UI+0x40 / +0x48,
    from the player's place (navigation): those two words are left out."""
    p, o = bytes.fromhex(port_hex), bytes.fromhex(row['ui_rec'])
    if o[2:4] == b'\x02\x02':
        p = p[:0x40] + o[0x40:0x44] + p[0x44:0x48] + o[0x48:0x4C] + p[0x4C:]
    assert p == o, (where, f'f{row["f"]}', 'status block', p.hex(), o.hex())


def sel_port(T, ticks, i):
    return T.selector(T.port_view(ticks, i)['spad'])


def sel_orig(T, row):
    return T.selector(row['spad'])


def fields_equal(T, ticks, i, row, what, req=True, msg=True, keys=('spad', 'cam', 'screen', 'power')):
    p, o = T.port_view(ticks, i), T.orig_view(row)
    where = f'{what} row f{row["f"]} (port tick {ticks[i]["tick"]})'
    for key in keys + (('req',) if req else ()):
        assert p[key] == o[key], (where, key, p[key], o[key])
    if msg and o['msg'][0] != 4 and p['msg'] is not None:
        assert p['msg'] == o['msg'], (where, 'message block', p['msg'], o['msg'])


def player_equal(T, tick, row, k, what, yaw=True, clock=True, m1=True):
    got, want = T.record_row(tick, row, k if clock else 0)
    got.pop('ground'), want.pop('ground')
    if not m1:
        got.pop('m1F1'), want.pop('m1F1')
    if not yaw:
        got.pop('yaw'), want.pop('yaw')
    assert got == want, (what, f'row f{row["f"]}', 'player', got, want)


def first(seq, pred, what):
    for k in seq:
        if pred(k):
            return k
    raise AssertionError(('not found', what))


# ------------------------------------------------------------- a take

def busy_port(T, ticks, i):
    """D_00275BD8, the page core's module-busy byte, after port tick i's
    frame (the next tick's loader_pre)."""
    return T.loader_after(ticks, i)[25]


def page_load(T, ticks, ip, hi, rows, fp, what, state):
    """A status page's module load after a request (port tick ip, row fp):
    the busy byte D_00275BD8 set on the same row in both; its length is the
    loader's at host speed (MODULE_LOADER.md: no longer than the
    recording's; with the PS2 disc-drive timing switch the recording's).
    Returns (load start offset, port length, recording length)."""
    load_p = first(range(hi - ip), lambda k: busy_port(T, ticks, ip + k) != 0, f'{what}: the port\'s page load')
    load_o = first(range(len(rows) - fp), lambda k: rows[fp + k]['bd8'] != 0, f'{what}: the page load')
    assert load_p == load_o, (what, 'the page load after the request', load_p, load_o)
    len_p = first(range(1, hi - ip - load_p), lambda k: busy_port(T, ticks, ip + load_p + k) == 0,
                  f'{what}: the port\'s load end')
    len_o = first(range(1, len(rows) - fp - load_o), lambda k: rows[fp + load_o + k]['bd8'] == 0,
                  f'{what}: the load end')
    if state['drive'] == 'ps2':
        assert len_p == len_o, (what, 'the page load with the PS2 disc-drive timing', len_p, len_o)
    else:
        assert len_p <= len_o, (what, 'the page load at host speed takes longer than the recording\'s', len_p, len_o)
    return load_p, len_p, len_o


def check_take(T, ticks, lo, hi, rows, item, beat, state):
    rec = ITEMS[item]
    i0 = first(range(lo, hi), lambda i: sel_port(T, ticks, i) != '00', f'{beat}: the port\'s use scan')
    f0 = first(range(1, len(rows)), lambda k: sel_orig(T, rows[k]) != '00' and sel_orig(T, rows[k - 1]) == '00',
               f'{beat}: the use scan')
    post_p = first(range(hi - i0), lambda k: T.port_view(ticks, i0 + k)['req'][:2] != '00', f'{beat}: port request')
    post_o = first(range(len(rows) - f0), lambda k: T.orig_view(rows[f0 + k])['req'][:2] != '00',
                   f'{beat}: request')
    assert T.port_view(ticks, i0 + post_p)['req'] == T.orig_view(rows[f0 + post_o])['req'], \
        (beat, 'the take\'s request', T.port_view(ticks, i0 + post_p)['req'], T.orig_view(rows[f0 + post_o])['req'])
    # The turn toward the item (001B7F90's bounded steps from the stance's
    # heading, navigation) may take a different number of rows: the take's
    # clip and everything the script does after the turn are aligned on the
    # clip's start.
    clip0_p, clip0_o = ticks[i0]['player'][3], rows[f0]['clip']
    c_p = first(range(1, post_p), lambda k: ticks[i0 + k]['player'][3] != clip0_p, f'{beat}: the port\'s take clip')
    c_o = first(range(1, post_o), lambda k: rows[f0 + k]['clip'] != clip0_o, f'{beat}: the take clip')
    # +1F1 is the stance's (the face taps before the press are navigation):
    # neither the take nor the release writes it, so it is compared only
    # where it was equal at the scan.
    m1 = ticks[i0]['player'][2] == rows[f0]['m1F1']
    for k in range(min(c_p, c_o)):
        where = f'{beat} turn'
        fields_equal(T, ticks, i0 + k, rows[f0 + k], where)
        player_equal(T, ticks[i0 + k], rows[f0 + k], k, where, yaw=False, clock=False, m1=m1 and k > 0)
    note = 'on the same row'
    if post_p - c_p != post_o - c_o:
        # Route 01's rule: the camera target's settle from the stance (the
        # stance is navigation input; where it settles follows the stance
        # too, the map item's take) ends the same number of rows before the
        # request in both (two in route 01's battery).
        def settle_end(target, posted):
            return max(k for k in range(1, posted) if target(k) != target(k - 1))
        end_p = settle_end(lambda k: T.port_view(ticks, i0 + k)['tgt'], post_p)
        end_o = settle_end(lambda k: T.orig_view(rows[f0 + k])['tgt'], post_o)
        assert post_p - end_p == post_o - end_o, (beat, 'the request after the camera settle', post_p, end_p,
                                                   post_o, end_o)
        note = (f'{post_p - post_o:+d} row(s) from the camera settle from the stance, {post_o - end_o} rows after '
                f'it in both')
    elif c_p != c_o:
        note = f'{c_p - c_o:+d} row(s) from the turn from the stance\'s heading'
    for j in range(min(post_p - c_p, post_o - c_o)):
        where = f'{beat} take'
        fields_equal(T, ticks, i0 + c_p + j, rows[f0 + c_o + j], where)
        player_equal(T, ticks[i0 + c_p + j], rows[f0 + c_o + j], j, where, yaw=False, clock=j > 0, m1=m1)
    # The take's takeover is the stage's own (audit 1b item 8): its program
    # requests the grab clip through the record's +1F2 (001B9A00 sub 0) and
    # 00183090 commits it; the recordings show the request on the commit
    # row. +1F2 row for row from the admission (00182D70 sets it to +20C)
    # through the turn, on the row before the commit and through the take;
    # +4 = 4 from the admission to 00182DF0.
    req = [(i0 + k, f0 + k) for k in range(1, min(c_p, c_o) - 1)] + [(i0 + c_p - 1, f0 + c_o - 1)] + \
          [(i0 + c_p + j, f0 + c_o + j) for j in range(min(post_p - c_p, post_o - c_o))]
    for i, f in req:
        assert ticks[i]['player'][8] == rows[f]['req1F2'], (beat, f'row f{rows[f]["f"]}', 'player +1F2',
                                                             ticks[i]['player'][8], rows[f]['req1F2'])
    assert rows[f0 + c_o]['req1F2'] == rows[f0 + c_o]['clip'], (beat, 'the request on the commit row', c_o)
    admit, free = T.check_stage_takeover(ticks, i0, T.port_release(ticks, i0, beat), beat)
    # From the request to the status close.
    ip, fp = i0 + post_p, f0 + post_o
    load, len_p, len_o = page_load(T, ticks, ip, hi, rows, fp, beat, state)
    for k in range(load):
        where = f'{beat} request'
        fields_equal(T, ticks, ip + k, rows[fp + k], where)
        ui_equal(ui_port(ticks[ip + k]), rows[fp + k], where)
    il, fl = ip + load, fp + load
    ie, fe = il + len_p, fl + len_o
    close_p = first(range(hi - ie), lambda k: ui_port(ticks[ie + k])[2:4] != '03', f'{beat}: port status close')
    close_o = first(range(len(rows) - fe), lambda k: rows[fe + k]['ui_rec'][2:4] != '03', f'{beat}: status close')
    # The Triangle (navigation input: route_capture br_status_exit's rule in
    # both) reaches the original two frames after its row, the port's
    # overlay one: the close comes at most one row apart.
    assert abs(close_p - close_o) <= 1, (beat, 'the status close', close_p, close_o)
    for k in range(min(close_p, close_o)):
        where = f'{beat} status page'
        fields_equal(T, ticks, ie + k, rows[fe + k], where)
        ui_equal(ui_port(ticks[ie + k]), rows[fe + k], where)
        tp, ip_, _ = br_port(ticks[ie + k])
        to, io_, _ = br_orig(rows[fe + k])
        assert (tp, ip_) == (to, io_), (where, f'f{rows[fe + k]["f"]}', 'taken bits / item counts', tp, ip_, to, io_)
    # From the close through control.
    ic, fc = ie + close_p, fe + close_o
    ctl_o = first(range(len(rows) - fc), lambda k: sel_orig(T, rows[fc + k]) == '00' and rows[fc + k]['m1F0'] == 0,
                  f'{beat}: control')
    count = min(ctl_o + AFTER_CONTROL, len(rows) - fc)
    for k in range(count):
        where = f'{beat} close'
        assert ic + k < hi, (where, 'the beat ends before the window', k)
        fields_equal(T, ticks, ic + k, rows[fc + k], where)
        player_equal(T, ticks[ic + k], rows[fc + k], k, where, yaw=False, clock=k > ctl_o, m1=m1)
        tp, invp, rp = br_port(ticks[ic + k])
        to, invo, ro = br_orig(rows[fc + k])
        assert (tp, invp) == (to, invo), (where, f'f{rows[fc + k]["f"]}', 'taken bits / item counts', tp, invp, to,
                                          invo)
        hp, ho = rp[rec][0][:16], ro[rec][0][:16]
        assert hp[:1] + hp[2:] == ho[:1] + ho[2:], (where, f'f{rows[fc + k]["f"]}', f'{item} header', hp.hex(),
                                                    ho.hex())
        assert ticks[ic + k]['player'][8] == rows[fc + k]['req1F2'], (where, f'f{rows[fc + k]["f"]}', 'player +1F2',
                                                                       ticks[ic + k]['player'][8],
                                                                       rows[fc + k]['req1F2'])
    taken = next(k for k in range(count) if br_orig(rows[fc + k])[0] != br_orig(rows[fc - 1])[0])
    return (f'{item}: the scan port tick {ticks[i0]["tick"]} = f{rows[f0]["f"]}, the request '
            f'{T.orig_view(rows[f0 + post_o])["req"]} at f{rows[fp]["f"]} ({note}), the page row for row with its '
            f'module load {len_p} rows (the recording\'s {len_o}), the close at f{rows[fc]["f"]} '
            f'({close_p - close_o:+d} row: the Triangle\'s pad timing), the taken bit {taken} rows after it and '
            f'control at f{rows[fc + ctl_o]["f"]}, as recorded; the stage\'s own takeover: +4 = 4 from port tick '
            f'{admit} to 00182DF0 at {free}, the grab clip {rows[f0 + c_o]["clip"]:#x} requested through +1F2 '
            f'(shown on its commit row) in both')


# ------------------------------------------------------------- climbs

def ladder(T, ticks, lo, hi, rows, beat, start_f=0):
    """The ladder from the grab through the hand-back and LADDER_AFTER rows
    (check_cage_ladders' rule). A recording whose processed pad word lost
    the held stick for a row during the climb (a pad pickup glitch of the
    capture: D_00810E70 0 for one row while the stick stayed held, as the
    BRANCH replays note) steps the climb half a step on that row: the
    window is compared row for row up to that row, then aligned again on
    the dismount (+1F0 0x18) through the hand-back."""
    e = first(range(lo, hi), lambda i: ticks[i]['player'][1] in (0x15, 0x16), f'{beat}: the port\'s ladder grab')
    r0 = first(range(start_f, len(rows)), lambda k: rows[k]['m1F0'] in (0x15, 0x16), f'{beat}: the ladder grab')
    back = first(range(r0 + 1, len(rows)), lambda k: rows[k]['m1F0'] not in LADDER_MODES, f'{beat}: hand-back')
    count = back - r0 + 1 + LADDER_AFTER
    climb = first(range(r0, back), lambda k: rows[k]['m1F0'] == 0x17, f'{beat}: the climb')
    glitch = next((k for k in range(climb + 1, back) if rows[k]['m1F0'] == 0x17 and int(rows[k]['held'], 16) == 0
                   and int(rows[k - 1]['held'], 16) != 0), None)
    top_o = first(range(climb, back), lambda k: rows[k]['m1F0'] == 0x18, f'{beat}: the dismount')
    top_p = first(range(e, hi), lambda i: ticks[i]['player'][1] == 0x18, f'{beat}: the port\'s dismount')
    pe, oe = f32(ticks[e]['pos_post'][1]), rows[r0]['pos'][1]
    worst = 0.0
    pairs = [(e + k, r0 + k, k) for k in range(count if glitch is None else glitch - r0 + 1)]
    if glitch is not None:
        assert ticks[top_p - (top_o - glitch - 1)]['player'][1] == 0x17, (beat, 'the dismount after the glitch')
        pairs += [(top_p + j, top_o + j, 1 + j) for j in range(back - top_o + 1 + LADDER_AFTER)]
    for i, k, n in pairs:
        t, r = ticks[i], rows[k]
        assert i < hi, (beat, 'the beat ends inside the ladder window')
        got, want = T.record_row(t, r, n)
        assert got == want, (beat, 'ladder row', r['f'], k - r0, got, want)
        pos = [f32(v) for v in t['pos_post']]
        for a in (0, 2):
            assert round(pos[a], 5) == r['pos'][a], (beat, 'ladder X/Z', r['f'], k - r0, pos, r['pos'])
        if r['m1F0'] in LADDER_MODES:
            if glitch is None or k <= glitch:
                lift = abs((pos[1] - pe) - (r['pos'][1] - oe))
                worst = max(worst, lift)
                assert lift < 1e-4, (beat, 'ladder lift', r['f'], k - r0, pos[1] - pe, r['pos'][1] - oe)
        else:
            assert round(pos[1], 5) == r['pos'][1], (beat, 'Y after the hand-back', r['f'], pos[1], r['pos'][1])
    grab = rows[r0]['m1F0']
    note = '' if glitch is None else (f'; the recording\'s pad lost the stick at f{rows[glitch]["f"]}, aligned again '
                                      f'on the dismount f{rows[top_o]["f"]}')
    return e + count, r0 + count, (f'the ladder f{rows[r0]["f"]}..f{rows[r0 + count - 1]["f"]} (grab {grab:#x}, '
                                   f'climb 0x17 f{rows[climb]["f"]}, hand-back f{rows[back]["f"]} on y '
                                   f'{rows[back]["pos"][1]}; lift within {worst:.1e}{note})')


def ledge_climb(T, ticks, lo, hi, rows, beat):
    e = first(range(lo + 1, hi), lambda i: ticks[i]['player'][0] == 2 and ticks[i - 1]['player'][0] != 2,
              f'{beat}: the port\'s ledge climb')
    r0 = first(range(1, len(rows)), lambda k: rows[k]['p5'] == 2 and rows[k - 1]['p5'] != 2, f'{beat}: ledge climb')
    land = first(range(r0 + 1, len(rows)), lambda k: rows[k]['p5'] != 2, f'{beat}: the climb\'s landing')
    count = land - r0 + 1 + LADDER_AFTER
    pe = [f32(v) for v in ticks[e]['pos_post']]
    oe = rows[r0]['pos']
    stance = ((pe[0] - oe[0]) ** 2 + (pe[2] - oe[2]) ** 2) ** 0.5
    worst = 0.0
    for k in range(count):
        t, r = ticks[e + k], rows[r0 + k]
        got, want = T.record_row(t, r, k)
        got.pop('yaw'), want.pop('yaw')
        assert got == want, (beat, 'climb row', r['f'], k, got, want)
        pos = [f32(v) for v in t['pos_post']]
        # Y as the lift from the entry row: the ledge's top under the
        # stance (the raised pipe's slope, navigation) may differ by the
        # stance offset's height.
        lift = abs((pos[1] - pe[1]) - (r['pos'][1] - oe[1]))
        assert lift < 1e-4, (beat, 'climb lift', r['f'], k, pos[1] - pe[1], r['pos'][1] - oe[1])
        d = (((pos[0] - pe[0]) - (r['pos'][0] - oe[0])) ** 2 + ((pos[2] - pe[2]) - (r['pos'][2] - oe[2])) ** 2) ** 0.5
        worst = max(worst, d)
    assert worst <= stance + 1e-3, (beat, 'climb X/Z displacement', worst, stance)
    fall = step_off(T, ticks, first(range(e + count, hi), lambda i: ticks[i]['player'][0] == 5,
                                    f'{beat}: the port\'s step-off'),
                    rows, first(range(r0 + count, len(rows)), lambda k: rows[k]['p5'] == 5, f'{beat}: step-off'), beat)
    return (f'the ledge climb f{rows[r0]["f"]}..f{rows[r0 + count - 1]["f"]} (stance {stance:.3f} from the '
            f'original\'s, X/Z residual {worst:.4f}) and its step-off: {fall}')


def step_off(T, ticks, e, rows, r0, what):
    """check_fall's rule for a step-off whose landing hands back to the idle
    state (the run-off stops the stick at the edge's far side): from the
    entry +5, +1F0, +1F1, clip and clock (once the fall's clip replaced the
    walk's) row for row and the Y as the drop from the entry; aligned on the
    landing (+5 = 8) through the hand-back (+5 0 or 1) and the row after it
    the same fields row for row (the run-off's stick, navigation input,
    resumes the walk after that, the recording's two pad frames after its
    row, the port's one). The landing on the same row, or one row apart
    when the edge heights differ (navigation input)."""
    land_p = next(k for k in range(1, 200) if ticks[e + k]['player'][0] == 8)
    land_o = next(k for k in range(1, 200) if rows[r0 + k]['p5'] == 8)
    ye_p, ye_o = f32(ticks[e]['pos_post'][1]), rows[r0]['pos'][1]
    same_height = abs(ye_p - ye_o) < 1e-3
    assert land_p == land_o or (not same_height and abs(land_p - land_o) == 1), \
        (what, 'step-off landing row', land_p, land_o, ye_p, ye_o)
    walk_clip = rows[r0]['clip']
    for k in range(1, min(land_p, land_o)):
        got, want = T.record_row(ticks[e + k], rows[r0 + k], k if rows[r0 + k]['clip'] != walk_clip else 0)
        got.pop('yaw'), want.pop('yaw')
        assert got == want, (what, 'step-off row', rows[r0 + k]['f'], k, got, want)
        drop = (f32(ticks[e + k]['pos_post'][1]) - ye_p) - (rows[r0 + k]['pos'][1] - ye_o)
        assert abs(drop) < 1e-4, (what, 'step-off drop', rows[r0 + k]['f'], k, drop)
    back = next(k for k in range(land_o, 200) if rows[r0 + k]['p5'] in (0, 1))
    for j in range(back - land_o + 2):
        got, want = T.record_row(ticks[e + land_p + j], rows[r0 + land_o + j], 1 if j or land_p == land_o else 0)
        got.pop('yaw'), want.pop('yaw')
        assert got == want, (what, 'landing row', rows[r0 + land_o + j]['f'], j, got, want)
    return (f'the step-off f{rows[r0]["f"]} (landing f{rows[r0 + land_o]["f"]}'
            f'{"" if land_p == land_o else ", one row apart from the edge heights"}, hand-back '
            f'f{rows[r0 + back]["f"]})')


# ------------------------------------------------------------- a box break

def box_view(rec):
    head, b0 = rec
    return (head[0], head[4], head[5], head[7], head[0xA], head[0x36:0x38].hex(),
            tuple(round(v, 4) for v in struct.unpack('<3f', b0[:12])),
            tuple(round(v, 4) for v in struct.unpack('<3f', b0[0x10:0x1C])) if len(b0) >= 0x1C else None)


def box_view_orig(row, name):
    a = bytes.fromhex(row['boxes'][name]['a'])
    return (a[0], a[4], a[5], a[7], a[0xA], a[0x36:0x38].hex(),
            tuple(round(v, 4) for v in struct.unpack_from('<3f', a, 0xB0)),
            tuple(round(v, 4) for v in struct.unpack_from('<3f', a, 0xC0)))


def check_break(T, ticks, lo, hi, rows, box, beat, state):
    rec = BOXES[box]
    hit_p = first(range(lo, hi), lambda i: br_port(ticks[i])[2][rec][0][0x36:0x38] != b'\0\0', f'{beat}: port hit')
    hit_o = first(range(len(rows)), lambda k: rows[k]['boxes'][box]['dmg36'] != 0, f'{beat}: hit')
    swing_p = max(i for i in range(lo + 1, hit_p + 1) if ticks[i]['player'][0] == 0x21
                  and ticks[i - 1]['player'][0] != 0x21)
    swing_o = max(k for k in range(1, hit_o + 1) if rows[k]['p5'] == 0x21 and rows[k - 1]['p5'] != 0x21)
    assert hit_p - swing_p == hit_o - swing_o, (beat, 'the hit inside the swing', hit_p - swing_p, hit_o - swing_o)
    # The knife's swing from here on (the whole-run checks' aim side-run
    # rule: the trail's and the debris' page draws, the eases).
    state['aim_from'] = min(state.get('aim_from', swing_p), swing_p)
    back = first(range(hit_o, len(rows)), lambda k: rows[k]['p5'] == 0, f'{beat}: the swing\'s hand-back')
    for k in range(back - swing_o + 1 + LADDER_AFTER):
        player_equal(T, ticks[swing_p + k], rows[swing_o + k], k, f'{beat} swing', yaw=False)
    count = min(BREAK_ROWS, len(rows) - hit_o + 1)
    for k in range(-1, count - 1):
        _t, _i, recs = br_port(ticks[hit_p + k])
        for name, address in BOXES.items():
            got, want = box_view(recs[address]), box_view_orig(rows[hit_o + k], name)
            assert got == want, (beat, f'row f{rows[hit_o + k]["f"]}', f'box {name}', got, want)
    states = [(rows[hit_o + k]['f'], name, box_view_orig(rows[hit_o + k], name)[1]) for k in range(1, count - 1)
              for name in BOXES if box_view_orig(rows[hit_o + k], name)[1] != box_view_orig(rows[hit_o + k - 1], name)[1]]
    return (f'box {box} hit at f{rows[hit_o]["f"]} ({hit_o - swing_o} rows into the swing in both), the swing '
            f'row for row to its hand-back f{rows[back]["f"]}; the boxes r3..r6 row for row over '
            f'{count} rows (state changes {", ".join(f"f{f} {n} -> {s}" for f, n, s in states)})')


# ------------------------------------------------------------- the beats

def scan_rows(T, ticks, lo, hi, rows, beat, skip=0):
    i0 = first(range(lo + 1, hi), lambda i: sel_port(T, ticks, i) != '00' and sel_port(T, ticks, i - 1) == '00',
               f'{beat}: the port\'s scan')
    f0 = first(range(max(1, skip), len(rows)),
               lambda k: sel_orig(T, rows[k]) != '00' and sel_orig(T, rows[k - 1]) == '00', f'{beat}: the scan')
    return i0, f0


def check_elevator_up(T, ticks, lo, hi, rows, beat, state):
    i0, f0 = scan_rows(T, ticks, lo, hi, rows, beat)
    r = T.release_row(rows, f0)
    count = r - f0 + T.AFTER_RELEASE
    placed, faced = T.compare_window(ticks, i0, rows, f0, count, beat)
    tog_p = first(range(count), lambda k: ticks[i0 + k]['floor'] != ticks[i0]['floor'], f'{beat}: port toggle')
    tog_o = first(range(count), lambda k: rows[f0 + k]['elev']['down83A'] != rows[f0]['elev']['down83A'],
                  f'{beat}: toggle')
    assert tog_p == tog_o and ticks[i0 + tog_p]['floor'] == 0, (beat, 'D_0081083A 1 -> 0', tog_p, tog_o)
    for k in range(count):
        t, row = ticks[i0 + k], rows[f0 + k]
        term = t.get('terminal')
        assert term, (beat, 'port tick', t['tick'], 'no terminal record in the tick log')
        want = [round(v, 5) for v in row['elevator_r19']['pos']]
        got = [round(f32(b), 5) for b in term[7]]
        assert got == want and term[1] == bytes.fromhex(row['elevator_r19']['h'])[4], \
            (beat, f'row f{row["f"]}', 'terminal +0x04 / +0xB0', term[1], got, want)
    follow = T.check_follow_after_release(ticks, i0, rows, f0, beat, 'exact')
    # The terminal's takeover is the stage's own (audit 1b item 8).
    commit = T.check_takeover_record(ticks, i0, rows, f0, count, beat)
    assert commit is not None and rows[f0 + commit]['clip'] == 0x47, (beat, 'the lever clip', commit)
    admit, free = T.check_stage_takeover(ticks, i0, count, beat)
    # check_indicator_children: the terminal is above again after this window.
    state['ride_up_scan'], state['ride_up_end'] = i0, i0 + count
    return (f'the scan port tick {ticks[i0]["tick"]} = f{rows[f0]["f"]}: the powered script 0x82A750 and the '
            f'carry up row for row to the release f{rows[r]["f"]} and {T.AFTER_RELEASE} rows after (spad, camera '
            f'byte, letterbox, message, power, placement, heading, the script\'s camera, the terminal\'s +0x04 / '
            f'+0xB0), D_0081083A 1 -> 0 at f{rows[f0 + tog_o]["f"]} in both; the stage\'s own takeover: +4 = 4 '
            f'from port tick {admit} to 00182DF0 at {free}, +1F2 / +20C row for row (clip 0x47 committed at '
            f'f{rows[f0 + commit]["f"]} with its clock); {follow}')


def check_panel_decline(T, ticks, lo, hi, rows, beat, state):
    i0, f0 = scan_rows(T, ticks, lo, hi, rows, beat)
    post = first(range(len(rows) - f0), lambda k: T.orig_view(rows[f0 + k])['req'] == '0182', f'{beat}: request')
    post_p = first(range(hi - i0), lambda k: T.port_view(ticks, i0 + k)['req'] == '0182', f'{beat}: port request')
    assert post == post_p, (beat, 'the BATTERY request', post_p, post)
    load, len_p, len_o = page_load(T, ticks, i0 + post, hi, rows, f0 + post, beat, state)
    load += post
    T.compare_window(ticks, i0, rows, f0, post, f'{beat} open', y_mode='retained')
    T.compare_window(ticks, i0 + post, rows, f0 + post, load - post, f'{beat} request', y_mode='retained', req=True)
    il, fl = i0 + load, f0 + load
    ie, fe = il + len_p, fl + len_o

    def prompt(ui):
        return ui[8:12] == '0504'
    ans_p = first(range(hi - ie), lambda k: k and prompt(ui_port(ticks[ie + k - 1])) and
                  not prompt(ui_port(ticks[ie + k])), f'{beat}: port answer')
    ans_o = first(range(len(rows) - fe), lambda k: k and prompt(rows[fe + k - 1]['ui_rec']) and
                  not prompt(rows[fe + k]['ui_rec']), f'{beat}: answer')
    # The prompt to the Cross on No (route_capture: idle 30 after the prompt
    # shows, then Cross): row for row to the earlier answer.
    for k in range(min(ans_p, ans_o) - 1):
        fields_equal(T, ticks, ie + k, rows[fe + k], f'{beat} prompt')
        assert ui_port(ticks[ie + k]) == rows[fe + k]['ui_rec'], (beat, 'prompt status block', rows[fe + k]['f'])
    ia, fa = ie + ans_p, fe + ans_o
    close_p = first(range(hi - ia), lambda k: ui_port(ticks[ia + k])[2:4] != '03', f'{beat}: port close')
    close_o = first(range(len(rows) - fa), lambda k: rows[fa + k]['ui_rec'][2:4] != '03', f'{beat}: close')
    for k in range(min(close_p, close_o) - 1):
        fields_equal(T, ticks, ia + k, rows[fa + k], f'{beat} list')
        assert ui_port(ticks[ia + k]) == rows[fa + k]['ui_rec'], (beat, 'list status block', rows[fa + k]['f'],
                                                                  ui_port(ticks[ia + k]), rows[fa + k]['ui_rec'])
    ic, fc = ia + close_p, fa + close_o
    r = T.release_row(rows, fc)
    count = r - fc + T.AFTER_RELEASE
    T.compare_window(ticks, ic, rows, fc, count, f'{beat} cancel', y_mode='retained', req=True)
    assert all(T.port_view(ticks, ic + k)['power'] == 0 for k in range(count)), (beat, 'the power came on')
    # The panel's takeover is the stage's own (audit 1b item 8): +1F2 / +20C
    # row for row from the scan to the page load and from the close through
    # the release; +4 = 4 from the admission to 00182DF0.
    assert T.check_takeover_record(ticks, i0, rows, f0, load, f'{beat} open') is None
    T.check_takeover_record(ticks, ic - 1, rows, fc - 1, count + 1, f'{beat} cancel')
    admit, free = T.check_stage_takeover(ticks, i0, ic + count - i0, beat)
    return (f'the scan port tick {ticks[i0]["tick"]} = f{rows[f0]["f"]}: script 0x2477A0 and the request 01/82 '
            f'row for row, the page load {len_p} rows (the recording\'s {len_o}), the prompt to the Cross on No, '
            f'the list to the Triangle, and from the close f{rows[fc]["f"]} the cancel script 0x247DA0 to the '
            f'release f{rows[r]["f"]} and {T.AFTER_RELEASE} rows after, power 0; the stage\'s own takeover: '
            f'+4 = 4 from port tick {admit} to 00182DF0 at {free}, +1F2 / +20C row for row')


def roger_port(tick):
    r = tick.get('roger')
    if not r:
        return None
    head, block = bytes.fromhex(r[1]), bytes.fromhex(r[3])
    return head[5], head[0xB], block[8:12].hex()


def roger_orig(row):
    a, c = bytes.fromhex(row['roger']['a']), bytes.fromhex(row['roger']['c'])
    return a[5], a[0xB], c[8:12].hex()


def check_roger_talk(T, ticks, lo, hi, rows, beat, state):
    """Roger's talk 0x828810 aligned on the scan to the release and
    AFTER_RELEASE rows. Row for row: the selector, camera byte, letterbox,
    power, D_008106B0/B1 and the message block; Roger's +0x05 / +0x0B and
    script pc; the player's +5 / +1F0 / +1F1 / clip / clock; the player's
    place and the frozen camera's eye / target as their displacement from
    the scan row (the stance and the follow camera at the press are
    navigation); the heading's turn on the same row and its steps after it.
    The turn-in-place clip of the turn row (341 / 342) follows the turn's
    side, which the stance's heading decides (navigation): it may differ
    only with the side. The line 0x13's voice (VOICE.DAT cue 1): its read
    takes the host-speed read's rows (the PS2 disc-drive timing switch: the
    recording's), and the teardown and everything after it come exactly
    that many rows earlier than the recording's (s rows; as
    check_director_beat's teardown)."""
    i0, f0 = scan_rows(T, ticks, lo, hi, rows, beat)
    r = T.release_row(rows, f0)
    count_o = r - f0 + T.AFTER_RELEASE
    msg_o = lambda k: T.orig_view(rows[f0 + k])['msg']
    msg_p = lambda k: T.port_view(ticks, i0 + k)['msg']
    m0 = first(range(count_o), lambda k: msg_o(k)[0] != 0, f'{beat}: the line')
    assert first(range(count_o), lambda k: msg_p(k)[0] != 0, f'{beat}: the port\'s line') == m0, \
        (beat, 'the line starts off the recording\'s row')
    phase_o = lambda k: rows[f0 + k]['cd157']                  # D_00282157 after the frame
    phase_p = lambda k: ticks[i0 + k + 1]['stream'][1]          # the next tick's pre-frame sample
    issue_o = first(range(m0, count_o), lambda k: phase_o(k) == 2, f'{beat}: the voice read')
    issue_p = first(range(m0, count_o), lambda k: phase_p(k) == 2, f'{beat}: the port\'s voice read')
    assert issue_p == issue_o, (beat, 'the voice read\'s issue', issue_p, issue_o)
    read_o = first(range(issue_o, count_o), lambda k: phase_o(k) != 2, f'{beat}: read done') - issue_o
    read_p = first(range(issue_p, count_o), lambda k: phase_p(k) != 2, f'{beat}: port read done') - issue_p
    if state['drive'] == 'ps2':
        assert read_p == read_o, (beat, 'the voice read with the PS2 disc-drive timing', read_p, read_o)
    else:
        assert read_p == T.R.HOST_READ_ROWS, (beat, 'the voice read at host speed', read_p)
    shift = read_o - read_p
    td_o = first(range(m0, count_o), lambda k: msg_o(k)[1] == 2, f'{beat}: teardown')
    td_p = first(range(m0, count_o), lambda k: msg_p(k)[1] == 2, f'{beat}: port teardown')
    assert td_o - td_p == shift, (beat, 'the teardown against the voice read\'s shift', td_p, td_o, shift)
    turn_o = first(range(count_o), lambda k: rows[f0 + k]['yaw'] != rows[f0]['yaw'], f'{beat}: the turn')
    yaw_p = lambda k: round(f32(ticks[i0 + k]['yaw_post']), 5)
    turn_p = first(range(count_o), lambda k: yaw_p(k) != yaw_p(0), f'{beat}: the port\'s turn')
    assert turn_p == turn_o, (beat, 'the turn toward Roger', turn_p, turn_o)
    side_o = rows[f0 + turn_o]['yaw'] > rows[f0 + turn_o - 1]['yaw']
    side_p = yaw_p(turn_p) > yaw_p(turn_p - 1)
    p0, o0 = T.port_view(ticks, i0), T.orig_view(rows[f0])
    # The idle clip's phase at the press is navigation: clocks from the
    # script's first clip on.
    clip0 = first(range(count_o), lambda k: rows[f0 + k]['clip'] != rows[f0]['clip'], f'{beat}: the first clip')
    clip_td = first(range(td_o, count_o), lambda k: rows[f0 + k]['clip'] != rows[f0 + td_o]['clip'],
                    f'{beat}: the clip after the teardown')
    turn_clips = 0
    for k in range(count_o - shift):
        ko = k if k < td_p else k + shift
        t, row = ticks[i0 + k], rows[f0 + ko]
        where = f'{beat} row f{row["f"]} (port tick {t["tick"]})'
        fields_equal(T, ticks, i0 + k, row, where)
        assert roger_port(t) == roger_orig(row), (where, 'Roger +0x05 / +0x0B / script pc', roger_port(t),
                                                 roger_orig(row))
        # The clip running at the teardown (the talk loop) keeps its own
        # clock across the shift: clocks after the teardown from the next
        # clip on.
        clocked = ko > clip0 and (k < td_p or ko >= clip_td)
        got, want = T.record_row(t, row, ko if clocked else 0)
        got.pop('ground'), want.pop('ground'), got.pop('yaw'), want.pop('yaw')
        if got['clip'] != want['clip']:
            assert {got['clip'], want['clip']} == {341, 342} and side_p != side_o, \
                (where, 'a clip other than the turn\'s side', got, want)
            turn_clips += 1
            got.pop('clip'), want.pop('clip'), got.pop('clock', None), want.pop('clock', None)
        assert got == want, (where, 'player', got, want)
        p, o = T.port_view(ticks, i0 + k), T.orig_view(row)
        # The place, and the camera while the script holds it (after the
        # release the follow camera moves on from the stance's own state).
        for key in ('pos',) + (('eye', 'tgt') if T.selector(o['spad']) != '00' else ()):
            dp = [round(a - b, 4) for a, b in zip(p[key], p0[key])]
            do = [round(a - b, 4) for a, b in zip(o[key], o0[key])]
            assert dp == do, (where, key + ' displacement from the scan', dp, do)
        if k > turn_p and k != td_p:
            # Row k - 1 is the recording's row ko - 1 on either side of the
            # teardown (at the teardown row itself the two are s rows apart).
            step_p = yaw_p(k) - yaw_p(k - 1)
            step_o = row['yaw'] - rows[f0 + ko - 1]['yaw']
            assert abs(step_p - step_o) <= 2e-5, (where, 'the heading\'s step', step_p, step_o)
    return (f'the scan port tick {ticks[i0]["tick"]} = f{rows[f0]["f"]}: Roger\'s talk 0x828810 row for row '
            f'(selector, camera byte, letterbox, power, message 0x13, Roger\'s +0x05 / +0x0B / script pc, the '
            f'player\'s +5 / +1F0 / +1F1 / clip / clock, the place and the frozen camera from the scan, the turn '
            f'on f{rows[f0 + turn_o]["f"]} and its steps; the turn clip on {turn_clips} row(s) by the stance\'s '
            f'side); the voice read {read_p} row(s) (the recording\'s {read_o}), the teardown and the rest '
            f'{shift} rows earlier, to the release f{rows[r]["f"]} and {T.AFTER_RELEASE} rows after')


TAKES = {'br_00_ledge_ammo': 'g0.3', 'br_01_map_item': 'g0.6', 'br_07_ledge_magazine': 'g0.5',
         'br_09_cage_key': 'g0.4', 'br_10_yard_ammo': 'g0.1', 'br_12_tower_ammo': 'g0.2'}


def check_beat(T, ticks, lo, hi, beat, state):
    rows = capture(beat)
    notes = []
    if beat in ('br_05_west_ladder_up', 'br_11_plateau_ladder_up'):
        notes.append(ledge_climb(T, ticks, lo, hi, rows, beat))
    if beat in ('br_05_west_ladder_up', 'br_08_west_ladder_down', 'br_09_cage_key', 'br_11_plateau_ladder_up',
                'br_13_plateau_ladder_down'):
        notes.append(ladder(T, ticks, lo, hi, rows, beat)[2])
    if beat in TAKES:
        notes.append(check_take(T, ticks, lo, hi, rows, TAKES[beat], beat, state))
    if beat == 'br_04_crate_stack_break':
        notes.append(check_break(T, ticks, lo, hi, rows, 'r5', beat, state))
    if beat == 'br_06_ledge_crate_break':
        notes.append(check_break(T, ticks, lo, hi, rows, 'r6', beat, state))
    if beat == 'br_02_elevator_up':
        notes.append(check_elevator_up(T, ticks, lo, hi, rows, beat, state))
    if beat == 'br_03_panel_decline':
        notes.append(check_panel_decline(T, ticks, lo, hi, rows, beat, state))
    if beat == 'br_14_roger_talk':
        notes.append(check_roger_talk(T, ticks, lo, hi, rows, beat, state))
    assert notes, (beat, 'no check for this beat')
    return f'{beat}: ' + '; '.join(notes)


def check_side(T, ticks, run, state, side):
    """One BRANCH side phase: each beat of its run against its recording."""
    assert re.search(rf'^level smoke: {side}: PASS', run, re.M), (side, 'did not pass in process')
    slices = beat_slices(ticks, run, side)
    notes = [check_beat(T, ticks, lo, hi, beat, state) for beat, (lo, hi) in slices.items()]
    state['cursor'] = max(hi for _lo, hi in slices.values())
    print(f'{side}: PASS ({"; ".join(notes)})')
