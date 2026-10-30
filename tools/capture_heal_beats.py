#!/usr/bin/env python3
"""Record the HEALING page's menu uses on the ORIGINAL (the PCSX2 fork).

Three side beats, each from a recorded route beat's fork state, driven with
pad input only through the decomp's tools/route_capture.py (its Route,
samplers and walk helpers; hidden ForkSession under the run lock
build/.pcsx2.lock):

  a02_s1_menu_cure     from a01r_03_door16: walk to the bed [0] (00159620,
                       class 4 subtype 0x36) as a02_s0 does, then START, the
                       hub's ITEM, its HEALING, Cross on item 0x20: 00185420
                       (0x20) finds the bed (00184D20's class-4 0x36 arm), the
                       use prompt, Yes, the bed and the count-up (page 8).
  a01r_s2_menu_cure_none  from a01r_02_bridge (AREA01, item 0x20 held): the
                       same menu use; 00185420(0x20) finds no device: the
                       banner 0x19, then back out and START.
  a01_s8_fire_medicine from a01_s5_duct (item 0x1E held, health 100): out of
                       the control room, south to the fire east of the crates
                       (a01_s3's burn: health 95), START, ITEM, HEALING, Cross
                       on item 0x1E, Yes: 002160B0 state 5 counts D_00810858
                       up to 100 while the player record's +0x220 keeps 95
                       until the commit; back out, START.

Each beat is written like route_capture's (trace.json with the beat's
sampler rows, inputs and snapshot; state.p2s / eeMemory.bin) to
../Extermination/build/fork_refs/s87/route_<area>/<beat>/, with one neutral
lead-in frame after the source state (as the recorded side beats a02_s0 and
a01_s5 have), plus heal.json: per row D_00810858 / D_0081085C, the player
record's +0x220 / +0x228, the UI block D_00810130..6F, D_00810C82..86,
D_008106B0 and the message words D_002821B0 / B4 / B8. Disc-derived output,
never committed. The fork states in build/startup-reference are not touched.

Usage (port root, the decomp's venv):
    ../Extermination/.venv/bin/python tools/capture_heal_beats.py a02_s1_menu_cure ...
"""
import json
import math
import struct
import sys
from pathlib import Path

DECOMP = Path(__file__).resolve().parents[2] / 'Extermination'
sys.path.insert(0, str(DECOMP / 'tools'))
import route_capture as rc  # noqa: E402

LEAD_IN = 1
UI = 0x810130
STICK_LEFT = (0x00, 0x7F)
STICK_DOWN_LEFT = (0x25, 0xD9)       # (-0.7071, 0.7071) on the 0..255 axes


def wait(r, pred, limit, what):
    for _ in range(limit):
        if pred():
            return
        r.step(1)
    raise TimeoutError(f'{what}; last {rc.summary(r.rows[-1])}')


def page_core(r):
    t = r.s.read(UI, 0x14)
    return dict(phase=t[1], step=t[2], child=t[4], state=t[5], cursor=t[6], screen=t[0x10], hover=t[0x11])


def open_healing(r):
    """START, the hub's hover 4 (ITEM, stick left), Cross, the root's hover 5
    (HEALING, stick down-left), Cross: the list (t[5] = 1)."""
    r.press('START', 2)
    wait(r, lambda: (lambda c: c['phase'] == 1 and c['step'] == 1)(page_core(r)), 600, 'the hub did not open')
    r.idle(10)
    r.set_pad(0, *STICK_LEFT)
    wait(r, lambda: page_core(r)['hover'] == 4, 60, 'hub hover 4 (ITEM) not reached')
    r.set_pad(rc.PAD['CROSS'], *STICK_LEFT)
    r.step(2)
    r.set_pad(0)
    wait(r, lambda: (lambda c: c['phase'] == 3 and c['step'] == 2 and c['screen'] == 0 and c['child'] == 1)(
        page_core(r)), 900, 'the ITEM root did not open')
    r.idle(10)
    r.set_pad(0, *STICK_DOWN_LEFT)
    wait(r, lambda: page_core(r)['hover'] == 5, 60, 'ITEM hover 5 (HEALING) not reached')
    r.set_pad(rc.PAD['CROSS'], *STICK_DOWN_LEFT)
    r.step(2)
    r.set_pad(0)
    wait(r, lambda: (lambda c: c['child'] == 7 and c['state'] == 1)(page_core(r)), 900, 'HEALING did not open')
    r.idle(30)


def close_screen(r):
    """Circle back to the ITEM root and the hub (a press every 20 frames), START."""
    for _ in range(5):
        if page_core(r)['child'] == 1:
            break
        r.press('CIRCLE', 2, after=18)
    wait(r, lambda: page_core(r)['child'] == 1, 100, 'HEALING did not return to the root')
    r.idle(10)
    for _ in range(5):
        if page_core(r)['phase'] == 1:
            break
        r.press('CIRCLE', 2, after=18)
    wait(r, lambda: (lambda c: c['phase'] == 1 and c['step'] == 1)(page_core(r)), 100,
         'the ITEM root did not return to the hub')
    r.idle(10)
    r.press('START', 2)
    r.until(rc.in_control, 600)
    rc.settle(r, 20)


def prompt_yes(r):
    """The use prompt (t[5] = 4, the cursor on No): Left, Cross."""
    wait(r, lambda: page_core(r)['state'] == 4, 300, 'no use prompt')
    r.idle(30)
    r.press('LEFT', 2, after=20)
    assert page_core(r)['cursor'] == 0, 'prompt cursor not on Yes'
    r.press('CROSS', 2)


