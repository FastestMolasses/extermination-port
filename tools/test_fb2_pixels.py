#!/usr/bin/env python3
"""The fb2 pixel harness: a number for "looks like the original".

PORT_PROFILES.md "Queued work" 1 and GS_EXACT.md section 10. The original
frames are the decomp's software-renderer framebuffers
(`../Extermination/build/s87/c7cap/fb2/<point>/displayed.bin`, decomp
CAPTURES_C7.md 5b): 19 route points, each the DISPFB2 field (512x224
PSMCT32, RGBA bytes, raster order) two main-loop iterations after a
recorded state. They are PCSX2's software GS, not real hardware.

What is compared, and how (like with like):

- **The original's frame.** At an fb2 point the outputs come from loop top
  s2. The displayed buffer is the one FRAME named at loop top s1: it was
  drawn in iteration s1 -> s2 from the list built in iteration s0 -> s1,
  and it shows the game state of row s1 (CAPTURES_C7.md 5b "What happens
  to the two buffers"). It was drawn with the XYOFFSET of s1 (OFY 1936.0
  or 1936.5 with the field). The harness reads both from meta.json and
  refuses a point whose displayed FBP is not s1's FRAME.
- **The port's frame.** The port's Original profile draws the world frame
  with the CPU GS model (src/gs/em_gs_world.h, GS_EXACT.md section 9): a
  512x224 PSMCT32 field in GS memory, which a capture writes beside the
  host image as `<capture>.gsfield` (512 x 224 x 4 bytes, R, G, B, A). The
  original's displayed buffer holds the whole list, its letterbox bars,
  transition fade and message glyphs included, which the port draws as the
  GPU's 2D overlay pass over the presented field; so the number compared
  is the presented frame at the centre of each field pixel (the
  placeholder presentation shows field pixel (x, y) there unchanged), and
  the field alone is reported beside it. The harness reports whether the
  field's FRAME_1 and XYOFFSET_1 are the original's displayed buffer and
  OFY at s1 (the frame loop's phase). With the GPU renderer
  (EM_GPU_RENDERER=1, the Enhanced profile's path) there is no field: the
  port draws with Metal into the headless render target (the 960x720
  window times the backing scale, 1920x1440 on this Mac), 4:3 game rect,
  and the harness samples it as below. The tick whose post-task state is route row
  s1 is found from the level smoke's own alignment
  (tools/test_level_smoke.py): a snapshot NN aligned at port tick i
  (post(i) = the recorded row `rec` of snapshot NN) gives tick
  i + (s1 - rec); first control (post(idx) = route 01 f0 = counter 4085)
  gives idx + (s1 - 4085). The harness then checks that the captured
  tick is that tick, and reports whether the port's camera eye / target,
  the player's position / heading and the task bytes (`spad`) at it equal
  row s1 bit for bit ("camera exact"). Only then is the frame the same
  game state (the drawn owners' own states are the level smoke's
  check_owner_units; the snow and the flame follow the port's rand()
  stream and differ at every point).
- **Sampling to 512x224 (GPU renderer only).** The GS samples pixel (x, y) of a field at the
  window point (x, y) = vertex - XYOFFSET (GS_EXACT.md 3.1). The port's
  projection (em_mat4_perspective_gs) maps the 4:3 viewport's width to
  the field's 512 pixels (x = 256 * (1 + ndc_x)) and its height to 224
  lines with OFY 1936.0 (y = 112 * (1 - ndc_y)). So GS pixel (x, y) with
  offset OFY is the port point x_b = x, y_b = y + (OFY - 1936) in field
  units, and the harness takes the Metal pixel whose centre is nearest to
  it: column floor(vx + x_b * vw / 512), row floor(vy + y_b * vh / 224)
  of the game rect (vx, vy, vw, vh). No filtering, no averaging. At
  1920x1440 a Metal pixel is 1/3.75 GS pixel wide and 1/6.43 line tall,
  so the sampled Metal pixel's own centre is at most 0.13 pixel / 0.08
  line from the GS sample point: that rounding is the sampling's own
  error, and it is not hidden or compensated.
- **Only RGB is compared.** PMODE shows circuit 2 only (EN1 0), so the
  displayed image has no alpha (CAPTURES_C7.md 5b "Pixel alpha").

Metrics per point: the exact-match fraction (all three channels equal),
the mean absolute channel error over RGB, the maximum channel error, and
percentiles of the per-pixel maximum channel error. A difference image,
the port's sampled field and the original field go to
build/fb2_pixels/<point>/ (ignored; the runs' tick logs and host-size
captures are deleted after the comparison). The metric is never tuned: no
tolerance, no alignment search, no blur.

Modes (CLAUDE.md "Tests"):
- quick (default, about 17 s: the New Game path to first control is the
  floor): the first_control point, from one level-smoke run to first
  control that captures the frame of the tick after first control. The
  port's camera is not the original's there (its opening ends earlier at
  host speed and the camera is still rising: census L33), so this point
  measures that difference as well as the pixel path.
- full (EM_TEST_FULL=1, about 4.5 min): the quick run, then the whole main
  route through roger twice (pass 1 aligns, pass 2 captures the aligned
  ticks and is re-aligned on its own log, so a run that is not
  deterministic fails), then every fb2 point: compared where the smoke
  aligns a tick, listed with the reason where it does not. first_control
  always comes from the quick run: the route run opens the status screen
  right after first control, so its next frame is not the recorded
  neutral-pad continuation.

Assertions: a compared point's captured tick is the aligned tick; the
camera-exact points (10 and 14, test_level_smoke.VIEW_EXACT) must still be
camera exact; and each
compared point's exact-match fraction must not fall below the floor
recorded in FLOORS (the GS field) or FLOORS_GPU (EM_GPU_RENDERER=1; the
Metal floors of 2026-09-28). GS_EXACT.md section 10 has the numbers per
point. The floors are measured values rounded down to 0.1 percentage
point, so a renderer change that loses matching pixels fails; one that
gains them should raise the floor in the same commit. The GS field's
buffer and OFY against the original's (field_phase) are only reported, not
asserted: they differ at 5 of the 7 compared points (GS_EXACT.md 10.1, an
open finding), and a phase difference already shows in the pixel numbers.
"""
import contextlib
import io
import json
import os
import re
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM            # noqa: E402
import test_level_smoke as T           # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
FB2 = DECOMP / 'build' / 's87' / 'c7cap' / 'fb2'
ROUTE = DECOMP / 'build' / 's87' / 'route'
BIN = ROOT / 'build' / 'extermination'
OUT = ROOT / 'build' / 'fb2_pixels'

