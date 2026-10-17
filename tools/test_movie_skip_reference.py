#!/usr/bin/env python3
"""The New Game movie skipped with START held from its start, against the
original on the PCSX2 fork (make test-movie-field; about 2 s).

The comparison video (the decomp's tools/video_compare/) holds START over
the whole New Game movie on both sides: the port's recording has START on
every step of the movie (logs/make_demo_level_rec.py; EM_NEW_GAME=1 holds it
too), the fork side sets START as the raw pad at a stop probe on the movie
driver 00203350's entry (ps2_fork.py run_to). The fork's receipt
(decomp build/fork_refs/probes_1016/skip/d0_start_1016c, made by
probes_1016/tools/probe_skip.py: the canonical boot, the movie entered on
D_00810E80 = 1, START held from the driver's entry, released at the next
loop top; inline log probes and tables, no memory written) gives the vblank
of each event. The port's run is EM_NEW_GAME=1 (the movie entered on 1 as
well) to the AREA11 opening's first frame with its tick log.

Asserted, every count of vblanks taken from the gate (002061B0(1)'s
return): the fork's disc and IOP set-up before the gate (25 vblanks from
the driver's entry here) is emulated time the port runs at host speed
(GS_EXACT.md 10.4) and is reported, not compared.
  - the gate opens on a field-0 vblank (the fork's GS CSR there);
  - pictures: the fork's field-0 and field-1 sends (00206030) are 12 each,
    the port shows pictures 0..11 (12 displayed);
  - 00206B70 (the skip) is called with the decoder's count D_002DF740 +8 at
    11 on the fork; the port honours it at that count (em_movie_field.h).
    The fork's call came at gate + 22, the port's at gate + 19 (when the
    count reaches 11 at host speed): the feed loop tests the pad once per
    pass, between its reads, which the fork runs at emulated disc and IOP
    speed. The outcome is the same: no other picture is decoded;
  - the play loop ends in the vblank of the last field-1 send: gate + 25 on
    both; 00203350 returns after its two closing waits, gate + 27, on a
    field-1 vblank: the port's last step and its P field;
  - the loop tops after the movie: D_00810E80 0 and D_00810E88 1 (the
    relation the movie's entry on 1 leaves), the scratchpad bytes
    0x70003B8C..93 and D_00810700..702 equal to the fork's for six
    iterations, the area byte set two iterations after the movie on both.
"""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import reference_root as refs  # noqa: E402
import test_scene_task_reference as tsr  # noqa: E402

BIN = os.environ.get('EM_BIN', 'build/extermination')
RECEIPT = refs.FORK / 'probes_1016' / 'skip' / 'd0_start_1016c'
AFTER = 6
# the probe sites (decomp probes_1016/tools/probe_movie.py, analyze_movie.py NAMES)
GATE_RET, LOOP_EXIT, DRIVER_ENTRY, DRIVER_RET, SYNC4_RET = 0x20387C, 0x2038B8, 0x203350, 0x1AAFDC, 0x203438


def read_table(path):
    """The fork's table format (PXTBL001: the fork's FORMATS.md)."""
    b = path.read_bytes()
    assert b[:8] == b'PXTBL001', path
    size = struct.unpack_from('<I', b, 12)[0]
    spans = json.loads(Path(str(path) + '.json').read_text())['spans']
    return [{'vsync': struct.unpack_from('<I', b, i + 4)[0],
             **{s['name']: b[i + s['offset']:i + s['offset'] + s['len']] for s in spans}}
            for i in range(256, len(b) - size + 1, size)]


def read_trace(path):
    """The fork's call trace (64-byte records): (vsync, pc, v0)."""
    b = path.read_bytes()
    return [(struct.unpack_from('<I', b, i + 4)[0], struct.unpack_from('<I', b, i + 24)[0],
             struct.unpack_from('<I', b, i + 52)[0]) for i in range(0, len(b) - 63, 64)]


def le(raw, n=4):
    return int.from_bytes(raw[:n], 'little')


def fork_facts():
    tr = read_trace(RECEIPT / 'sends.trace')
    vt = {r['vsync']: r for r in read_table(RECEIPT / 'vsync.tbl')}
    first = lambda pc: next(v for v, p, _ in tr if p == pc)  # noqa: E731
    entry, gate, end, ret = first(DRIVER_ENTRY), first(GATE_RET), first(LOOP_EXIT), first(DRIVER_RET)
    s0, s1 = read_table(RECEIPT / 'send0.tbl'), read_table(RECEIPT / 'send1.tbl')
    skip = read_table(RECEIPT / 'skip_call.tbl')
    csr_field = lambda v: (le(vt[v]['csr'], 8) >> 13) & 1  # noqa: E731
    ticks = sorted(read_table(RECEIPT / 'tick.tbl'), key=lambda r: le(r['counter']))
    k = next(i for i in range(1, len(ticks))      # the first loop top after the movie (the run's one)
             if le(ticks[i]['vs']) - le(ticks[i - 1]['vs']) > 1)
    tops = [{'fi': le(r['fi'], 2), 'fld': le(r['fld'], 2), 'spad': r['spad'].hex(), 'area': r['task0'][:3].hex()}
            for r in ticks[k:k + AFTER]]
    return {'entry_fi': le(ticks[k - 1]['fi'], 2), 'setup': gate - entry, 'gate_field': csr_field(gate),
            'sends': (len(s0), len(s1)), 'first_send': min(r['vsync'] for r in s0) - gate,
            'skip_calls': [(r['vsync'] - gate, le(r['decoded'])) for r in skip],
            'end': end - gate, 'last_send1': max(r['vsync'] for r in s1) - gate, 'ret': ret - gate,
            'ret_field': csr_field(ret), 'sync4_v0': next(v0 for v, p, v0 in tr if p == SYNC4_RET), 'tops': tops}