def beat_menu_cure(r):
    rc.use_a02_sampler(r)
    rc.next_long_frames(r)
    rc.walk_path(r, [(78, -205), (75, -197)], tol=1.0)
    rc.approach(r, 75.0, -195.0)
    rc.face(r, 0.0)
    hp0, inf0 = r.rows[-1]['hp'], r.rows[-1]['inf']
    open_healing(r)
    r.press('CROSS', 2)                          # item 0x20: 00185420(0x20) finds the bed
    prompt_yes(r)
    rc.next_control_kept(r, 6000, 60)
    rc.settle(r, 10)
    return {'what': 'at the bed [0]: START, ITEM, HEALING, Cross on item 0x20 (00185420 finds the bed), '
                    'Yes: the bed and the count-up',
            'hp_before': hp0, 'inf_before': inf0, 'hp_end': r.rows[-1]['hp'], 'inf_after': r.rows[-1]['inf']}


def beat_menu_cure_none(r):
    rc.use_a01r_sampler(r)
    rc.next_long_frames(r)
    open_healing(r)
    r.press('CROSS', 2)                          # item 0x20: 00185420(0x20) finds nothing
    wait(r, lambda: page_core(r)['state'] == 6, 60, 'no refusal banner')
    banner = struct.unpack('<I', r.s.read(0x2821B8, 4))[0]
    wait(r, lambda: page_core(r)['state'] == 1, 400, 'the banner did not end')
    r.idle(20)
    close_screen(r)
    return {'what': 'AREA01 (the revisit, item 0x20 held): START, ITEM, HEALING, Cross on item 0x20: '
                    'no device (banner), back out, START', 'banner': banner}


def beat_fire_medicine(r):
    rc.use_a01_sampler(r)
    rc.a01_door_out_of_control(r)
    rc.walk_path(r, [(52, -563.5)] + rc.A01_TO_CRATE_NORTH[:6], tol=1.2)
    hp0 = rc.a01_hp(r)
    r.until(lambda row: row['m1F0'] == 0x3E, 300, 0, *rc.stick_values(r, 34.0, -708.0))
    r.set_pad(0)
    rc.settle(r, 30)
    hp1 = rc.a01_hp(r)
    if not hp1 < 100:
        raise RuntimeError(f'no burn: health {hp1}')
    open_healing(r)
    r.press('CROSS', 2)                          # item 0x1E: the use prompt
    prompt_yes(r)
    wait(r, lambda: page_core(r)['state'] == 1, 600, 'the count-up did not end')
    r.idle(20)
    close_screen(r)
    return {'what': 'from the duct: out of the control room, the fire east of the crates (burn), START, '
                    'ITEM, HEALING, Cross on item 0x1E, Yes: the count-up (002160B0 state 5), back out, START',
            'hp_before': hp0, 'hp_burnt': hp1, 'hp_end': rc.a01_hp(r)}


BEATS = {
    'a02_s1_menu_cure': ('a01r_03_door16', beat_menu_cure),
    'a01r_s2_menu_cure_none': ('a01r_02_bridge', beat_menu_cure_none),
    'a01_s8_fire_medicine': ('a01_s5_duct', beat_fire_medicine),
}


def extras(s):
    return dict(d858=struct.unpack('<2f', s.read(0x810858, 8)), r220=struct.unpack('<f', s.read(0x8104D0, 4))[0],
                r228=struct.unpack('<f', s.read(0x8104D8, 4))[0], ui=s.read(UI, 0x40).hex(),
                items=list(s.read(0x810C80, 8)[2:7]), b0=s.read(0x8106B0, 4)[:2].hex(),
                msg=list(struct.unpack('<3i', s.read(0x2821B0, 12))))


def run(name):
    source, fn = BEATS[name]
    rc.use_fork()
    src = rc.beat_source(source)
    out = rc.beat_dir(name)
    assert not out.exists(), (name, 'beat folder exists; remove it first', str(out))
    base = out.parent
    with rc.open_session(src, log_dir=base / 'logs' / name) as s:
        s.step(LEAD_IN)
        r = rc.Route(s)
        heal = []
        step = r.step

        def sampled_step(n=1):
            row = None
            for _ in range(n):
                row = step(1)
                heal.append(dict(extras(s), f=row['f'], counter=row['counter']))
            return row
        r.begin()
        heal.append(dict(extras(s), f=0, counter=r.rows[0]['counter']))
        r.step = sampled_step
        try:
            meta = fn(r)
        except Exception as exc:
            fail = base / '_failed' / name
            fail.mkdir(parents=True, exist_ok=True)
            (fail / 'trace.json').write_text(json.dumps({'error': repr(exc), 'inputs': r.inputs, 'rows': r.rows,
                                                         'heal': heal}, separators=(',', ':')))
            raise
        if r.rows[0]['f'] != 0 or heal[0]['counter'] != r.rows[0]['counter']:
            raise RuntimeError('row 0 is not the first sample')
        meta.update(source=source, tail_idle_frames=0, emulator='fork', generation=rc.GENERATION,
                    source_state=str(src), lead_in_frames=LEAD_IN, phase_correction_frames=0,
                    phase_lock=False, recorder='port tools/capture_heal_beats.py')
        saved = r.save(name, meta)
        (saved / 'heal.json').write_text(json.dumps(heal, separators=(',', ':')) + '\n')
    ok = rc.resumes(saved / 'state.p2s', base / 'logs' / (name + '_check'))
    print(name, '->', saved, r.frame_index, 'frames;', meta, 'resumes' if ok else 'DOES NOT RESUME', flush=True)
    return ok


def main():
    names = sys.argv[1:] or list(BEATS)
    bad = [n for n in names if n not in BEATS]
    if bad:
        raise SystemExit(f'unknown beats {bad}; known: {list(BEATS)}')
    return 0 if all(run(n) for n in names) else 1


if __name__ == '__main__':
    sys.exit(main())