POINTS = ('00_panel_no_battery', '01_battery', '02_elevator_refusal', '03_panel_power', '04_elevator_ride',
          '05_boxes', '06_hill_slide', '07_truck_preview', '08_truck_crossing', '09_fence_door',
          '10_cage_roof_roger', '11_crevice_prompt', '12_crevice_jump', '13_east_tower', '14_roger_encounter',
          '15_level_exit', 'first_control', 'route03_end', 'route07_end')
FIRST_CONTROL_COUNTER = 4085   # route 01 f0 = user slot 04 (the smoke's first_control tick)
GS_W, GS_H, GS_OFY = 512, 224, 1936.0
GPU = os.environ.get('EM_GPU_RENDERER') == '1'
# Exact-match floors (fraction of the 114,688 pixels) of the presented GS
# field, 2026-10-03 (chain step GSFRAME); see the module docstring and
# GS_EXACT.md section 10.
FLOORS = {
    'first_control': 0.009,
    '08_truck_crossing': 0.031,
    '10_cage_roof_roger': 0.159,
    '11_crevice_prompt': 0.054,
    '12_crevice_jump': 0.024,
    '13_east_tower': 0.559,
    '14_roger_encounter': 0.871,
}
# The same with the GPU renderer (EM_GPU_RENDERER=1), measured 2026-09-28.
FLOORS_GPU = {
    'first_control': 0.008,
    '08_truck_crossing': 0.027,
    '10_cage_roof_roger': 0.308,
    '11_crevice_prompt': 0.079,
    '12_crevice_jump': 0.024,
    '13_east_tower': 0.353,
    '14_roger_encounter': 0.287,
}
# Why the level smoke aligns no port tick with a point (full mode lists
# these instead of a number).
NOT_ALIGNED = {
    '00_panel_no_battery': 'side beat 00 runs on its own (test-level-smoke-side) and registers no snapshot',
    '01_battery': 'the smoke aligns route 02 on the elevator scan, not on snapshot 01',
    '02_elevator_refusal': 'the smoke aligns route 03 on the panel scan, not on snapshot 02',
    '03_panel_power': 'the smoke aligns route 04 on the terminal scan, not on snapshot 03',
    '04_elevator_ride': 'the smoke aligns route 05 on the climbs, not on snapshot 04',
    '05_boxes': 'the smoke aligns route 06 on the slide, not on snapshot 05',
    '06_hill_slide': 'the smoke aligns route 07 on the trigger, not on snapshot 06',
    '07_truck_preview': 'the smoke aligns route 08 on the truck, not on snapshot 07',
    '09_fence_door': 'side beat 09 runs on its own and registers no snapshot',
    '15_level_exit': 'AREA01 (the smoke ends at Roger)',
    'route03_end': 'a repeat of 03 (no snapshot alignment)',
    'route07_end': 'a repeat of 07 (no snapshot alignment)',
}