def port_facts(out):
    env = {k: v for k, v in os.environ.items() if not k.startswith('EM_')}
    env.update(EM_HEADLESS='1', EM_UNCAPPED='1', EM_NEW_GAME='1', EM_NEW_GAME_STATE_TEST=str(out / 'state.img'),
               EM_AREA_CHANGE_LOG=str(out / 'ticks.jsonl'))
    p = subprocess.run([BIN], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
    (out / 'run.log').write_text(p.stdout)
    assert p.returncode == 0, ('the port run', p.returncode, p.stdout[-2000:])
    b = re.findall(r'^startup: movie 0 \(E900\.PSS\) for the game task, D_00810E80 (\d)', p.stdout, re.M)
    e = re.findall(r'^startup: movie (\w+), (\d+) displayed frames, .*; (\d+) steps \(gate at vblank (\d+), the play '
                   r'loop\'s end at (\d+), P field (\d)\)', p.stdout, re.M)
    assert len(b) == 1 and len(e) == 1, ('one New Game movie', b, e)
    outcome, shown, steps, gate, end, pfield = e[0][0], *(int(x) for x in e[0][1:])
    ticks = [json.loads(line) for line in (out / 'ticks.jsonl').open() if '"tick"' in line]
    k = next(i for i in range(1, len(ticks)) if ticks[i]['vs'] - ticks[i - 1]['vs'] > 1)
    off = tsr.OFFSET[0x70003B8C], tsr.OFFSET[0x810700]
    tops = []
    for t in ticks[k:k + AFTER]:
        pre = bytes.fromhex(t['pre'])
        tops.append({'fi': t['fld'][0], 'fld': t['fld'][1], 'spad': pre[off[0]:off[0] + 8].hex(),
                     'area': pre[off[1]:off[1] + 3].hex()})
    assert ticks[k]['vs'] - ticks[k - 1]['vs'] == steps, ('the movie iteration\'s vblanks', steps)
    return {'entry_fi': int(b[0]), 'outcome': outcome, 'shown': shown, 'end': end - gate, 'ret': steps - 1 - gate,
            'gate': gate, 'pfield': pfield, 'tops': tops}


def main():
    if not (RECEIPT / 'sends.trace').exists():
        print(f'movie skip reference: SKIP: {RECEIPT} is missing (decomp probes_1016/tools/probe_skip.py)')
        return
    f = fork_facts()
    with tempfile.TemporaryDirectory(prefix='movie_skip_ref_', dir='build') as tmp:
        p = port_facts(Path(tmp))
    where = ('fork', RECEIPT.name)
    assert f['entry_fi'] == 1 == p['entry_fi'], (where, 'the movie entered on D_00810E80 = 1', f['entry_fi'],
                                                 p['entry_fi'])
    assert f['gate_field'] == 0, (where, 'the gate opens on a field-0 vblank', f['gate_field'])
    assert f['sends'] == (12, 12) and p['outcome'] == 'skipped' and p['shown'] == 12, (
        'the pictures shown: the fork\'s sends against the port\'s pictures 0..11', f['sends'], p['shown'])
    assert f['first_send'] == 2 and len(f['skip_calls']) == 1 and f['skip_calls'][0][1] == 11, (
        where, 'the first send and the skip at the decoder\'s count 11', f['first_send'], f['skip_calls'])
    assert f['end'] == f['last_send1'] == 25 == p['end'], ('the play loop\'s end after the gate', f['end'],
                                                            f['last_send1'], p['end'])
    assert f['ret'] == 27 == p['ret'] and f['ret_field'] == 1 == p['pfield'] and f['sync4_v0'] == 1, (
        'the return after the gate and its field', f['ret'], p['ret'], f['ret_field'], p['pfield'])
    assert len(f['tops']) == len(p['tops']) == AFTER, ('the loop tops after the movie', f['tops'], p['tops'])
    for k, (a, b) in enumerate(zip(f['tops'], p['tops'])):
        assert a == b, ('the loop top', k + 1, 'after the movie: fork', a, 'port', b)
    assert f['tops'][0]['fi'] == 0 and f['tops'][0]['fld'] == 1, ('the relation after the movie', f['tops'][0])
    area = next(k for k, t in enumerate(p['tops']) if t['area'] != '000000')
    print(f'movie skip reference: START held from the movie\'s start, entered on D_00810E80 = 1; the gate on a '
          f'field-0 vblank; 12 pictures (fork sends 12 + 12); the fork\'s skip at the decoder\'s count 11 (gate + '
          f'{f["skip_calls"][0][0]}); the play loop\'s end at gate + 25 and the return at gate + 27 on field 1 on '
          f'both; the {AFTER} loop tops after it equal (D_00810E80 / D_00810E88, 0x70003B8C..93, D_00810700..702; '
          f'the area byte at loop top {area + 1}); not compared: the fork\'s {f["setup"]} vblanks from the driver\'s '
          f'entry to the gate (emulated disc and IOP set-up), the port\'s {p["gate"]}')
    print('movie skip reference: PASS')


if __name__ == '__main__':
    main()
