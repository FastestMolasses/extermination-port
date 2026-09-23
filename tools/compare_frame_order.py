#!/usr/bin/env python3
"""Compare a native frame trace with the original per-frame call order (WP-3 S6).

Design: docs/SCENE_COORDINATOR_DESIGN.md section 3.3 (and section 6, row S6).

Inputs
  native    EM_FRAME_TRACE JSONL written by src/game/em_frame_trace.c, one line
            per task tick (format in em_frame_trace.h). An original JSON may be
            given instead (the self-test does this).
  original  the measured traces in Extermination/build/s87/frame_trace/
            (idle04, walk04, st03, cut02, cut07, cut15, st14), written by that
            directory's trace.py: frames[].events[] with pc/fn/op/target,
            callback/current_actor for jalr, entry events, classifier_ret.

One vocabulary (both sides are normalized to it)
  * Scope. Only calls made by the coordinator functions that the original
    tracer instrumented are compared: 001ACEC0, 001AD250, 0x1AE040, 001AE5E0,
    001AE6B0 (their jal sites) and the 001AFD70 walk. trace.py also traced the
    main loop, 001AB6A0, 0015C160, 001D1C50, 001AAD00, 001A8970, 001A8660 and
    jalr sites nested in owner behaviours; those calls happen inside workers
    (or outside the task) and the native cores do not trace them, so they are
    dropped from both sides and counted. Native events from callers the
    original did not instrument (e.g. 001AD140) are dropped the same way.
  * Walk nodes. Per ticked node 001AFD70 calls 001CB590(node, 0x2F0, +9) and
    then the behaviour *(+0x10) (Extermination/src/func_001AFD70.c). A
    001AFD70->001CB590 call immediately followed by a 001AFD70 jalr is folded
    into that node event. A node is compared on callback + record, never on
    node address (LIFO slot reuse; design risk 8).
  * Records. trace.py's match(): "X" (fn+uid+position) and "X (moved)"
    (fn+uid) name the record X; "at-pos-of X" only means the node sits at X's
    position with a callback/uid that matches no record, so it is no record.
  * Arguments. The original trace has no call arguments, except the a0 seen at
    the 001AFD70 entry (the walk mode), which is attached to the preceding
    call to 001AFD70. Arguments are compared only where both sides have them.
  * Frame state. The original samples the task bytes +8..+0xC and the selector
    0x70003B8D at the 0x1AE040 entry; the native trace samples the same (null
    when 0x1AE040 is not entered). The classifier value (001AE7E0 result) is
    a per-frame field; the call to 001AE7E0 itself is an ordinary event.

Alignment is by machine state, never by counter: the original frames form a
window that must match native frames with identical (task bytes, selector),
consecutive. The first such window is used (or --native-index). Every frame
of the window reports its first divergence.

Allowed differences come from tools/frame_order_allow.json: each entry is an
event pattern that may be missing from the native side ("missing"), present
only on the native side ("extra"), or a frame field that may differ
("field"). Each carries a reason and the WP/step that removes it; the list
must only shrink. Allowed events are removed from their side before the strict
ordered comparison. Entries that matched nothing are reported as stale.

Usage
  compare_frame_order.py NATIVE.jsonl --original cut02 [--original idle04 ...]
  compare_frame_order.py --self-test
Exit status 0 = PASS, 1 = FAIL, 2 = bad input.
"""
import argparse
import copy
import ctypes as C
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ORIGINAL_DIR = DECOMP / 'build/s87/frame_trace'
SYMBOLS = DECOMP / 'config/symbol_addrs.txt'
ALLOW = ROOT / 'tools/frame_order_allow.json'
ORIGINAL_NAMES = ('idle04', 'walk04', 'st03', 'cut02', 'cut07', 'cut15', 'st14')

WALK = '001AFD70'
SET_CURRENT = '001CB590'
FRAME_MACHINE = '001AE040'
# Callers compared: instrumented by trace.py (its FUNCS list) and traced by the
# native cores (em_scene_task: 001ACEC0, 001AD250; em_scene_frame: 0x1AE040,
# 001AE5E0, 001AE6B0; the pool walk 001AFD70).
SCOPE = ('001ACEC0', '001AD250', FRAME_MACHINE, '001AE5E0', '001AE6B0', WALK)
# Callers trace.py instrumented whose calls are inside workers (or before the
# task runs); pseudo-callers "main" (0x1AAE40) and "nested" (behaviour jalrs).
ORIGINAL_UNSCOPED = ('main', 'nested', '001AB6A0', '0015C160', '001D1C50', '001AAD00',
                     '001A8970', '001A8660')
