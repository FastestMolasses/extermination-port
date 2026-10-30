#!/usr/bin/env python3
"""Record the comparison-only drive replay tables on the PCSX2 fork.

For each recorded route beat (the decomp's build/fork_refs/s87/route_*),
re-drive the beat on the fork from its source state with its recorded
inputs (the decomp's tools/route_capture.py, hidden, under the run lock)
and sample, every field, the main-loop counter 0x70003B64 and the stream
lanes' read sequencer 001FA0D0 (decomp src/func_001FA0D0.c, NEARMISS C:
phase D_00282157, lane D_00282158; the lane records at D_00281FD0, stride
0x60, sector +0x30, count +0x34, state byte +3). A read is issued in the
field whose sample first shows phase 2; its seek fields are the following
fields still in phase 2 (001FA0D0 polls 00112D18 once per field), ending
when 001FA0D0 marks the lane's byte 2 (done). A phase-2 exit without that
mark (a break, 00113478) is not a measured seek and is left out.

Output (one NEW folder; disc-derived timing data, never committed):
  <out>/<beat>.reads   lines "counter sector count fields" (counter: the
                       sample's 0x70003B64 in the issue field)
  <out>/receipt.json   per beat: rows, reads, omitted, replay differences

The port reads a merged table through the developer switch
EM_PS2_DRIVE_REPLAY (docs/IOP_STREAM.md "Drive replay"); the level 2-4
harnesses merge the tables of the beats on the path they run.
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

DECOMP = Path('/Users/abe/Documents/Extermination.nosync/Extermination')
sys.path.insert(0, str(DECOMP / 'tools'))

LANES, LANE_STRIDE, SEQ = 0x281FD0, 0x60, 0x282150
EXTEND_MAX = 40   # fields sampled past a beat's last row for a read still in flight


def sample(s):
    counter = struct.unpack('<I', s.read(0x70003B64, 4))[0]
    return counter, s.read(SEQ, 0x40), s.read(LANES, LANE_STRIDE * 3)


def extract(samples, first_issue_index):
    """Reads issued between samples k-1 and k for k >= first_issue_index."""
    reads, omitted = [], []
    for k in range(max(1, first_issue_index), len(samples)):
        _, a, _ = samples[k - 1]
        counter, b, lanes = samples[k]
        if not (a[7] == 1 and b[7] == 2):
            continue
        lane = b[8]
        sector, count = struct.unpack_from('<ii', lanes, LANE_STRIDE * lane + 0x30)
        j = k + 1
        while j < len(samples) and samples[j][1][7] == 2 and samples[j][1][8] == lane:
            j += 1
        if j >= len(samples):
            omitted.append(dict(counter=counter, sector=sector, count=count, why='in flight at the end'))
            continue
        if samples[j][2][LANE_STRIDE * lane + 3] != 2:
            omitted.append(dict(counter=counter, sector=sector, count=count, why='phase 2 left without done'))
            continue
        reads.append((counter, sector, count, j - k - 1))
    return reads, omitted


def beat_path(rc, beat, branch):
    """The beat's folder: the canonical fork set, or a recording branch's
    copy (fork_refs/<branch>/s87/...: the bed branch's continuation)."""
    d = rc.beat_dir(beat)
    return Path(str(d).replace('/fork_refs/s87/', f'/fork_refs/{branch}/s87/')) if branch else d


def record_beat(rc, beat, out, branch=None):
    trace = json.loads((beat_path(rc, beat, branch) / 'trace.json').read_text())
    src = Path(trace['source_state'])
    lead = int(trace.get('lead_in_frames') or 0)
    rows = trace['rows']
    by_f = {}
    for v in trace['inputs']:
        by_f.setdefault(v['f'], []).append(v)
    samples, differ = [], []
    with rc.open_session(src, log_dir=out / 'logs' / beat) as s:
        samples.append(sample(s))                    # the source state
        for _ in range(lead):
            s.step(1)
            samples.append(sample(s))
        r = rc.Route(s)
        r.pad_fix = rc.v263_pad_corrections(beat)
        r.begin()
        first = len(samples) - 1                     # row 0 is the last lead-in sample
        if samples[first][0] != rows[0]['counter']:
            differ.append((0, samples[first][0], rows[0]['counter']))
        for f in range(len(rows) - 1):
            for v in by_f.get(f, []):
                r.set_pad(v['buttons'], v['lx'], v['ly'])
            row = r.step(1)
            samples.append(sample(s))
            want = rows[f + 1]
            if row['counter'] != want['counter'] or row['msg'] != want['msg'] or \
               any(abs(x - y) > 1e-3 for x, y in zip(row['pos'], want['pos'])):
                differ.append((f + 1, row['counter'], want['counter']))
        # a read still in flight at the last row: keep the last pad and step on
        # until 001FA0D0 leaves phase 2 (the drive's time does not depend on it)
        for _ in range(EXTEND_MAX):
            if samples[-1][1][7] != 2:
                break
            r.step(1)
            samples.append(sample(s))
    last_row_index = first + len(rows) - 1
    reads, omitted = extract(samples, 1)
    reads = [x for x in reads if x[0] <= samples[last_row_index][0]]
    # A re-drive that did not reproduce the recording measured another run:
    # its table is kept beside, under a name no harness reads.
    name = f'{beat}.reads' if not differ else f'{beat}.reads.diverged'
    with (out / name).open('w') as fh:
        fh.write(f'# {beat}: counter sector count fields (fork recording; comparison only)\n')
        for x in reads:
            fh.write('%d %d %d %d\n' % x)
    return dict(beat=beat, source_state=str(src), lead_in=lead, rows=len(rows),
                first_counter=rows[0]['counter'], last_counter=rows[-1]['counter'],
                reads=len(reads), omitted=omitted, replay_differences=len(differ),
                first_differences=differ[:5],
                fields=sorted({x[3] for x in reads}))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True, help='a NEW folder under build/')
    ap.add_argument('--branch', help='a recording branch under fork_refs (default: the canonical s87 set)')
    ap.add_argument('beats', nargs='+', help='full beat names (e.g. a01_03_shaft_locked)')
    a = ap.parse_args()
    import route_capture as rc
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    receipt = dict(tool='tools/record_drive_replay.py', started=time.strftime('%Y-%m-%d %H:%M:%S'), beats=[])
    for beat in a.beats:
        t0 = time.monotonic()
        info = record_beat(rc, beat, out, a.branch)
        info['seconds'] = round(time.monotonic() - t0, 1)
        receipt['beats'].append(info)
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(f"{beat}: {info['rows']} rows, {info['reads']} reads (fields {info['fields']}), "
              f"{len(info['omitted'])} omitted, {info['replay_differences']} replay differences, "
              f"{info['seconds']} s", flush=True)
    bad = sum(b['replay_differences'] for b in receipt['beats'])
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