# ------------------------------------------------------------------ inputs

def fb2_point(name):
    """The point's displayed field and the facts that place it: the loop
    tops, the recorded snapshot's counter and the OFY it was drawn with."""
    d = FB2 / name
    meta = json.loads((d / 'meta.json').read_text())
    stages = {s['at']: s for s in meta['snapshots']}
    s1, s2 = stages['s1'], stages['s2']
    shown = int(meta['displayed_buffer']['fbp'], 16)
    assert int(s2['dispfb2_fbp'], 16) == shown, (name, 'DISPFB2 at s2 is not the displayed buffer')
    frames = {int(f, 16) for f in s1['frame_fbp']}
    assert frames == {shown}, (name, 'the displayed buffer is not the FRAME of s1', s1['frame_fbp'], shown)
    ofy = set(s1['ofy'])
    assert len(ofy) == 1, (name, 'both contexts do not share one OFY at s1', s1['ofy'])
    raw = np.frombuffer((d / 'displayed.bin').read_bytes(), dtype=np.uint8)
    assert raw.size == GS_W * GS_H * 4, (name, 'displayed.bin size', raw.size)
    rec = meta.get('recorded_snapshot') or {}
    return {'name': name, 'rgb': raw.reshape(GS_H, GS_W, 4)[:, :, :3].copy(), 'ofy': ofy.pop(), 'fbp': shown,
            's1': int(s1['counter']), 'rec': rec.get('counter'), 'field_at_s1': s1.get('csr_field')}


_ROWS = {}


def route_row(counter):
    """The recorded route row at a main-loop counter (any beat), or None."""
    if not _ROWS:
        for beat in sorted(p.name for p in ROUTE.iterdir() if p.name[:2].isdigit()):
            trace = ROUTE / beat / 'trace.json'
            if trace.exists():
                for row in json.loads(trace.read_text())['rows']:
                    _ROWS.setdefault(row['counter'], (beat, row))
    return _ROWS.get(counter)