ALLOW_KINDS = ('missing', 'extra', 'field')
ALLOW_EVENT_KEYS = ('op', 'fn', 'target', 'callback', 'record')
ALLOW_FIELDS = ('classifier',)


class BadInput(Exception):
    pass


# ------------------------------------------------------------------ names

_symbols = None


def symbols():
    global _symbols
    if _symbols is None:
        _symbols = {}
        if SYMBOLS.exists():
            for line in SYMBOLS.read_text().splitlines():
                m = re.match(r'\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+)\s*;', line)
                if m:
                    _symbols[m.group(1)] = int(m.group(2), 16)
    return _symbols


def addr(value, what='address'):
    """Normalize 0x1ae040 / "0x1ae040" / "001AE040" / "func_001AE040" /
    a symbol name to "001AE040"."""
    if value is None:
        return None
    if isinstance(value, int):
        return f'{value & 0xFFFFFFFF:08X}'
    s = str(value)
    if s in ('main', 'nested'):
        return s
    m = re.fullmatch(r'(?:func_|0x)?([0-9A-Fa-f]{1,8})', s)
    if m:
        return f'{int(m.group(1), 16):08X}'
    if s in symbols():
        return f'{symbols()[s]:08X}'
    raise BadInput(f'cannot resolve {what} {s!r} (not hex, not in {SYMBOLS})')


def record_tag(rec):
    if rec is None:
        return None
    if rec.startswith('at-pos-of '):
        return None
    if rec.endswith(' (moved)'):
        return rec[:-len(' (moved)')]
    return rec


# ------------------------------------------------------------------ model

class Ev:
    __slots__ = ('op', 'fn', 'target', 'callback', 'record', 'cls', 'a0', 'where')

    def __init__(self, op, fn, target=None, callback=None, record=None, cls=None, a0=None,
                 where=''):
        self.op, self.fn, self.target, self.callback = op, fn, target, callback
        self.record, self.cls, self.a0, self.where = record, cls, a0, where

    def same(self, other):
        if (self.op, self.fn, self.target, self.callback, self.record) != \
                (other.op, other.fn, other.target, other.callback, other.record):
            return False
        return self.a0 is None or other.a0 is None or self.a0 == other.a0

    def fields(self):
        return {'op': self.op, 'fn': self.fn, 'target': self.target, 'callback': self.callback,
                'record': self.record}

    def __str__(self):
        if self.op == 'jalr':
            s = f'{self.fn} jalr callback={self.callback} record={self.record}'
            if self.cls is not None:
                s += f' class={self.cls}'
        else:
            s = f'{self.fn} -> {self.target}'
            if self.a0 is not None:
                s += f' (a0={self.a0})'
        return s + (f' [{self.where}]' if self.where else '')


class Frame:
    def __init__(self, index, counter):
        self.index, self.counter = index, counter
        self.task = None
        self.selector = None
        self.classifier = None
        self.fade = None
        self.events = []
        self.dropped = 0

    def key(self):
        return (self.task, self.selector)


class Trace:
    def __init__(self, name, kind, frames):
        self.name, self.kind, self.frames = name, kind, frames


def fold_walk(events):
    """Fold 001AFD70->001CB590 into the node event that follows it."""
    out = []
    for i, e in enumerate(events):
        nxt = events[i + 1] if i + 1 < len(events) else None
        if (e.op == 'call' and e.fn == WALK and e.target == SET_CURRENT and nxt is not None
                and nxt.op == 'jalr' and nxt.fn == WALK):
            continue
        out.append(e)
    return out


# ------------------------------------------------------------------ loaders

