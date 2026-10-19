"""The comparison-only drive replay for the level 2-4 harnesses.

The PS2 drive's seek time varies read by read in the fork recordings; the
port's drive model serves each distance class one value (docs/IOP_STREAM.md
"Drive model"). Under the developer switch EM_PS2_DRIVE_REPLAY (user
decision 2026-10-19; never a player or launcher option) each read takes the
seek fields the fork recording measured for it (IOP_STREAM.md "Drive
replay"). The per-beat tables are recorded on the fork from the user's own
disc by tools/record_drive_replay.py into ignored build/drive_replay/ (disc-
derived timing data: never committed). A harness merges the tables of the
phases on its path into one file with an anchor per phase ("@ phase
first_counter": the recording's main-loop counter at the phase's row 0;
the level smoke maps the run's counter onto it when it aligns that row).
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DIR = ROOT / 'build/drive_replay'
REPORT = re.compile(r'^stream drive: replay: (\d+) of (\d+) recorded reads served \((\d+) of them other than '
                    r'the model, (\d+) at a counter other than the recording\'s\), (\d+) reads with no recorded entry \(the model\), (\d+) of them inside the '
                    r'recorded span \(last counter (\d+) sector (\d+)\), (\d+) entries not reached$', re.M)


def read_beat(path):
    reads = []
    for line in Path(path).read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        counter, sector, count, fields = map(int, line.split())
        reads.append((counter, sector, count, fields))
    return reads


def find_beat(table_dir, beat):
    """<beat>.reads in table_dir or one of its recording folders (each
    record_drive_replay.py run writes a new one); copies must agree."""
    table_dir = Path(table_dir)
    found = sorted({*table_dir.glob(f'{beat}.reads'), *table_dir.glob(f'*/{beat}.reads')})
    assert found, (f'drive replay table missing: {beat}.reads under {table_dir} (record it on the fork: '
                   f'tools/record_drive_replay.py --out {table_dir}/<new folder> {beat}; or run with '
                   f'--no-drive-replay)')
    for p in found:
        receipt = p.parent / 'receipt.json'
        assert receipt.exists(), (beat, 'drive replay table without its recording receipt', str(p))
        info = [b for b in json.loads(receipt.read_text())['beats'] if b['beat'] == beat]
        assert len(info) == 1 and info[0]['replay_differences'] == 0, \
            (beat, 'the fork re-drive that recorded this table did not reproduce the recording', str(p))
    tables = {tuple(read_beat(p)) for p in found}
    assert len(tables) == 1, (beat, 'recordings of this beat differ', [str(p) for p in found])
    return found[0]


def write_table(phases, table_dir, out_path):
    """phases: (phase, beat folder name, first_counter) in path order.
    Writes the merged table; returns a summary (raises when a table is missing)."""
    table_dir = Path(table_dir)
    lines, entries, anchors, sources, last = [], [], [], [], None
    for phase, beat, first in phases:
        path = find_beat(table_dir, beat)
        reads = read_beat(path)
        assert all(a[0] <= b[0] for a, b in zip(reads, reads[1:])), (beat, 'table out of counter order')
        if reads and last is not None:
            assert reads[0][0] > last, (phase, 'drive replay tables overlap on this path', reads[0][0], last)
        if reads:
            last = reads[-1][0]
        anchors.append(f'@ {phase} {first}')
        entries.extend(reads)
        sources.append(str(path))
    lines.append('# merged drive replay (comparison only; disc-derived, never committed)')
    lines.extend(anchors)
    lines.extend('%d %d %d %d' % e for e in entries)
    Path(out_path).write_text('\n'.join(lines) + '\n')
    return dict(table=str(out_path), sources=sources, phases=[p for p, _, _ in phases],
                entries=len(entries))


def report(run_text):
    """The run's replay line as a dict, or None."""
    m = REPORT.search(run_text)
    if not m:
        return None
    keys = ('served', 'entries', 'differed', 'near', 'missed', 'missed_in_span', 'last_missed_counter',
            'last_missed_sector', 'not_reached')
    return dict(zip(keys, map(int, m.groups())))