def read_bmp(path):
    """A 24-bit bottom-up BMP (em_gfx_metal.m write_bmp) as (h, w, 3) RGB."""
    b = Path(path).read_bytes()
    assert b[:2] == b'BM', (path, 'not a BMP')
    off, = struct.unpack_from('<I', b, 10)
    w, h = struct.unpack_from('<ii', b, 18)
    bpp, = struct.unpack_from('<H', b, 28)
    assert bpp == 24 and h > 0, (path, 'unexpected BMP layout', bpp, h)
    stride = (w * 3 + 3) & ~3
    rows = np.frombuffer(b, dtype=np.uint8, count=stride * h, offset=off).reshape(h, stride)[:, :w * 3]
    return rows.reshape(h, w, 3)[::-1, :, ::-1].copy()   # top-down, BGR -> RGB


def game_rect(w, h):
    """em_gfx_metal.m begin_frame's 4:3 viewport, in target pixels."""
    vw, vh, vx, vy = float(w), float(h), 0.0, 0.0
    if w * 3.0 >= h * 4.0:
        vw = h * 4.0 / 3.0
        vx = (w - vw) * 0.5
    else:
        vh = w * 3.0 / 4.0
        vy = (h - vh) * 0.5
    return vx, vy, vw, vh


def sample_field(port, ofy):
    """The port's frame at the GS sample points of a 512x224 field drawn
    with this OFY (module docstring, "Sampling to 512x224")."""
    h, w, _ = port.shape
    vx, vy, vw, vh = game_rect(w, h)
    xs = np.floor(vx + np.arange(GS_W) * vw / GS_W).astype(np.int64)
    ys = np.floor(vy + (np.arange(GS_H) + (ofy - GS_OFY)) * vh / GS_H).astype(np.int64)
    assert xs.min() >= 0 and xs.max() < w and ys.min() >= 0 and ys.max() < h, 'sample outside the target'
    return port[ys][:, xs], (w, h, vx, vy, vw, vh)


def shown_field(port):
    """The presented host frame at the centre of each field pixel of the 4:3
    game rect (EM_GFX_FIELD_SPREAD shows field pixel (x, y) over the rect's
    columns [x * vw / 512, (x + 1) * vw / 512) and rows likewise)."""
    h, w, _ = port.shape
    vx, vy, vw, vh = game_rect(w, h)
    xs = np.floor(vx + (np.arange(GS_W) + 0.5) * vw / GS_W).astype(np.int64)
    ys = np.floor(vy + (np.arange(GS_H) + 0.5) * vh / GS_H).astype(np.int64)
    return port[ys][:, xs], (w, h, vx, vy, vw, vh)


def metrics(port, orig):
    d = np.abs(port.astype(np.int16) - orig.astype(np.int16))
    per = d.max(axis=2)
    n = per.size
    return {'pixels': int(n), 'exact': int((per == 0).sum()), 'exact_fraction': float((per == 0).sum()) / n,
            'mean_abs_channel_error': float(d.mean()), 'max_channel_error': int(d.max()),
            'p50': int(np.percentile(per, 50)), 'p90': int(np.percentile(per, 90)),
            'p99': int(np.percentile(per, 99))}, per


def write_png(path, rgb):
    h, w, _ = rgb.shape
    raw = b''.join(b'\x00' + rgb[y].astype(np.uint8).tobytes() for y in range(h))
    def chunk(kind, data):
        c = kind + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def diff_image(per):
    """Per-pixel maximum channel error: 0 black, 1..255 as grey 64 + err * 3
    (clamped), so every non-exact pixel is visible."""
    g = np.where(per == 0, 0, np.minimum(255, 64 + per.astype(np.int32) * 3)).astype(np.uint8)
    return np.stack([g, g, g], axis=2)


# ------------------------------------------------------------------ the port