def load_original_data(data, name):
    if not isinstance(data, dict) or 'frames' not in data:
        raise BadInput(f'{name}: not an original frame trace')
    frames = []
    for fi, raw in enumerate(data['frames']):
        f = Frame(fi, raw.get('counter'))
        pending_classifier = False
        for ei, e in enumerate(raw['events']):
            where = f"pc {e.get('pc')}"
            if 'entry' in e:
                entry = addr(e['entry'], 'entry')
                if entry == FRAME_MACHINE:
                    if f.task is not None:
                        raise BadInput(f'{name} frame {fi}: 0x1AE040 entered twice')
                    f.task = (e['b8'], e['b9'], e['bA'], e['state_B'], e['sub_C'])
                    f.selector = e['sel_3B8D']
                elif entry == WALK:
                    last = f.events[-1] if f.events else None
                    if last is None or last.op != 'call' or last.target != WALK:
                        raise BadInput(f'{name} frame {fi} event {ei}: 001AFD70 entry without '
                                       'a traced call to it')
                    last.a0 = int(e['a0'], 16)
                elif entry != '001ACEC0':  # entered from 001AB6A0 (unscoped)
                    raise BadInput(f'{name} frame {fi} event {ei}: unknown entry {e["entry"]}')
                continue
            if 'classifier_ret' in e:
                if not pending_classifier or f.classifier is not None:
                    raise BadInput(f'{name} frame {fi} event {ei}: classifier result without '
                                   'a 001AE7E0 call')
                f.classifier = e['classifier_ret']
                pending_classifier = False
                continue
            fn = addr(e['fn'], 'caller')
            if fn not in SCOPE:
                if fn not in ORIGINAL_UNSCOPED:
                    raise BadInput(f'{name} frame {fi} event {ei}: caller {e["fn"]} is neither '
                                   'compared nor known-unscoped')
                f.dropped += 1
                continue
            if e['op'] == 'jalr':
                cur = e.get('current_actor') or {}
                f.events.append(Ev('jalr', fn, callback=addr(e['callback']),
                                   record=record_tag(cur.get('record')), cls=cur.get('class'),
                                   where=where))
            elif e['op'] in ('jal', 'j'):
                target = addr(e['target'], 'target')
                f.events.append(Ev('call', fn, target=target, where=where))
                if fn == FRAME_MACHINE and target == '001AE7E0':
                    pending_classifier = True
            else:
                raise BadInput(f'{name} frame {fi} event {ei}: unknown op {e["op"]}')
        f.events = fold_walk(f.events)
        frames.append(f)
    return Trace(name, 'original', frames)


def load_native_lines(lines, name):
    frames = []
    for li, line in enumerate(lines):
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as exc:
            raise BadInput(f'{name} line {li + 1}: not JSON ({exc})') from None
        if 'error' in obj:
            raise BadInput(f'{name} line {li + 1}: recorder error: {obj["error"]} '
                           f'(counter {obj.get("counter")})')
        f = Frame(len(frames), obj['counter'])
        f.task = tuple(obj['task']) if obj.get('task') is not None else None
        f.selector = obj.get('selector')
        f.classifier = obj.get('classifier')
        f.fade = obj.get('fade')
        for ei, e in enumerate(obj['events']):
            fn = addr(e['fn'], 'caller')
            where = f'line {li + 1} event {ei}'
            if fn not in SCOPE:
                f.dropped += 1
                continue
            if e.get('op') == 'jalr':
                f.events.append(Ev('jalr', fn, callback=addr(e['callback']),
                                   record=e.get('record'), cls=e.get('class'), where=where))
            else:
                args = e.get('args') or []
                f.events.append(Ev('call', fn, target=addr(e['target'], 'target'),
                                   a0=args[0] if args else None, where=where))
        f.events = fold_walk(f.events)
        frames.append(f)
    return Trace(name, 'native', frames)


def load_trace(path, name=None):
    path = Path(path)
    name = name or path.stem
    text = path.read_text()
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        data = None
    if isinstance(data, dict) and 'frames' in data:
        return load_original_data(data, name)
    return load_native_lines(text.splitlines(), name)


def original_path(name):
    p = Path(name)
    if p.exists():
        return p
    p = ORIGINAL_DIR / f'{name}.json'
    if not p.exists():
        raise BadInput(f'original trace {name!r} not found ({p})')
    return p


# ------------------------------------------------------------------ allow-list

