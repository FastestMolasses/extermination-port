#!/usr/bin/env python3
"""The gun lamp live outside AREA11, headless: a loaded game, the lamp
switched on, the cell record's quadword and the cone shells (docs/AIM_FIRE.md
section 10.8).

Where D_008106C8 lacks 0x20000000 (every AREA01, AREA00 and AREA02 route
capture: 0x8D00 / 0x8D01 / 0x0D00), 00187780 (decomp src/func_00187780.c,
NEARMISS) draws the cone shells 001D9530 (byte-matched) after the flare, and
its probe 0019A570(.., 6, 0) can name the cell record D_700030B0, whose
+0x24 the lamp copies with 00102948: an LQ of the aligned quadword
0x700030D0..0x700030DF (the record's +0x20 word and its normal). Before
this test both fail-stopped the game task (em_scene: FAULT at 00187780):
the composition had no 16-byte view of 0x700030D0, and 001D9530 was not
composed.

Each run: EM_START_AT=<point> (the original LOAD GAME path) with an
EM_START_AT_PAD script from control: past the post-load lock, R1 (draw),
Square (the lamp on), the stick left then up while aiming, Circle (a
few rounds), L3 (the reload), Square twice (off, on), release. Checked: no
fault and the script's end; the lamp's cone pages (EM_LAMP_PAGE_DUMP, the
pages that ran the level kernel 00237180) stop while the lamp is off and
come back; the cell record's quadword was read (area01: the probe named
D_700030B0); and the first cone page (EM_TEST_FULL=1: the first three)
walked again by
tools/chain_page_model.py, which executes the ORIGINAL microcode the page
uploads (the level kernel 00237180 and the box clip kernel 00239C90) over
the port's own page bytes, draw exactly the port's primitives (the digest,
the count) with the same DIRECT, XGKICK and MSCAL counts. The EE side of
the lamp is test-aim-fire-lamp-reference's (the original instructions).

Quick: area01, area01-revisit and area02 in parallel (about 10 s);
EM_TEST_FULL=1 adds area00 and a02_03. Needs make start-saves, and the
effect table and Roger bank exports that hold the cone's data (STARTUP.md).
"""
import json
import os
from pathlib import Path
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, str(Path(__file__).resolve().parent))
import chain_page_model as M  # noqa: E402
from level_smoke_chain_page import digest  # noqa: E402
import reference_mode  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'build/extermination'
SAVES = ROOT / 'build/start_saves'
OUT = ROOT / 'build/lamp_cone'
LOCK = 100          # after a load the player takes no input for the post-control lock
R1, SQUARE, CIRCLE, L3 = 0x800, 0x8000, 0x2000, 0x2
QUICK = ('area01', 'area01-revisit', 'area02')
FULL = QUICK + ('area00', 'a02_03')


def pad_script():
    """EMA1 rows (frame, canonical EM_PAD buttons, lx, ly) and the frames
    the lamp is off (between the two Square presses)."""
    d = LOCK
    rows = [(0, 0, 127, 127), (d, R1, 127, 127), (d + 30, R1 | SQUARE, 127, 127), (d + 34, R1, 127, 127)]
    f = d + 50
    # the stick: left (in area01 the lamp's probe names the cell record
    # from about 16 frames on), then up
    for x, y, n in ((0, 127, 60), (127, 0, 40)):
        rows.append((f, R1, x, y))
        f += n
    rows.append((f, R1, 127, 127))
    for k in range(4):                                    # a few rounds, then the reload
        rows += [(f + 10 + 16 * k, R1 | CIRCLE, 127, 127), (f + 14 + 16 * k, R1, 127, 127)]
    f += 80
    rows += [(f, R1 | L3, 127, 127), (f + 4, R1, 127, 127)]
    f += 90                                               # the reload's animation takes no Square
    off = f
    rows += [(f, R1 | SQUARE, 127, 127), (f + 4, R1, 127, 127)]
    f += 40
    on = f
    rows += [(f, R1 | SQUARE, 127, 127), (f + 4, R1, 127, 127)]
    f += 40
    rows.append((f, 0, 127, 127))
    text = '\n'.join([f'EMA1 1 {f + 30} 1'] + [f'{a} {b:x} {x} {y}' for a, b, x, y in rows]) + '\n'
    return text, off, on