def run_port(tag, until, env_extra):
    d = OUT / tag
    d.mkdir(parents=True, exist_ok=True)
    for f in d.glob('*'):
        if f.is_file():
            f.unlink()
    env = dict(os.environ, EM_UNCAPPED='1', EM_STARTUP_TEST='newgame-level', EM_LEVEL_SMOKE_UNTIL=until,
               EM_AREA_CHANGE_LOG=str(d / 'ticks.jsonl'))
    env.update(env_extra)
    t0 = time.monotonic()
    with open(d / 'run.log', 'w') as log:
        rc = subprocess.run([str(BIN)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
    run = (d / 'run.log').read_text()
    assert rc == 0 and re.search(r'^level smoke: PASS ', run, re.M), (tag, 'the port run failed', rc,
                                                                      run[-2000:])
    ticks = [json.loads(line) for line in (d / 'ticks.jsonl').open()]
    return d, run, ticks, time.monotonic() - t0


def alignment(ticks, run, full):
    """{point: port tick index whose post is the point's snapshot row}, from
    the level smoke's own phase checks (their prints are swallowed; their
    assertions still run)."""
    state = {'drive': T.R.drive_mode(run), 'status_pages_trace': None}
    with contextlib.redirect_stdout(io.StringIO()):
        if full:
            for name, check in T.PHASES:
                if re.search(rf'^level smoke: {name}: PASS', run, re.M):
                    check(ticks, run, state)
        else:
            T.check_first_control(ticks, run, state)
    base = {beat: i for beat, i in state.get('snapshots', [])}
    base['first_control'] = state['first_control']
    return base


def f32(v):
    return round(T.f32(v), 5)


def placement(ticks, i, row):
    """Camera eye / target, the player's position / heading and the task
    bytes (the scratchpad block the route rows hold as `spad`) of a port
    tick's post against a route row, bit for bit (as rounded by the rows)."""
    tick = ticks[i]
    port = {'eye': [f32(v) for v in tick['eye_post']], 'tgt': [f32(v) for v in tick['tgt_post']],
            'pos': [f32(v) for v in tick['pos_post']], 'yaw': f32(tick['yaw_post']),
            'spad': T.port_view(ticks, i)['spad']}
    diff = {k: port[k] for k in port if port[k] != row[k]}
    return not diff, diff


# ------------------------------------------------------------------ main

def target_tick(point, base):
    snap = base.get('first_control') if point['name'] == 'first_control' else base.get(point['name'])
    if snap is None:
        return None
    rec = FIRST_CONTROL_COUNTER if point['name'] == 'first_control' else point['rec']
    return snap + (point['s1'] - rec)


def gs_field(capture, run):
    """The GS field a capture wrote (<capture>.gsfield) as (224, 512, 3) RGB,
    with the FRAME_1 and XYOFFSET_1 the run log reports for it; None when
    the run drew no field (the GPU renderer)."""
    path = Path(str(capture) + '.gsfield')
    if not path.exists():
        return None
    raw = np.frombuffer(path.read_bytes(), dtype=np.uint8)
    assert raw.size == GS_W * GS_H * 4, (path, 'the GS field is not 512x224', raw.size)
    m = re.search(rf'^capture: wrote {re.escape(str(path))} \(512x224 GS field, FRAME_1 ([0-9a-f]{{16}}), '
                  rf'XYOFFSET_1 ([0-9a-f]{{16}})\)', run, re.M)
    assert m, (path, 'the run log names no FRAME_1 / XYOFFSET_1 for the field')
    return raw.reshape(GS_H, GS_W, 4)[:, :, :3].copy(), int(m.group(1), 16), int(m.group(2), 16)


def compare(point, tick_index, ticks, capture, run):
    name = point['name']
    t = ticks[tick_index]
    row = route_row(point['s1'])
    exact, diff = placement(ticks, tick_index, row[1]) if row else (None, 'no recorded row at s1')
    gs = gs_field(capture, run)
    assert (gs is None) == GPU, (name, 'the GS field is missing' if gs is None else
                                 'a GS field was written with EM_GPU_RENDERER=1')
    phase = None
    if gs is not None:
        field, frame, xyoffset = gs
        port_ofy = ((xyoffset >> 32) & 0xFFFF) / 16.0
        # the field's buffer and half line against the original's at s1 (the
        # frame loop's phase, RENDER_CONTEXT.md 9.3): reported, and the
        # comparison stands either way (a phase difference is itself a
        # difference of the port's frame)
        phase = {'port_fbp': hex(frame & 0x1FF), 'original_fbp': hex(point['fbp']), 'port_ofy': port_ofy,
                 'original_ofy': point['ofy'], 'same': frame & 0x1FF == point['fbp'] and port_ofy == point['ofy']}
        # what is shown: the presented frame (the field under the GPU's 2D
        # overlay pass: the letterbox bars, the transition fade, the message
        # glyphs) read back at the field's pixel centres, where the
        # placeholder presentation shows field pixel (x, y) unchanged
        presented, geometry = shown_field(read_bmp(capture))
        bare = field
        field = presented
    else:
        port_rgb = read_bmp(capture)
        field, geometry = sample_field(port_rgb, point['ofy'])
        bare = None
    m, per = metrics(field, point['rgb'])
    d = OUT / name
    d.mkdir(parents=True, exist_ok=True)
    write_png(d / 'port_512x224.png', field)
    write_png(d / 'original_512x224.png', point['rgb'])
    write_png(d / 'diff_512x224.png', diff_image(per))
    bare_metrics = None
    if bare is not None:
        bare_metrics, bper = metrics(bare, point['rgb'])
        write_png(d / 'port_gs_field_512x224.png', bare)
        write_png(d / 'diff_gs_field_512x224.png', diff_image(bper))
    result = dict(m, point=name, port_tick=t['tick'], s1=point['s1'], ofy=point['ofy'],
                  port_frame='GS field' if gs is not None else 'Metal (GPU renderer)', field_phase=phase,
                  gs_field_alone=bare_metrics,
                  row=f'{row[0]} f{row[1]["f"]}' if row else None, camera_exact=exact,
                  differs=diff if not exact else None, target=list(geometry[:2]),
                  game_rect=[round(v, 3) for v in geometry[2:]])
    (d / 'result.json').write_text(json.dumps(result, indent=1))
    return result


def line(r):
    cam = ('camera exact' if r['camera_exact'] else
           f'camera NOT exact ({", ".join(sorted(r["differs"]))} differ)' if r['camera_exact'] is False else
           r['differs'])
    ph = r.get('field_phase')
    if ph and not ph['same']:
        cam += f'; the port field is FBP {ph["port_fbp"]} OFY {ph["port_ofy"]} (the original\'s {ph["original_fbp"]}, {ph["original_ofy"]})'
    g = r.get('gs_field_alone')
    if g:
        cam += (f'; the GS field alone (no overlay pass): exact {g["exact"]:,} '
                f'({100 * g["exact_fraction"]:.2f}%), mean channel error {g["mean_abs_channel_error"]:.2f}')
    return (f'{r["point"]}: port tick {r["port_tick"]} = row {r["row"]} (s1 {r["s1"]}, OFY {r["ofy"]}), {cam}: '
            f'exact {r["exact"]:,} of {r["pixels"]:,} ({100 * r["exact_fraction"]:.2f}%), mean channel error '
            f'{r["mean_abs_channel_error"]:.2f}, max {r["max_channel_error"]}, per-pixel max error p50/p90/p99 '
            f'{r["p50"]}/{r["p90"]}/{r["p99"]}')


def quick_point(points):
    """first_control from one level-smoke run to first control (both modes:
    the route run opens the status screen right after first control, so its
    next tick is not the recorded neutral-pad continuation)."""
    cap = OUT / 'quick' / 'first_control.bmp'
    d, run, ticks, secs = run_port('quick', 'first_control',
                                   {'EM_LEVEL_SMOKE_FB_CAPTURE': f'first_control+2:{cap}'})
    base = alignment(ticks, run, False)
    want = target_tick(points['first_control'], base)
    m = re.search(r'^fb capture: tick (\d+) -> ', run, re.M)
    assert m and cap.exists(), 'the port wrote no capture'
    assert int(m.group(1)) == ticks[want]['tick'], ('first_control: the captured tick is not the aligned '
                                                    'tick', int(m.group(1)), ticks[want]['tick'])
    return compare(points['first_control'], want, ticks, cap, run), secs


def main():
    assert BIN.exists(), 'build/extermination is missing (make all)'
    if not FB2.exists():
        print(f'fb2 pixels: SKIP: {FB2} is missing (the decomp\'s C7 capture, CAPTURES_C7.md 5b)')
        return
    OUT.mkdir(parents=True, exist_ok=True)
    points = {name: fb2_point(name) for name in (POINTS if RM.FULL else ('first_control',))}
    results, listed = [], []
    first, s0 = quick_point(points)
    results.append(first)
    runs = f'one port run to first control ({s0:.0f} s)'
    if RM.FULL:
        d1, run1, ticks1, s1 = run_port('pass1', 'roger', {})
        base1 = alignment(ticks1, run1, True)
        wanted = {}
        for name, p in points.items():
            if name == 'first_control':
                continue
            i = target_tick(p, base1)
            if i is None or i >= len(ticks1):
                listed.append((name, NOT_ALIGNED.get(name, 'no aligned port tick')))
            else:
                wanted[name] = i
        spec = ';'.join(f'{ticks1[i]["tick"]}:{OUT / "pass2" / (name + ".bmp")}' for name, i in wanted.items())
        d2, run2, ticks2, s2 = run_port('pass2', 'roger', {'EM_FB_CAPTURE_TICKS': spec})
        base2 = alignment(ticks2, run2, True)
        for name, i in wanted.items():
            j = target_tick(points[name], base2)
            assert j == i and ticks2[j]['tick'] == ticks1[i]['tick'], (name, 'the port run is not deterministic: '
                                                                       'pass 2 aligns another tick', i, j)
            assert ticks2[j]['post'] == ticks1[i]['post'], (name, 'pass 2 state differs at the aligned tick')
            results.append(compare(points[name], j, ticks2, OUT / 'pass2' / (name + '.bmp'), run2))
        runs += f', two port runs through roger ({s1:.0f} s, {s2:.0f} s)'
    for r in results:
        print('fb2 pixels: ' + line(r))
    for name, why in listed:
        print(f'fb2 pixels: {name}: not compared: {why}')
    (OUT / 'summary.json').write_text(json.dumps({'mode': RM.MODE, 'results': results,
                                                  'not_compared': dict(listed)}, indent=1))
    # The runs' tick logs (up to 300 MB each) and the host-size captures are
    # scratch; the per-point PNGs, result.json and run.log stay.
    for run_dir in ('quick', 'pass1', 'pass2'):
        for f in (list((OUT / run_dir).glob('ticks.jsonl')) + list((OUT / run_dir).glob('*.bmp')) +
                  list((OUT / run_dir).glob('*.bmp.gsfield'))):
            f.unlink()
    for r in results:
        if r['point'] in T.VIEW_EXACT:
            assert r['camera_exact'], (r['point'], 'a camera-exact point is no longer camera exact', r['differs'])
        floor = (FLOORS_GPU if GPU else FLOORS).get(r['point'])
        if floor is not None:
            assert r['exact_fraction'] >= floor, (r['point'], 'exact-match fraction below its floor',
                                                  r['exact_fraction'], floor)
    RM.banner(f'{len(results)} of {len(POINTS)} fb2 points compared')
    frame = ('the presented GS field (under the overlay pass) at the field pixel centres, and the field alone'
             if not GPU else
             f'Metal {results[0]["target"][0]}x{results[0]["target"][1]}, 4:3 rect {results[0]["game_rect"]}, '
             'point-sampled at the GS sample points')
    print(f'fb2 pixels: PASS ({runs}; port frame: {frame}; images and summary.json in '
          f'{OUT.relative_to(ROOT)}/)')


if __name__ == '__main__':
    main()