def load_allow(path):
    data = json.loads(Path(path).read_text())
    if not isinstance(data, dict) or not isinstance(data.get('entries'), list):
        raise BadInput(f'{path}: expected {{"entries": [...]}}')
    ids = set()
    for i, e in enumerate(data['entries']):
        tag = f'{path} entry {i}'
        extra_keys = set(e) - {'id', 'kind', 'traces', 'event', 'field', 'reason', 'removed_by'}
        if extra_keys:
            raise BadInput(f'{tag}: unknown keys {sorted(extra_keys)}')
        for k in ('id', 'kind', 'traces', 'reason', 'removed_by'):
            if not e.get(k):
                raise BadInput(f'{tag}: missing {k}')
        if e['id'] in ids:
            raise BadInput(f'{tag}: duplicate id {e["id"]}')
        ids.add(e['id'])
        if e['kind'] not in ALLOW_KINDS:
            raise BadInput(f'{tag}: kind must be one of {ALLOW_KINDS}')
        if not re.fullmatch(r'(WP-\d+|S\d+[ab]?)', e['removed_by']):
            raise BadInput(f'{tag}: removed_by must name a WP (WP-n) or step (Sn)')
        if not isinstance(e['traces'], list) or not all(
                t == '*' or t in ORIGINAL_NAMES for t in e['traces']):
            raise BadInput(f'{tag}: traces must list names from {ORIGINAL_NAMES} or "*"')
        if e['kind'] == 'field':
            if e.get('field') not in ALLOW_FIELDS or 'event' in e:
                raise BadInput(f'{tag}: a field entry names one of {ALLOW_FIELDS} and no event')
        else:
            ev = e.get('event')
            if not isinstance(ev, dict) or not ev or 'field' in e:
                raise BadInput(f'{tag}: an event entry needs a non-empty "event" pattern')
            bad = set(ev) - set(ALLOW_EVENT_KEYS)
            if bad:
                raise BadInput(f'{tag}: unknown event keys {sorted(bad)}')
            if ev.get('op', 'call') not in ('call', 'jalr'):
                raise BadInput(f'{tag}: op must be call or jalr')
            for k in ('fn', 'target', 'callback'):
                if k in ev:
                    ev[k] = addr(ev[k])
    return data['entries']


def applies(entry, trace_name):
    return '*' in entry['traces'] or trace_name in entry['traces']


def pattern_match(pattern, ev):
    f = ev.fields()
    return all(f[k] == v for k, v in pattern.items())


# ------------------------------------------------------------------ compare

def align(orig, native, start=None):
    n = len(orig.frames)
    if n == 0:
        raise BadInput(f'{orig.name}: no frames')
    keys = [f.key() for f in orig.frames]
    candidates = [s for s in range(len(native.frames) - n + 1)
                  if all(native.frames[s + j].key() == keys[j] for j in range(n))]
    if start is not None:
        return (start if start in candidates else None), candidates
    return (candidates[0] if candidates else None), candidates


def compare(orig, native, allow, start=None):
    """Return a result dict; result['pass'] is the verdict."""
    res = {'original': orig.name, 'native': native.name, 'pass': False, 'frames': [],
           'stale_allow': [], 'message': ''}
    s, candidates = align(orig, native, start)
    res['candidates'] = candidates
    if s is None:
        seen = sorted({str(f.key()) for f in native.frames})
        res['message'] = (f'no native window matches the original machine state '
                          f'{[str(f.key()) for f in orig.frames]}'
                          + (f' at native index {start}' if start is not None else '')
                          + f'; native states seen: {seen[:12]}')
        return res
    res['native_start'] = s
    entries = [e for e in allow if applies(e, orig.name)]
    used = set()
    ok = True
    for j, of in enumerate(orig.frames):
        nf = native.frames[s + j]
        fr = {'frame': j, 'original_counter': of.counter, 'native_counter': nf.counter,
              'native_index': s + j, 'state': str(of.key()), 'divergence': None}
        oev, nev = [], []
        for e in of.events:
            hit = [a['id'] for a in entries if a['kind'] == 'missing' and pattern_match(a['event'], e)]
            used.update(hit)
            if not hit:
                oev.append(e)
        for e in nf.events:
            hit = [a['id'] for a in entries if a['kind'] == 'extra' and pattern_match(a['event'], e)]
            used.update(hit)
            if not hit:
                nev.append(e)
        if of.classifier != nf.classifier:
            hit = [a['id'] for a in entries if a['kind'] == 'field' and a['field'] == 'classifier']
            used.update(hit)
            if not hit:
                fr['divergence'] = {'field': 'classifier', 'original': of.classifier,
                                    'native': nf.classifier}
        if fr['divergence'] is None:
            for i in range(max(len(oev), len(nev))):
                a = oev[i] if i < len(oev) else None
                b = nev[i] if i < len(nev) else None
                if a is None or b is None or not a.same(b):
                    fr['divergence'] = {'index': i,
                                        'original': str(a) if a else '(end of frame)',
                                        'native': str(b) if b else '(end of frame)',
                                        'original_event': a.fields() if a else None,
                                        'native_event': b.fields() if b else None}
                    break
        fr['compared'] = (len(oev), len(nev))
        fr['dropped'] = (of.dropped, nf.dropped)
        if fr['divergence'] is not None:
            ok = False
        res['frames'].append(fr)
    res['stale_allow'] = [e['id'] for e in entries if e['id'] not in used]
    res['pass'] = ok
    return res


