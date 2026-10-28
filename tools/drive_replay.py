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


def loader_reads(trace_path, iso=None, stream_reads=None):
    """The module loader's bank-load reads (slot-2 record +8 == 0: 001FF830
    with 001FF3F0) of one fork recording, as "L" entries (counter, file,
    sector, sectors, busy): file 0 = INDEX.IDX, 1 = DATA.DAT, the sector
    inside that file. The recording samples the slot-2 record once per
    main-loop tick and the task dispatches once per tick, so a read issued
    in tick i (the header in step 0 -> 1, a chunk in sub-state 1 -> 2, the
    payload in step 3 -> 4) and completed by the poll of tick j took
    j - i - 1 busy polls (docs/MODULE_LOADER.md 1.7, as the New Game rows).
    Each read's file, sector and count are the user's disc's (the module's
    header sector, tools/export_module_loader.py module_reads). Area loads
    (+8 == 1) keep their established alignment rules and are not listed. A
    load with a retried read (a poll error) is refused. The loader does
    nothing while the stream sequencer's phase D_00282157 is nonzero (a
    stream read in flight), so a row in a poll step counts as a poll only
    while no stream read is in flight: with the beat's stream table
    (`stream_reads`, read_beat) a load overlapping one is refused."""
    import export_module_loader as eml
    rows = json.loads(Path(trace_path).read_text())['rows']
    disc = None
    out, i = [], 0
    def rec(r):
        b = bytes.fromhex(r['slots'])
        return b[0x40], b[0x48], b[0x49], b[0x4B], b[0x4E]
    while i < len(rows):
        slot, state, step, sub, module = rec(rows[i])
        if not (slot and state == 0 and step == 1):
            i += 1
            continue
        assert i == 0 or rec(rows[i - 1])[1:3] != (0, 1), (str(trace_path), 'load starts before the recording')
        if disc is None:
            disc = eml.Disc(Path(iso) if iso else eml.DECOMP / 'Extermination-rebuilt.iso')
        _header, reads = eml.module_reads(disc, disc.index, disc.data, module)
        runs, j, prev = [], i, None
        while j < len(rows):
            k = rec(rows[j])
            if not (k[0] and k[1] == 0 and k[4] == module):
                break
            key = 'header' if k[2] == 1 else 'chunk' if (k[2], k[3]) == (2, 2) else 'payload' if k[2] == 4 else None
            if key and (prev != key or (runs and runs[-1][2] != j - 1)):
                runs.append([key, j, j])
            elif key:
                runs[-1][2] = j
            assert prev is None or k[2] >= rec(rows[j - 1])[2], (str(trace_path), rows[j]['f'], 'loader step moved back (a retried read)')
            prev = key
            j += 1
        assert j < len(rows) and rec(rows[j])[1] == 0x63, (str(trace_path), 'load still running at the recording\'s end')
        names = [w for w, _l, _n in reads]
        kinds = [r[0] for r in runs]
        expect = ['header'] + ['chunk'] * sum(n.startswith('chunk') for n in names) + ['payload']
        assert kinds == expect, (str(trace_path), rows[i]['f'], 'loader reads differ from the module header', kinds, expect)
        if stream_reads is not None:
            c0, c1 = rows[i]['counter'], rows[j]['counter']
            busy = [(c, f) for c, _s, _n, f in stream_reads if c <= c1 and c + f + 1 >= c0]
            assert not busy, (str(trace_path), 'a stream read is in flight during the load', busy)
        for (what, lsn, sectors), (_k, first, last) in zip(reads, runs):
            base = disc.index[0] if what == 'header' else disc.data[0]
            if sectors:
                out.append((rows[first]['counter'], 0 if what == 'header' else 1, lsn - base, sectors,
                            last - first))
        i = j
    return out


def write_table(phases, table_dir, out_path, traces=None, extra_anchors=()):
    """phases: (phase, beat folder name, first_counter) in path order.
    traces: optional {phase: fork trace.json}; their bank-load reads
    (loader_reads) are added as "L" lines for the module loader.
    Writes the merged table; returns a summary (raises when a table is missing)."""
    table_dir = Path(table_dir)
    lines, entries, anchors, sources, last, loader = [], [], [], [], None, []
    for phase, beat, first in phases:
        path = find_beat(table_dir, beat)
        reads = read_beat(path)
        if traces and phase in traces:
            loader.extend(loader_reads(traces[phase], stream_reads=reads))
        assert all(a[0] <= b[0] for a, b in zip(reads, reads[1:])), (beat, 'table out of counter order')
        if reads and last is not None:
            assert reads[0][0] > last, (phase, 'drive replay tables overlap on this path', reads[0][0], last)
        if reads:
            last = reads[-1][0]
        anchors.append(f'@ {phase} {first}')
        entries.extend(reads)
        sources.append(str(path))
    # Extra anchors re-align a phase that has no table of its own inside
    # another beat's recording (the AREA00 arrival inside a01_07): the
    # run's counter relation changes across the area load.
    anchors.extend(f'@ {phase} {counter}' for phase, counter in extra_anchors)
    lines.append('# merged drive replay (comparison only; disc-derived, never committed)')
    lines.extend(anchors)
    lines.extend('%d %d %d %d' % e for e in entries)
    lines.extend('L %d %d %d %d %d' % e for e in loader)
    Path(out_path).write_text('\n'.join(lines) + '\n')
    return dict(table=str(out_path), sources=sources, phases=[p for p, _, _ in phases],
                entries=len(entries), loader_entries=len(loader))


def report(run_text):
    """The run's replay line as a dict, or None."""
    m = REPORT.search(run_text)
    if not m:
        return None
    keys = ('served', 'entries', 'differed', 'near', 'missed', 'missed_in_span', 'last_missed_counter',
            'last_missed_sector', 'not_reached')
    return dict(zip(keys, map(int, m.groups())))
