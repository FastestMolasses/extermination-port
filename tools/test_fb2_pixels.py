#!/usr/bin/env python3
"""The fb2 pixel harness: a number for "looks like the original".

PORT_PROFILES.md "Queued work" 1 and GS_EXACT.md section 10. The original
frames are the decomp's software-renderer framebuffers (decomp
CAPTURES_C7.md 5b): 19 route points, each the DISPFB2 field (512x224
PSMCT32, RGBA bytes, raster order) two main-loop iterations after a
recorded state. They are PCSX2's software GS, not real hardware. By
default they are the PCSX2 agent-debug fork's re-recordings
(`../Extermination/build/fork_refs/pixels/fb2/<point>/displayed.bin`, with
the fork's route `build/fork_refs/s87/route`; decomp PCSX2_FORK.md "Pixel
references re-recorded on the fork"); EM_REFERENCE=v263 reads PCSX2
2.6.3's (`build/s87/c7cap/fb2`, `build/s87/route`; tools/reference_root.py).
The counters below are v2.6.3's; the fork's are 784 lower from first
control on and 776 lower at the end of route 15.

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
  is the presented frame at the centre of each field pixel's place in the
  picture (the presentation, GS_EXACT.md section 11, shows field pixel
  (x, y) there unchanged: the lines 2y + line .. 2y + line + 1 of the 448,
  line 1 for a half-line OFY, moved by the screen position's shift; the
  capture's <path>.present gives them; shown_field reads one point inside
  them), and the field alone is reported beside it. The harness reports whether the
  field's FRAME_1 and XYOFFSET_1 are the original's displayed buffer and
  OFY at s1 (the frame loop's phase). With the GPU renderer
  (EM_GPU_RENDERER=1, the Enhanced profile's path) there is no field: the
  port draws with Metal into the headless render target (the 960x720
  window times the backing scale, 1920x1440 on this Mac), 4:3 game rect,
  and the harness samples it as below. The tick whose post-task state is route row
  s1 is found from the level smoke's own alignment
  (tools/test_level_smoke.py): a snapshot NN aligned at port tick i
  (post(i) = the recorded row `rec` of snapshot NN) gives tick
  i + (s1 - rec); first control (post(idx) = route 01 f0 = counter 4085,
  FIRST_CONTROL_COUNTER: 3301 on the fork) gives idx + (s1 - 4085). The harness then checks that the captured
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
- full (EM_TEST_FULL=1, about 6 min): the quick run, then the whole main
  route through the AREA01 arrival (a01_arrival) twice (pass 1 aligns,
  pass 2 captures the aligned ticks and is re-aligned on its own log, so a
  run that is not deterministic fails), then every fb2 point: compared
  where the smoke aligns a tick, listed with the reason where it does not.
  first_control always comes from the quick run: the route run opens the
  status screen right after first control, so its next frame is not the
  recorded neutral-pad continuation.
- `--point 15_level_exit` (about 2.5 min): only the AREA01 point, from one
  run to the AREA01 arrival. Its recorded snapshot is route 15's last row
  f801 (counter 16562; the fork's f809, 15786), the end of the smoke's a01_arrival phase, and the
  capture session after it ran neutral-pad frames, as the port's run does
  after the phase; its row s1 (16564, two frames on) is not in route 15's
  trace, so the camera / player / task comparison uses the capture's own
  row (capture.json, recorded in the same session as the field).

The like-for-like original per point (2026-10-10, GS_EXACT.md 10.4). The
buffer a frame is drawn into follows D_00810E80 (it flips every main-loop
iteration) and its half line (OFY) follows the field bit D_00810E88; at a
load both follow the disc's timing, which the port does not reproduce (host
speed, CLAUDE.md). So the same game state can be drawn on the other phase.
With the GS field the harness picks, per point, the original drawn on the
port's phase:
- **primary**: the point's fb2 field (above), when its buffer and OFY at s1
  are the port field's;
- **same phase** (fork reference only): the decomp's same-game-state
  re-recording on the port's phase
  (`../Extermination/build/fork_refs/pixels/fb2_phase/<point>`, decomp
  tools/fb2_phase_refs.py and PCSX2_FORK.md), when the primary's phase is
  not the port's. The harness checks that its s0..s2 capture rows equal the
  primary's on every field but the phase and time ones (PHASE_KEYS) and that
  the manifest records the same rand() state at s2; the game-state facts
  (s1, the recorded snapshot, the route row) stay the primary's;
- **same half line, other buffer**: as above, when the re-recording has the
  port's OFY but the other buffer (15_level_exit: no original run reaches
  the port's phase there, decomp PCSX2_FORK.md). The buffer's address alone
  moves no pixel (the manifest's checks `*_d7_vs_d4`: the same tick and
  half line in buffers 0x0 and 0x38, displayed, draw and Z bit-exact; the
  harness requires them);
- otherwise the primary, with the reason (EM_REFERENCE=v263 has no
  same-phase set; the GPU renderer has no field phase and samples at the
  primary's OFY).
The choice is printed and recorded (`reference` in result.json). The phase
difference itself (the port field's buffer and OFY against the primary's,
`field_phase`) is reported separately as a timing item: the port runs the
disc at host speed (FIRST_LEVEL_AUDIT.md item 2, 1b F1). It is not
asserted.

Assertions: a compared point's captured tick is the aligned tick; the
camera-exact points (10 and 14, test_level_smoke.VIEW_EXACT) must still be
camera exact; and each
compared point's exact-match fraction must not fall below the floor
recorded in FLOORS (the GS field, against the like-for-like original) or
FLOORS_GPU (EM_GPU_RENDERER=1; the Metal floors of 2026-09-28, against the
primary). GS_EXACT.md section 10 has the numbers per point. The floors are
measured values rounded down to 0.1 percentage point, so a renderer change
that loses matching pixels fails; one that gains them should raise the
floor in the same commit.
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
import reference_root as refs          # noqa: E402  (EM_REFERENCE: fork by default)
import test_level_smoke as T           # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
# The fb2 points and the route (EM_REFERENCE, tools/reference_root.py): the
# fork's re-recordings by default (build/fork_refs/pixels/fb2 and
# build/fork_refs/s87/route; decomp PCSX2_FORK.md "Pixel references
# re-recorded on the fork"), PCSX2 2.6.3's (s87/c7cap/fb2, s87/route) with
# EM_REFERENCE=v263.
FB2 = refs.capture('s87/c7cap/fb2')
ROUTE = refs.capture('s87/route')
# The same-game-state re-recordings on the port's field phase (fork only;
# module docstring "The like-for-like original per point").
FB2_PHASE = refs.FORK / 'pixels' / 'fb2_phase' if refs.FORK_MODE else None
# Capture-row fields that are the frame loop's phase, time or provenance,
# not game state (decomp tools/fb2_phase_refs.py compares the chains the
# same way); in `hw` only the two phase words are left out.
PHASE_KEYS = {'counter', 'f', 'fi', 'fld', 'vs', 'vsync', 'inside_step', 'snapshot'}
HW_PHASE_KEYS = {'frame_idx_810E80', 'field_810E88'}
BIN = ROOT / 'build' / 'extermination'
OUT = ROOT / 'build' / 'fb2_pixels'

POINTS = ('00_panel_no_battery', '01_battery', '02_elevator_refusal', '03_panel_power', '04_elevator_ride',
          '05_boxes', '06_hill_slide', '07_truck_preview', '08_truck_crossing', '09_fence_door',
          '10_cage_roof_roger', '11_crevice_prompt', '12_crevice_jump', '13_east_tower', '14_roger_encounter',
          '15_level_exit', 'first_control', 'route03_end', 'route07_end')
# route 01 f0 = the first-control state (the smoke's first_control tick): the
# fork chain's counters are 784 lower than v2.6.3's from first control on.
FIRST_CONTROL_COUNTER = 3301 if refs.FORK_MODE else 4085
GS_W, GS_H, GS_OFY = 512, 224, 1936.0
GPU = os.environ.get('EM_GPU_RENDERER') == '1'
# Exact-match floors (fraction of the 114,688 pixels) of the presented GS
# field, 2026-10-03 (chain step GSFRAME); see the module docstring and
# GS_EXACT.md section 10.
FLOORS = {
    # Raised at step DOF (2026-10-09: 001DDE10's depth-of-field pass drawn
    # by the GS model; GS_EXACT.md 10.1): 10 15.96 -> 18.74 %, 11 5.48 ->
    # 5.52 %, 13 55.92 -> 62.44 %, 14 87.18 -> 92.28 %; the others within
    # their floors (first control 0.98 -> 0.97 %, 08 3.14 %, 12 2.46 %,
    # 15 98.57 %). Raised again at the pass's conformance step (2026-10-10:
    # a UV sprite's row coordinate accumulated in binary32, GS_EXACT.md
    # 3.4): 10 -> 18.92 %, 13 -> 62.599 %, 14 -> 92.40 %; 11 5.51 %, first
    # control 0.97 % (1,117 pixels), 08, 12 and 15 unchanged.
    'first_control': 0.009,
    '08_truck_crossing': 0.031,
    '10_cage_roof_roger': 0.189,
    '11_crevice_prompt': 0.055,
    '12_crevice_jump': 0.024,
    '13_east_tower': 0.625,
    '14_roger_encounter': 0.924,
    # AREA01's arrival, measured at the GSFRAME merge (2026-10-08: camera
    # exact, 98.57 %, under the overlay pass's transition fade).
    '15_level_exit': 0.985,
}
if refs.FORK_MODE:
    # Against the fork's fb2 points (2026-10-10; GS_EXACT.md 10.3): 16 of
    # the 19 fields equal v2.6.3's bit for bit, so the floors above hold,
    # and 08 rises (3.14 -> 3.26 %: the fork's field there is drawn with the
    # port's half line, v2.6.3's was a post-load hitch's). 15_level_exit
    # keeps its floor: the fork's arrival field is drawn half a line lower
    # (OFY 1936.5) than the port's and v2.6.3's (1936.0), 18.03 % exact
    # against that primary; an open finding (GS_EXACT.md 10.3, 10.4), not a
    # floor to lower.
    FLOORS = dict(FLOORS, **{'08_truck_crossing': 0.032})
    # Against the like-for-like original (2026-10-10, GS_EXACT.md 10.4):
    # the points whose primary is on the other field phase are compared with
    # the same game state re-recorded on the port's phase (fb2_phase).
    # first control 0.97 -> 1.03 % (1,178 pixels), 10 18.92 -> 95.47 %
    # (109,496), 11 5.51 -> 9.85 % (11,295), 12 2.46 -> 2.60 % (2,983);
    # 15_level_exit back to 98.57 % (113,053; the port's half line, the other
    # buffer), its floor unchanged; 08, 13 and 14 compare with the primary
    # (same phase), unchanged.
    FLOORS = dict(FLOORS, **{'first_control': 0.010, '10_cage_roof_roger': 0.954, '11_crevice_prompt': 0.098,
                             '12_crevice_jump': 0.026})
# The same with the GPU renderer (EM_GPU_RENDERER=1), measured 2026-09-28.
FLOORS_GPU = {
    'first_control': 0.008,
    '08_truck_crossing': 0.027,
    '10_cage_roof_roger': 0.308,
    '11_crevice_prompt': 0.079,
    '12_crevice_jump': 0.024,
    '13_east_tower': 0.353,
    '14_roger_encounter': 0.287,
    # AREA01's arrival (2026-10-04, step DRAWN: camera exact, 47.41 %).
    '15_level_exit': 0.474,
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
    'route03_end': 'a repeat of 03 (no snapshot alignment)',
    'route07_end': 'a repeat of 07 (no snapshot alignment)',
}


# ------------------------------------------------------------------ inputs

def fb2_point(name, base=None):
    """The point's displayed field and the facts that place it: the loop
    tops, the recorded snapshot's counter and the OFY it was drawn with
    (from the primary set, or from `base`)."""
    d = (base or FB2) / name
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
    # The capture session's own rows (counter -> row), recorded with the field.
    cap = d / 'capture.json'
    rows = {r['counter']: r for r in json.loads(cap.read_text()).get('rows', [])} if cap.exists() else {}
    return {'name': name, 'rgb': raw.reshape(GS_H, GS_W, 4)[:, :, :3].copy(), 'ofy': ofy.pop(), 'fbp': shown,
            's1': int(s1['counter']), 'rec': rec.get('counter'), 'field_at_s1': s1.get('csr_field'),
            'rows': rows, 'dir': d}


def game_row(row):
    """A capture row without its phase, time and provenance fields."""
    out = {k: v for k, v in row.items() if k not in PHASE_KEYS}
    if 'hw' in out:
        out['hw'] = {k: v for k, v in out['hw'].items() if k not in HW_PHASE_KEYS}
    return out


_PHASE_MANIFEST = {}


def phase_manifest():
    if not _PHASE_MANIFEST:
        _PHASE_MANIFEST.update(json.loads((FB2_PHASE / 'manifest.json').read_text()))
    return _PHASE_MANIFEST


def same_phase_original(point, port_fbp, port_ofy):
    """The like-for-like original of a point for a port field drawn in
    buffer port_fbp with OFY port_ofy (module docstring): (point, choice).
    The returned point keeps the primary's game-state facts (s1, rec, rows)
    and takes the field, buffer and OFY of the original chosen."""
    name = point['name']
    if (point['fbp'], point['ofy']) == (port_fbp, port_ofy):
        return point, {'set': 'primary', 'why': 'the primary original is on the port field\'s phase'}
    if FB2_PHASE is None:
        return point, {'set': 'primary', 'why': 'no same-phase original in this reference (EM_REFERENCE=v263)'}
    if not (FB2_PHASE / name / 'meta.json').exists():
        return point, {'set': 'primary', 'why': f'no same-phase original of {name} in {FB2_PHASE}'}
    alt = fb2_point(name, FB2_PHASE)
    entry = phase_manifest()['points'][name]
    # The same game state: the capture session's s0..s2 rows equal the
    # primary's on every field but the phase / time ones, and the rand()
    # state at s2 is the primary's.
    prim_rows = [point['rows'][c] for c in sorted(point['rows'])]
    alt_rows = [alt['rows'][c] for c in sorted(alt['rows'])]
    assert len(prim_rows) == len(alt_rows) == 3, (name, 'the capture rows are not s0..s2', len(prim_rows),
                                                  len(alt_rows))
    for k, (a, b) in enumerate(zip(prim_rows, alt_rows)):
        ga, gb = game_row(a), game_row(b)
        assert ga == gb, (name, f'the same-phase original\'s row {k} is not the primary\'s game state',
                          sorted(f for f in set(ga) | set(gb) if ga.get(f) != gb.get(f)))
    assert entry['rows_equal'] and entry['rand_state_s2']['equal'], (name, 'the manifest does not record the '
                                                                     'same rows and rand() state', entry)
    # s1 is the same loop top of the session (the second row), and as many
    # iterations after the recorded snapshot as the primary's
    assert alt['s1'] - min(alt['rows']) == point['s1'] - min(point['rows']) == 1, (name, 's1 is not row 1')
    assert (alt['rec'] is None) == (point['rec'] is None) and \
        (alt['rec'] is None or alt['s1'] - alt['rec'] == point['s1'] - point['rec']), \
        (name, 'the same-phase original is not as many iterations after its snapshot', alt['rec'], point['rec'])
    picked = dict(point, rgb=alt['rgb'], fbp=alt['fbp'], ofy=alt['ofy'], dir=alt['dir'])
    where = f'{FB2_PHASE.name}/{name} (s1 counter {alt["s1"]}, {entry.get("variant")} boot)'
    if (alt['fbp'], alt['ofy']) == (port_fbp, port_ofy):
        return picked, {'set': 'same phase', 'path': where,
                        'why': 'the primary is on the other phase; the same game state on the port\'s buffer and '
                               'half line'}
    if alt['ofy'] == port_ofy:
        # Only the buffer differs: the manifest's checks show the address
        # alone moves no pixel (same tick and half line in 0x0 and 0x38).
        checks = {k: v for k, v in phase_manifest()['checks'].items() if k.endswith('_d7_vs_d4')}
        assert checks and all(v['displayed']['bit_exact'] and v['draw']['bit_exact'] and v['z']['bit_exact']
                              for v in checks.values()), (name, 'no evidence that the buffer address moves no '
                                                          'pixel', sorted(checks))
        return picked, {'set': 'same half line, other buffer', 'path': where,
                        'why': 'no original run reaches the port\'s phase here; the port\'s half line in buffer '
                               f'{hex(alt["fbp"])} (the buffer moves no pixel: checks {", ".join(sorted(checks))})'}
    return point, {'set': 'primary', 'why': f'{FB2_PHASE.name}/{name} is not on the port\'s half line either'}


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


def shown_field(port, capture):
    """The presented host frame at each field pixel's place in the picture
    (EM_GFX_FIELD_INTERLACED, src/gs/em_gs_display.h): field pixel (x, y)
    covers picture pixel x + shift_x and the lines [2y + line + dy,
    2y + line + dy + 2) of the 448 (line 1 for a half-line OFY, dy the
    screen offset's lines); the capture's <path>.present gives the game
    rect's origin and scale and the shift (dy + line). The point read is
    (x + 0.5, 2y + 1 + line / 2) moved by the offset: the span's centre for
    a whole-line field (the point read before 2026-10-09), and a point in
    the first of its two lines for a half-line field, whose last row shows
    only that line (the second is cropped)."""
    h, w, _ = port.shape
    vx, vy, vw, vh = game_rect(w, h)
    present = {}
    for text in Path(str(capture) + '.present').read_text().splitlines():
        key, *values = text.split()
        present[key] = [float(v) for v in values] if key != 'xyoffset' else values
    ox, oy = present['origin']
    sx, sy = present['scale']
    assert (ox, oy) == (vx, vy) and abs(sx * vw - GS_W) < 1e-3 and abs(sy * vh - 448) < 1e-3, \
        (capture, 'the presented game rect is not the 4:3 viewport', present, (vx, vy, vw, vh))
    shx, shy = present['shift']
    line = {0: 0, 8: 1}[(int(present['xyoffset'][0], 16) >> 32) & 0xF]
    xs = np.floor(vx + (np.arange(GS_W) + 0.5 + shx) * vw / GS_W).astype(np.int64)
    ys = np.floor(vy + (2 * np.arange(GS_H) + 1 + (shy - line) + line / 2) * vh / 448).astype(np.int64)
    assert xs.min() >= 0 and xs.max() < w and ys.min() >= 0 and ys.max() < h, 'field pixel outside the target'
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

ticks_tails = {}


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
    # As tools/test_level_smoke.py main: the scene ticks, and the tails
    # (each run's post-frame witnesses) for the alignment's state.
    lines = [json.loads(line) for line in (d / 'ticks.jsonl').open()]
    ticks = [line for line in lines if 'tick' in line]
    ticks_tails[id(ticks)] = [line for line in lines if 'tail' in line]
    return d, run, ticks, time.monotonic() - t0


def alignment(ticks, run, full):
    """{point: port tick index whose post is the point's snapshot row}, from
    the level smoke's own phase checks (their prints are swallowed; their
    assertions still run)."""
    tails = ticks_tails.get(id(ticks), [])
    state = {'drive': T.R.drive_mode(run), 'status_pages_trace': None,
             'tail': tails[-1] if tails else None, 'tails': tails}
    with contextlib.redirect_stdout(io.StringIO()):
        if full:
            for name, check in T.PHASES:
                if re.search(rf'^level smoke: {name}: PASS', run, re.M):
                    check(ticks, run, state)
        else:
            T.check_first_control(ticks, run, state)
    base = {beat: i for beat, i in state.get('snapshots', [])}
    base['first_control'] = state.get('first_control')
    # The AREA01 point: the tick whose post is route 15's last row f801,
    # the end of the a01_arrival phase (level_smoke_area01.check_arrival).
    end = state.get('area01_ends', {}).get('a01_arrival')
    if end is not None:
        base['15_level_exit'] = max(i for i, t in enumerate(ticks) if t.get('counter') == end)
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
    if row is None and point['s1'] in point['rows']:
        row = ('capture.json', point['rows'][point['s1']])
    exact, diff = placement(ticks, tick_index, row[1]) if row else (None, 'no recorded row at s1')
    gs = gs_field(capture, run)
    assert (gs is None) == GPU, (name, 'the GS field is missing' if gs is None else
                                 'a GS field was written with EM_GPU_RENDERER=1')
    phase = None
    orig = point
    reference = {'set': 'primary', 'why': 'the GPU renderer draws no field (no field phase); sampled at the '
                                          'primary\'s OFY'}
    if gs is not None:
        field, frame, xyoffset = gs
        port_ofy = ((xyoffset >> 32) & 0xFFFF) / 16.0
        # the field's buffer and half line against the primary original's at
        # s1 (the frame loop's phase, RENDER_CONTEXT.md 9.3): the timing item,
        # reported and not asserted (the port runs the disc at host speed)
        phase = {'port_fbp': hex(frame & 0x1FF), 'original_fbp': hex(point['fbp']), 'port_ofy': port_ofy,
                 'original_ofy': point['ofy'], 'same': frame & 0x1FF == point['fbp'] and port_ofy == point['ofy']}
        # the pixels are compared with the original drawn on the port's phase
        orig, reference = same_phase_original(point, frame & 0x1FF, port_ofy)
        # what is shown: the presented frame (the field under the GPU's 2D
        # overlay pass: the letterbox bars, the transition fade, the message
        # glyphs) read back at the centre of each field pixel's place in the
        # picture, where the presentation shows field pixel (x, y) unchanged
        presented, geometry = shown_field(read_bmp(capture), capture)
        bare = field
        field = presented
    else:
        port_rgb = read_bmp(capture)
        field, geometry = sample_field(port_rgb, point['ofy'])
        bare = None
    reference.update(fbp=hex(orig['fbp']), ofy=orig['ofy'], dir=str(orig['dir']))
    m, per = metrics(field, orig['rgb'])
    d = OUT / name
    d.mkdir(parents=True, exist_ok=True)
    write_png(d / 'port_512x224.png', field)
    write_png(d / 'original_512x224.png', orig['rgb'])
    write_png(d / 'diff_512x224.png', diff_image(per))
    bare_metrics = None
    if bare is not None:
        bare_metrics, bper = metrics(bare, orig['rgb'])
        write_png(d / 'port_gs_field_512x224.png', bare)
        write_png(d / 'diff_gs_field_512x224.png', diff_image(bper))
    result = dict(m, point=name, port_tick=t['tick'], s1=point['s1'], ofy=orig['ofy'],
                  port_frame='GS field' if gs is not None else 'Metal (GPU renderer)', field_phase=phase,
                  reference=reference,
                  gs_field_alone=bare_metrics,
                  row=(f'{row[0]} f{row[1]["f"]}' if 'f' in row[1] else f'{row[0]} counter {point["s1"]}')
                  if row else None, camera_exact=exact,
                  differs=diff if not exact else None, target=list(geometry[:2]),
                  game_rect=[round(v, 3) for v in geometry[2:]])
    (d / 'result.json').write_text(json.dumps(result, indent=1))
    return result


def line(r):
    cam = ('camera exact' if r['camera_exact'] else
           f'camera NOT exact ({", ".join(sorted(r["differs"]))} differ)' if r['camera_exact'] is False else
           r['differs'])
    ref = r['reference']
    cam += (f'; against the {ref["set"]} original' + (f' {ref["path"]}' if ref.get('path') else '') +
            f' (FBP {ref["fbp"]}, OFY {ref["ofy"]}' +
            ('' if (r.get('field_phase') or {}).get('same') else f': {ref["why"]}') + ')')
    g = r.get('gs_field_alone')
    if g:
        cam += (f'; the GS field alone (no overlay pass): exact {g["exact"]:,} '
                f'({100 * g["exact_fraction"]:.2f}%), mean channel error {g["mean_abs_channel_error"]:.2f}')
    return (f'{r["point"]}: port tick {r["port_tick"]} = row {r["row"]} (s1 {r["s1"]}, OFY {r["ofy"]}), {cam}: '
            f'exact {r["exact"]:,} of {r["pixels"]:,} ({100 * r["exact_fraction"]:.2f}%), mean channel error '
            f'{r["mean_abs_channel_error"]:.2f}, max {r["max_channel_error"]}, per-pixel max error p50/p90/p99 '
            f'{r["p50"]}/{r["p90"]}/{r["p99"]}')


# The original's rule after Roger's departure FMV (decomp PCSX2_FORK.md,
# fb2_phase manifest "findings"): the FMV's iteration ends with D_00810E80 ==
# D_00810E88, so at 15_level_exit the original draws buffer 0x0 with OFY
# 1936.5 or 0x38 with 1936.0, never the other two.
FMV_RULE = {(0x0, 1936.5), (0x38, 1936.0)}


def timing_line(r):
    """The field phase against the primary original (the timing item), or
    None when it is the same."""
    ph = r.get('field_phase')
    if not ph or ph['same']:
        return None
    what = ('buffer and half line' if ph['port_fbp'] != ph['original_fbp'] and ph['port_ofy'] != ph['original_ofy']
            else 'half line' if ph['port_ofy'] != ph['original_ofy'] else 'buffer')
    text = (f'{r["point"]}: the port field is FBP {ph["port_fbp"]} OFY {ph["port_ofy"]}, the route\'s original '
            f'FBP {ph["original_fbp"]} OFY {ph["original_ofy"]} (the other {what}): the field phase, set by the '
            'disc\'s timing at loads, which the port runs at host speed (FIRST_LEVEL_AUDIT.md item 2, 1b F1)')
    if r['point'] == '15_level_exit' and (int(ph['port_fbp'], 16), ph['port_ofy']) not in FMV_RULE:
        text += ('; open finding: after Roger\'s departure FMV the original always has D_00810E80 == '
                 'D_00810E88, and this field breaks that rule')
    return text


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


def area01_point(points):
    """15_level_exit from one run to the AREA01 arrival (a01_arrival + 2)."""
    p = points['15_level_exit']
    # The capture session ran neutral-pad frames after route 15's last row
    # (f809 on the fork, f801 on v2.6.3), as the port's idle does: the idle
    # is lengthened by s1 - rec ticks (EM_A01_ARRIVAL_TICKS; the a01_arrival
    # check still compares its 60 world rows) so that it ends on row
    # s1's tick, and the hook (which runs inside the phase's last tick, before
    # that tick is logged) captures that tick (+1). The assertion below checks
    # the captured tick is the aligned one.
    cap = OUT / 'a01' / '15_level_exit.bmp'
    d, run, ticks, secs = run_port('a01', 'a01_arrival', {
        'EM_A01_ARRIVAL_TICKS': str(60 + p['s1'] - p['rec']), 'EM_LEVEL_SMOKE_FB_CAPTURE': f'a01_arrival+1:{cap}'})
    base = alignment(ticks, run, True)
    want = target_tick(p, base)
    m = re.search(r'^fb capture: tick (\d+) -> ', run, re.M)
    assert m and cap.exists(), 'the port wrote no capture'
    assert int(m.group(1)) == ticks[want]['tick'], ('15_level_exit: the captured tick is not the aligned tick',
                                                     int(m.group(1)), ticks[want]['tick'])
    return compare(p, want, ticks, cap, run), secs


def main():
    assert BIN.exists(), 'build/extermination is missing (make all)'
    if not FB2.exists():
        print(f'fb2 pixels: SKIP: {FB2} is missing (the decomp\'s C7 capture, CAPTURES_C7.md 5b)')
        return
    OUT.mkdir(parents=True, exist_ok=True)
    if sys.argv[1:] == ['--point', '15_level_exit']:
        r, secs = area01_point({'15_level_exit': fb2_point('15_level_exit')})
        print('fb2 pixels: ' + line(r))
        if timing_line(r):
            print('fb2 pixels: timing: ' + timing_line(r))
        for f in (list((OUT / 'a01').glob('ticks.jsonl')) + list((OUT / 'a01').glob('*.bmp')) +
                  list((OUT / 'a01').glob('*.bmp.gsfield'))):
            f.unlink()
        floor = (FLOORS_GPU if GPU else FLOORS)[r['point']]
        assert r['exact_fraction'] >= floor, (r['point'], 'exact-match fraction below its floor',
                                              r['exact_fraction'], floor)
        print(f'fb2 pixels: PASS (one port run to the AREA01 arrival ({secs:.0f} s); images in '
              f'{OUT.relative_to(ROOT)}/15_level_exit/)')
        return
    assert not sys.argv[1:], 'usage: test_fb2_pixels.py [--point 15_level_exit]'
    points = {name: fb2_point(name) for name in (POINTS if RM.FULL else ('first_control',))}
    results, listed = [], []
    first, s0 = quick_point(points)
    results.append(first)
    runs = f'one port run to first control ({s0:.0f} s)'
    if RM.FULL:
        # The AREA01 idle runs on to 15_level_exit's row s1 (area01_point).
        a01 = points['15_level_exit']
        idle = {'EM_A01_ARRIVAL_TICKS': str(60 + a01['s1'] - a01['rec'])}
        d1, run1, ticks1, s1 = run_port('pass1', 'a01_arrival', idle)
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
        d2, run2, ticks2, s2 = run_port('pass2', 'a01_arrival', dict(idle, EM_FB_CAPTURE_TICKS=spec))
        base2 = alignment(ticks2, run2, True)
        for name, i in wanted.items():
            j = target_tick(points[name], base2)
            assert j == i and ticks2[j]['tick'] == ticks1[i]['tick'], (name, 'the port run is not deterministic: '
                                                                       'pass 2 aligns another tick', i, j)
            assert ticks2[j]['post'] == ticks1[i]['post'], (name, 'pass 2 state differs at the aligned tick')
            results.append(compare(points[name], j, ticks2, OUT / 'pass2' / (name + '.bmp'), run2))
        runs += f', two port runs through the AREA01 arrival ({s1:.0f} s, {s2:.0f} s)'
    for r in results:
        print('fb2 pixels: ' + line(r))
    for r in results:
        if timing_line(r):
            print('fb2 pixels: timing: ' + timing_line(r))
    for name, why in listed:
        print(f'fb2 pixels: {name}: not compared: {why}')
    (OUT / 'summary.json').write_text(json.dumps({'mode': RM.MODE, 'reference': refs.MODE, 'results': results,
                                                  'timing': {r['point']: timing_line(r) for r in results
                                                             if timing_line(r)},
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