def report(res, out=sys.stdout):
    verdict = 'PASS' if res['pass'] else 'FAIL'
    print(f"{verdict} {res['original']} vs {res['native']}", file=out)
    if res['message']:
        print(f"  {res['message']}", file=out)
    if 'native_start' in res:
        print(f"  aligned at native index {res['native_start']} "
              f"({len(res['candidates'])} candidate window(s))", file=out)
    for fr in res['frames']:
        d = fr['divergence']
        head = (f"  frame {fr['frame']} (original counter {fr['original_counter']}, native "
                f"counter {fr['native_counter']}, state {fr['state']}): "
                f"{fr['compared'][0]}/{fr['compared'][1]} events compared, "
                f"{fr['dropped'][0]}/{fr['dropped'][1]} unscoped dropped")
        print(head + (' ok' if d is None else ''), file=out)
        if d is not None:
            if 'field' in d:
                print(f"    first divergence: {d['field']} original={d['original']} "
                      f"native={d['native']}", file=out)
            else:
                print(f"    first divergence at index {d['index']}:", file=out)
                print(f"      original: {d['original']}", file=out)
                print(f"      native:   {d['native']}", file=out)
    for sid in res['stale_allow']:
        print(f"  note: allow entry {sid!r} matched nothing (stale? the list must only shrink)",
              file=out)


# ------------------------------------------------------------------ self-test

def original_to_native_lines(data):
    """Render an original trace in the native JSONL format, keeping every
    in-scope raw event (001CB590 per node included, unfolded)."""
    lines = []
    for raw in data['frames']:
        task = selector = classifier = None
        events = []
        for e in raw['events']:
            if 'entry' in e:
                if addr(e['entry']) == FRAME_MACHINE:
                    task = [e['b8'], e['b9'], e['bA'], e['state_B'], e['sub_C']]
                    selector = e['sel_3B8D']
                elif addr(e['entry']) == WALK:
                    events[-1]['args'][0] = int(e['a0'], 16)
                continue
            if 'classifier_ret' in e:
                classifier = e['classifier_ret']
                continue
            fn = addr(e['fn'])
            if fn not in SCOPE:
                continue
            if e['op'] == 'jalr':
                cur = e.get('current_actor') or {}
                events.append({'fn': fn, 'op': 'jalr', 'callback': addr(e['callback']),
                               'class': cur.get('class'), 'record': record_tag(cur.get('record')),
                               'binding': None})
            else:
                events.append({'fn': fn, 'target': addr(e['target']), 'args': [0, 0, 0, 0]})
        lines.append(json.dumps({'counter': raw['counter'], 'task': task, 'classifier': classifier,
                                 'selector': selector, 'fade': None, 'events': events}))
    return lines


def raw_index(frame, target):
    idx = [i for i, e in enumerate(frame['events']) if e.get('target') == f'func_{target}']
    if len(idx) != 1:
        raise AssertionError(f'expected one call to {target}, found {len(idx)}')
    return idx[0]


def normalized_index(trace, fi, target):
    idx = [i for i, e in enumerate(trace.frames[fi].events) if e.op == 'call' and e.target == target]
    if len(idx) != 1:
        raise AssertionError(f'expected one normalized call to {target}, found {len(idx)}')
    return idx[0]