def run(point):
    out = OUT / point
    out.mkdir(parents=True, exist_ok=True)
    text, off, on = pad_script()
    pad = out / 'lamp.pad'
    pad.write_text(text)
    dump = out / 'pages.jsonl'
    if dump.exists():
        dump.unlink()
    env = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_START_AT=point, EM_START_AT_PAD=str(pad), EM_LAMP_PAGE_DUMP=str(dump),
               EM_HEADLESS='1', EM_MUTE='1', EM_UNCAPPED='1')
    p = subprocess.Popen([str(BIN)], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    lines, control = [], None
    try:
        for line in p.stdout:
            line = line.rstrip('\n')
            lines.append(line)
            if 'EM_START_AT_PAD:' in line and 'rows over' in line:
                control = int(line.rsplit('engine frame ', 1)[1])
            # the game runs on after the script (a neutral pad): stop it there
            if 'EM_START_AT_PAD: the script ended' in line or 'the game task is stopped' in line:
                break
    finally:
        p.kill()
        p.wait()
    (out / 'run.log').write_text('\n'.join(lines) + '\n')
    pages = [json.loads(x) for x in dump.read_text().splitlines()] if dump.exists() else []
    return lines, pages, control, off, on


def rewalk(page):
    mem = [(a, bytes.fromhex(h)) for a, h in page['reads']]

    def read(a, n):
        for base, b in mem:
            if base <= a and a + n <= base + len(b):
                return b[a - base:a - base + n]
        raise M.ModelError(f'the dumped page has no bytes at {a:#x}+{n:#x}')

    units = {u[0]: tuple(u[1:]) for u in page['units']}
    if page['pass']:
        model = M.Page(read, units=units, pass_calls=page['targets'], draw_envs=page['draw_envs'],
                       draw_env_slot=page['slot'])
    else:
        model = M.Page(read, skip_calls=page['targets'], units=units)
    model.run(page['start'])
    return (digest(model.gs.prims), len(model.gs.prims), len(model.directs), len(model.kicks),
            model.mscal_counts.get(M.PROGRAM_LEVEL, 0), model.mscal_counts.get(M.PROGRAM_BOX_CLIP, 0))


def check(point):
    lines, pages, control, off, on = run(point)
    problems = []
    faults = [x for x in lines if 'FAULT' in x or 'faulted' in x or x.startswith('chain page:')]
    if faults:
        problems.append('faulted: ' + ' | '.join(faults[:3]))
    if not any('EM_START_AT_PAD: the script ended' in x for x in lines):
        problems.append('the pad script did not end')
    full = [p for p in pages if 'reads' in p]
    if not full:
        problems.append('no page ran the cone shells (00237180)')
        return point, problems, 0, 0
    if control is not None:
        frames = [p['frame'] - control for p in pages]
        # Square's press reaches the stance two frames later; the cone stops
        # within a few frames of the press and comes back after the next one.
        gap = [f for f in frames if off + 8 <= f < on]
        after = [f for f in frames if f >= on + 8]
        if gap:
            problems.append(f'cone pages while the lamp is off ({len(gap)})')
        if not after:
            problems.append('no cone page after the lamp came back on')
    cells = max(p.get('cell_quadwords', 0) for p in pages)
    if point == 'area01' and not cells:
        problems.append('the probe never named the cell record (0x700030D0 not read)')
    # quick: the first cone page; full: the first three
    for p in full if reference_mode.FULL else full[:1]:
        try:
            want = rewalk(p)
        except M.ModelError as e:
            problems.append(f'frame {p["frame"]}: the model refused the page: {e}')
            continue
        got = (p['digest'], p['prims'], p['directs'], p['kicks'], p['mscal_level'], p['mscal_box_clip'])
        if want != got:
            problems.append(f'frame {p["frame"]}: the port drew {got}, the original microcode {want}')
    return point, problems, len(pages), cells


def main():
    if not BIN.exists():
        sys.exit(f'{BIN} is missing (make)')
    points = FULL if reference_mode.FULL else QUICK
    for point in points:
        if not (SAVES / point / 'start.json').exists():
            sys.exit(f'{SAVES / point} is missing (make start-saves)')
    print(f'lamp cone: mode {reference_mode.MODE}: {len(points)} loaded games')
    with ThreadPoolExecutor(max_workers=len(points)) as pool:
        results = list(pool.map(check, points))
    failed = 0
    for point, problems, npages, cells in results:
        print(f'  {point}: {"FAIL" if problems else "ok"} ({npages} cone pages, 0x700030D0 read {cells} times)')
        for p in problems:
            print(f'    {p}')
        failed += bool(problems)
    print('lamp cone: ' + ('FAIL' if failed else 'PASS'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