def build_recorder(tmp):
    lib = Path(tmp) / 'libem_frame_trace.dylib'
    subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-I', str(ROOT / 'src'), str(ROOT / 'src/game/em_frame_trace.c'),
                    '-o', str(lib)], check=True)
    rec = C.CDLL(str(lib))
    rec.em_frame_trace_open.argtypes = [C.c_void_p, C.c_char_p]
    rec.em_frame_trace_close.argtypes = [C.c_void_p]
    rec.em_frame_trace_tick_begin.argtypes = [C.c_void_p, C.c_uint32]
    rec.em_frame_trace_frame_state.argtypes = [C.c_void_p, C.c_char_p, C.c_uint8, C.c_int32]
    rec.em_frame_trace_classifier.argtypes = [C.c_void_p, C.c_int32]
    rec.em_frame_trace_call.argtypes = [C.c_void_p] + [C.c_uint32] * 6
    rec.em_frame_trace_node.argtypes = [C.c_void_p, C.c_uint32, C.c_uint8, C.c_char_p, C.c_char_p]
    rec.em_frame_trace_tick_end.argtypes = [C.c_void_p]
    return rec


def replay_through_recorder(rec, data, path):
    """Drive the real C recorder with an original trace's in-scope events."""
    buf = C.create_string_buffer(512)  # >= sizeof(EmFrameTrace)
    assert rec.em_frame_trace_open(buf, str(path).encode()) == 0
    for raw in data['frames']:
        rec.em_frame_trace_tick_begin(buf, raw['counter'])
        pending = None  # the 001AFD70 call waits for its entry a0
        def flush():
            nonlocal pending
            if pending is not None:
                rec.em_frame_trace_call(buf, *pending)
                pending = None
        for e in raw['events']:
            if 'entry' in e:
                ent = addr(e['entry'])
                if ent == FRAME_MACHINE:
                    flush()
                    task = bytes([e['b8'], e['b9'], e['bA'], e['state_B'], e['sub_C']])
                    rec.em_frame_trace_frame_state(buf, task, e['sel_3B8D'], 0)
                elif ent == WALK:
                    pending[2] = int(e['a0'], 16)
                    flush()
                continue
            if 'classifier_ret' in e:
                flush()
                rec.em_frame_trace_classifier(buf, e['classifier_ret'])
                continue
            fn = addr(e['fn'])
            if fn not in SCOPE:
                continue
            flush()
            if e['op'] == 'jalr':
                cur = e.get('current_actor') or {}
                r = record_tag(cur.get('record'))
                rec.em_frame_trace_node(buf, int(e['callback'], 16), cur.get('class') or 0,
                                        r.encode() if r else None, b'selftest')
            else:
                call = [int(fn, 16), int(addr(e['target']), 16), 0, 0, 0, 0]
                if addr(e['target']) == WALK:
                    pending = call
                else:
                    rec.em_frame_trace_call(buf, *call)
        flush()
        assert rec.em_frame_trace_tick_end(buf) == 0
    assert rec.em_frame_trace_close(buf) == 0


def self_test(allow_path):
    failures = []

    def check(cond, msg):
        print(('  ok   ' if cond else '  FAIL ') + msg)
        if not cond:
            failures.append(msg)

    real = load_allow(allow_path)
    print(f'allow-list {allow_path}: {len(real)} entr{"y" if len(real) == 1 else "ies"}, schema ok')
    # The mechanics below test the comparator itself, so they run with no
    # allowed differences: an entry of the real list (e.g. S10a's missing
    # 001AFD70 nodes) would otherwise hide the very divergences these checks
    # inject. The allow-list mechanics use their own temporary entries.
    allow = []
    originals = {n: json.loads(original_path(n).read_text()) for n in ORIGINAL_NAMES}

    with tempfile.TemporaryDirectory() as tmp:
        rec = build_recorder(tmp)
        for n, data in originals.items():
            o = load_original_data(data, n)
            r = compare(o, load_original_data(copy.deepcopy(data), n), allow)
            nev = sum(len(f.events) for f in o.frames)
            check(r['pass'], f'{n}: original against itself PASS ({len(o.frames)} frames, '
                             f'{nev} compared events)')
            nat = load_native_lines(original_to_native_lines(data), n + '.jsonl')
            check(compare(o, nat, allow)['pass'], f'{n}: native-format rendering PASS')
            p = Path(tmp) / f'{n}.recorder.jsonl'
            replay_through_recorder(rec, data, p)
            rr = compare(o, load_trace(p), allow)
            check(rr['pass'], f'{n}: replay through em_frame_trace.c PASS')

        # The design's synthetic swap: 001F0360 <-> 0015BCF0 in cut02.
        data = originals['cut02']
        o = load_original_data(data, 'cut02')
        for fi in range(len(data['frames'])):
            want = normalized_index(o, fi, '001F0360')
            mutated = copy.deepcopy(data)
            ev = mutated['frames'][fi]['events']
            a, b = raw_index(mutated['frames'][fi], '001F0360'), raw_index(mutated['frames'][fi], '0015BCF0')
            ev[a], ev[b] = ev[b], ev[a]
            variants = {
                'original-format': load_original_data(mutated, 'cut02-swap'),
                'native-format': load_native_lines(original_to_native_lines(mutated), 'cut02-swap'),
            }
            p = Path(tmp) / f'swap{fi}.jsonl'
            replay_through_recorder(rec, mutated, p)
            variants['recorder'] = load_trace(p)
            for label, nat in variants.items():
                r = compare(o, nat, allow)
                bad = [fr for fr in r['frames'] if fr['divergence'] is not None]
                d = bad[0]['divergence'] if len(bad) == 1 else {}
                check(not r['pass'] and len(bad) == 1 and bad[0]['frame'] == fi
                      and d.get('index') == want
                      and d['original_event']['target'] == '001F0360'
                      and d['native_event']['target'] == '0015BCF0',
                      f'cut02 swap in frame {fi} ({label}): FAIL at frame {fi} index {want} '
                      f'(original 001F0360, native 0015BCF0)')
                if label == 'recorder' and fi == 0:
                    report(r)

        # Per-node matching is on callback + record: a changed record fails
        # at that node; a node address change alone does not.
        nat_lines = original_to_native_lines(data)
        frame0 = json.loads(nat_lines[0])
        nodes = [i for i, e in enumerate(frame0['events']) if e.get('op') == 'jalr' and e['record']]
        k = nodes[3]
        frame0['events'][k]['record'] = 'area11[63]'
        r = compare(o, load_native_lines([json.dumps(frame0)] + nat_lines[1:], 'rec'), allow)
        check(not r['pass'] and r['frames'][0]['divergence']['native_event']['record'] == 'area11[63]',
              'changed record on one node: FAIL at that node')
        moved = copy.deepcopy(data)
        for e in moved['frames'][0]['events']:
            if e.get('current_actor'):
                e['current_actor']['ptr'] = '0x7fffff'
        check(compare(o, load_original_data(moved, 'addr'), allow)['pass'],
              'node addresses changed, callbacks and records equal: PASS')

        # Alignment is by machine state, not counter.
        shifted = [json.dumps({'counter': 1, 'task': [9, 9, 9, 9, 9], 'classifier': None,
                               'selector': 0, 'fade': 0, 'events': []})]
        renumbered = [json.dumps(dict(json.loads(line), counter=77 + i))
                      for i, line in enumerate(original_to_native_lines(data))]
        r = compare(o, load_native_lines(shifted + renumbered, 'shift'), allow)
        check(r['pass'] and r['native_start'] == 1,
              'extra leading tick and different counters: aligned at index 1, PASS')
        wrong = [json.dumps(dict(json.loads(line), selector=0)) for line in nat_lines]
        r = compare(o, load_native_lines(wrong, 'sel'), allow)
        check(not r['pass'] and 'native_start' not in r, 'selector differs: no window, FAIL')

        # Classifier is compared per frame.
        wrong = [json.dumps(dict(json.loads(line), classifier=2)) for line in nat_lines]
        r = compare(o, load_native_lines(wrong, 'cls'), allow)
        check(not r['pass'] and r['frames'][0]['divergence'].get('field') == 'classifier',
              'classifier differs: FAIL on the field')

        # Allow-list mechanics (temporary entries, never written to the real list).
        def drop_node(obj, callback):
            # the node's jalr and the 001CB590 call that precedes it
            ev = obj['events']
            keep = [e for i, e in enumerate(ev)
                    if e.get('callback') != callback
                    and not (i + 1 < len(ev) and ev[i + 1].get('callback') == callback)]
            return dict(obj, events=keep)
        area_title = [json.dumps(drop_node(obj, '001C5930')) for obj in map(json.loads, nat_lines)]
        missing = load_native_lines(area_title, 'missing')
        check(not compare(o, missing, allow)['pass'], 'node 001C5930 missing: FAIL')
        tmp_allow = Path(tmp) / 'allow.json'
        tmp_allow.write_text(json.dumps({'entries': [
            {'id': 'test-missing', 'kind': 'missing', 'traces': ['cut02'],
             'event': {'op': 'jalr', 'callback': '0x1c5930'}, 'reason': 'self-test',
             'removed_by': 'S10b'},
            {'id': 'test-stale', 'kind': 'extra', 'traces': ['*'],
             'event': {'fn': '001AE6B0', 'target': '00000001'}, 'reason': 'self-test',
             'removed_by': 'WP-4'}]}))
        r = compare(o, missing, load_allow(tmp_allow))
        check(r['pass'] and r['stale_allow'] == ['test-stale'],
              'allowed "missing" entry: PASS; unused entry reported stale')
        extra = [json.dumps(dict(obj, events=obj['events'] + [
                     {'fn': '001AE6B0', 'target': '00000001', 'args': [0, 0, 0, 0]}]))
                 for obj in map(json.loads, nat_lines)]
        check(not compare(o, load_native_lines(extra, 'extra'), allow)['pass'],
              'extra trailing call: FAIL')
        # (Only the "extra" entry: the "missing" one would remove the area-title
        # node from the original side, which this native trace still has.)
        extra_allow = [e for e in load_allow(tmp_allow) if e['kind'] == 'extra']
        r = compare(o, load_native_lines(extra, 'extra'), extra_allow)
        check(r['pass'] and r['stale_allow'] == [], 'allowed "extra" entry: PASS')
        r = compare(o, load_native_lines(extra, 'extra'), load_allow(tmp_allow))
        check(not r['pass'], 'an allowed "missing" event the native side still has: FAIL')
        for bad_entry in ({'id': 'x', 'kind': 'missing', 'traces': ['*'], 'event': {'op': 'jalr'},
                           'reason': 'r'},
                          {'id': 'x', 'kind': 'missing', 'traces': ['*'], 'event': {'op': 'jalr'},
                           'reason': 'r', 'removed_by': 'later'},
                          {'id': 'x', 'kind': 'field', 'traces': ['*'], 'field': 'task',
                           'reason': 'r', 'removed_by': 'S9'}):
            tmp_allow.write_text(json.dumps({'entries': [bad_entry]}))
            try:
                load_allow(tmp_allow)
                check(False, f'malformed allow entry rejected: {bad_entry}')
            except BadInput:
                check(True, 'malformed allow entry rejected')

        # Recorder errors fail the comparison.
        err = nat_lines[:1] + ['{"error":"call outside a tick","counter":5}']
        try:
            load_native_lines(err, 'err')
            check(False, 'recorder error line rejected')
        except BadInput:
            check(True, 'recorder error line rejected')

    print('SELF-TEST ' + ('PASS' if not failures else f'FAIL ({len(failures)})'))
    return 0 if not failures else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('native', nargs='?', help='EM_FRAME_TRACE JSONL (or an original JSON)')
    ap.add_argument('--original', action='append', default=[],
                    help=f'original trace name ({", ".join(ORIGINAL_NAMES)}) or path')
    ap.add_argument('--allow', default=str(ALLOW))
    ap.add_argument('--native-index', type=int, help='force the window start (native tick index)')
    ap.add_argument('--json', help='also write the results as JSON to this path')
    ap.add_argument('--self-test', action='store_true')
    a = ap.parse_args()
    try:
        if a.self_test:
            return self_test(a.allow)
        if not a.native or not a.original:
            ap.error('need NATIVE and at least one --original (or --self-test)')
        allow = load_allow(a.allow)
        native = load_trace(a.native)
        results = []
        for name in a.original:
            orig = load_trace(original_path(name), Path(name).stem)
            if orig.kind != 'original':
                raise BadInput(f'{name}: not an original trace')
            res = compare(orig, native, allow, a.native_index)
            report(res)
            results.append(res)
        if a.json:
            Path(a.json).write_text(json.dumps(results, indent=1))
        return 0 if all(r['pass'] for r in results) else 1
    except BadInput as exc:
        print(f'BAD INPUT: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
