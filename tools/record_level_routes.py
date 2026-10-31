#!/usr/bin/env python3
"""Record missing routes with the decomp's hidden, owned fork capture sessions.

Run with the decomp's .venv Python on macOS arm64 (PCSX2 runs under Rosetta).
All captures and detailed logs stay under the decomp's ignored build/fork_refs.
Only the decomp's public capture tools are imported; no emulator source is read.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import shutil
import struct
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
POOL_BASE, POOL_STRIDE, POOL_COUNT = 0x7A5640, 0x2F0, 256


def route_beats(rc, lanes, extra=()):
    """Keep normal group selection; allow explicitly requested side beats."""
    beats = lanes.all_beats()
    wanted = set(extra) - beats.keys()
    for key, rows in vars(rc).items():
        if not key.endswith('_BEATS') or not isinstance(rows, list):
            continue
        for row in rows:
            if isinstance(row, tuple) and len(row) == 3 and row[0] in wanted:
                name, source, fn = row
                group = name.split('_')[0]
                if group in lanes.LEVEL_ORDER:
                    beats[name] = (source, fn, group)
    return beats


def owner_signature(ram, address):
    """Match callback and placement identity, never an animation or position.

    001B6990 copies class/model, param/UID, kind/link and table index into
    these fields. The callback additionally disambiguates deferred owners.
    Signature bytes are compared locally and never included in public output.
    """
    return tuple(ram[address + offset:address + offset + size]
                 for offset, size in ((2, 2), (0xD, 3), (0x10, 4), (0x54, 4), (0x9A, 1)))


def live_owners(ram):
    address = struct.unpack_from('<I', ram, 0x275BC0)[0]
    seen = set()
    while address:
        slot, remainder = divmod(address - POOL_BASE, POOL_STRIDE)
        if remainder or not 0 <= slot < POOL_COUNT or address in seen or not ram[address]:
            raise ValueError('Invalid live actor list in binding anchor')
        seen.add(address)
        address = struct.unpack_from('<I', ram, address + 0x1C)[0]
    return seen


def match_owners(owners, reference, actual):
    if reference[0x810700:0x810702] != actual[0x810700:0x810702]:
        raise ValueError('Owner binding anchors have different area/sub-area')
    reference_live, actual_live = live_owners(reference), live_owners(actual)
    result = {}
    for name, address in owners.items():
        if address not in reference_live:
            raise ValueError('Reference owner is not live: ' + name)
        matches = [a for a in actual_live if owner_signature(actual, a) == owner_signature(reference, address)]
        if len(matches) != 1:
            raise ValueError('Owner identity requires exactly one live match: ' + name)
        result[name] = matches[0]
    if len(set(result.values())) != len(result):
        raise ValueError('Owner identities alias the same pool node')
    return result


def apply_owner_binding(rc, binding):
    """Change only the sampler's read addresses; game memory is untouched."""
    prefix = binding['sampler']
    owners = getattr(rc, prefix + '_OWNERS')
    if not hasattr(rc, '_levels_original_' + prefix):
        setattr(rc, '_levels_original_' + prefix, owners.copy())
    spans = getattr(rc, prefix + '_SPANS')
    mapped = binding['owners']
    if owners.keys() != mapped.keys():
        raise ValueError('Owner binding keys differ from sampler: ' + prefix)
    changed = []
    for name, address, size in spans:
        key = name.partition(':')[0]
        if ':' in name and key in owners:
            address += mapped[key] - owners[key]
        changed.append((name, address, size))
    # Keep dictionary identity for decoder defaults and other sampler users.
    owners.update(mapped)
    setattr(rc, prefix + '_SPANS', changed)


def inherited_load_chain(actual_path, parent_path, area):
    """Require recorded ancestry within the same area/sub-area load."""
    folder, parent = actual_path.parent.resolve(), parent_path.parent.resolve()
    seen, chain = set(), []
    while folder != parent:
        if folder in seen:
            raise ValueError('Cycle in inherited owner anchor ancestry')
        seen.add(folder)
        path = folder / 'trace.json'
        raw = path.read_bytes()
        trace = json.loads(raw)
        if not trace['rows'] or any(row['area4'][:4] != area for row in trace['rows']):
            raise ValueError('Inherited owner ancestry crossed an area/sub-area load')
        chain.append({'trace': str(path), 'trace_sha256': hashlib.sha256(raw).hexdigest()})
        folder = Path(trace['source_state']).resolve().parent
    return chain


def make_owner_binding(rc, prefix, beat, actual_path, inherit=None):
    reference_path = rc._legacy_beat_dir(beat) / 'eeMemory.bin'
    actual_path = actual_path.parent / 'eeMemory.bin'
    reference, actual = reference_path.read_bytes(), actual_path.read_bytes()
    owners = getattr(rc, prefix + '_OWNERS')
    borrowed = {}
    if inherit is not None:
        parent = inherit['binding']
        original = inherit['original_owners']
        for key, address in parent['owners'].items():
            if key in owners:
                if owners[key] != original[key]:
                    raise ValueError('Inherited sampler names a different original slot: ' + key)
                borrowed[key] = address
        if not borrowed:
            raise ValueError('Inherited sampler has no shared owners')
        parent_ram = Path(parent['actual_ram']).read_bytes()
        if parent_ram[0x810700:0x810702] != actual[0x810700:0x810702]:
            raise ValueError('Inherited anchor is from a different area/sub-area')
        chain = inherited_load_chain(actual_path, Path(parent['actual_ram']),
                                     actual[0x810700:0x810702].hex())
    mapped = match_owners({k: v for k, v in owners.items() if k not in borrowed}, reference, actual)
    mapped.update(borrowed)
    result = {'sampler': prefix, 'anchor_beat': beat,
            'reference_ram': str(reference_path), 'reference_ram_sha256': hashlib.sha256(reference).hexdigest(),
            'actual_ram': str(actual_path), 'actual_ram_sha256': hashlib.sha256(actual).hexdigest(),
            'owners': mapped}
    if inherit is not None:
        # Slots whose owners have freed themselves retain the earlier
        # sampler's read address, as in the original fixed-slot trace.
        result['inherit'] = inherit
        result['inherit_chain'] = chain
    return result


def validate_owner_binding(rc, binding):
    prefix = binding['sampler']
    current = getattr(rc, prefix + '_OWNERS').copy()
    original = getattr(rc, '_levels_original_' + prefix, current)
    setattr(rc, '_levels_original_' + prefix, original)
    inherit = binding.get('inherit')
    if inherit is not None:
        validate_owner_binding(rc, inherit['binding'])
        parent = inherit['binding']['sampler']
        if inherit['original_owners'] != getattr(rc, '_levels_original_' + parent):
            raise ValueError('Inherited sampler original addresses changed')
    getattr(rc, prefix + '_OWNERS').update(original)
    try:
        verified = make_owner_binding(rc, prefix, binding['anchor_beat'],
                                      Path(binding['actual_ram']).with_name('state.p2s'), inherit)
    finally:
        getattr(rc, prefix + '_OWNERS').update(current)
    if verified != binding:
        raise ValueError('Owner binding anchor changed: ' + prefix)


def private_root(decomp, path):
    base = (decomp / 'build/fork_refs').resolve()
    path = path.resolve()
    if base not in path.parents:
        raise ValueError('output/source roots must be inside the decomp build/fork_refs')
    return path


def install_idle_wait(rc, waits):
    """00161020 polls Use in its idle case 1, including nonzero idle clips.

    The old helper's clip-zero test can return during the blend-out cases,
    which do not poll Use, or keep waiting through another idle animation.
    This policy only changes how long the controller stays neutral.
    """
    def ready(r):
        state = r.s.read(rc.PLAYER + 4, 4)
        return state[0] == 1 and state[1] == 0 and state[2] == 1 and rc.in_control(r.rows[-1])

    def wait(r, limit):
        start = r.frame_index
        r.set_pad(0)
        for _ in range(limit):
            if ready(r):
                waits.append({'start_frame': start, 'neutral_frames': r.frame_index - start})
                return
            if r.rows[-1]['m1F0'] in rc.A04_GRABS:
                rc.a04_shake(r)
            else:
                r.step(1)
        raise TimeoutError('Original idle Use-polling state not reached')

    def settle(r, frames=10, limit=1500):
        r.idle(frames)
        wait(r, limit)

    original_use = rc.a06_use

    def use(r, *args, **kwargs):
        original_press = r.press

        def press(name, frames=2, after=0):
            if name == 'CROSS':
                wait(r, 300)
            return original_press(name, frames, after)

        r.press = press
        try:
            return original_use(r, *args, **kwargs)
        finally:
            r.press = original_press

    rc.a06_settle, rc.a06_use = settle, use


def install_hang_wait(rc):
    """Do not mistake 00181D70's hang damage reaction for a bar endpoint.

    Preserve the existing movement, swing and drop predicates, but require
    the normal player/hang state before accepting a stationary endpoint or
    sending the drop press. Hit reactions can keep the old action byte.
    """
    def traverse(r, x, z, limit=900):
        for _ in range(limit):
            r.stick_toward(x, z, 1.0)
            row = r.step(1)
            head = r.s.read(rc.PLAYER + 4, 4)
            if head[0] == 1 and row['m1F0'] == 0x28:
                return
            if (head[0] == 1 and head[1] == 0x10 and len(r.rows) > 21
                    and all(q['p5'] == 0x10 and q['m1F0'] == 0x21 for q in r.rows[-21:])
                    and all(abs(a - b) < 0.02 for a, b in zip(r.rows[-21]['pos'], row['pos']))):
                r.set_pad(0)
                return
        raise TimeoutError('Bar end not reached in the normal hang state')

    def drop(r):
        for _ in range(6):
            r.idle(5)
            for _ in range(300):
                head = r.s.read(rc.PLAYER + 4, 4)
                if head[0] == 1:
                    if head[1] not in (0xF, 0x10):
                        return
                    if head[1] == 0x10 and head[2] == 1:
                        break
                r.idle(1)
            else:
                raise TimeoutError('Normal hang Use-polling state not reached')
            r.press('CROSS', 3)
            for _ in range(40):
                r.step(1)
                head = r.s.read(rc.PLAYER + 4, 4)
                if head[0] == 1 and r.rows[-1]['p5'] not in (0xF, 0x10):
                    return
        raise TimeoutError('No drop from the bar in the normal player state')

    rc.a19c_traverse, rc.a19c_drop = traverse, drop


def collect_medical_item(rc, route, points):
    """Collect the live AREA15 treatment pickup using normal movement/Use.

    The recovered AREA02 branch consumed the treatment retained by the
    legacy route. Resolve the replacement from the live actor list; never
    synthesize inventory or relax the later full-health/infection check.
    """
    if route.rows[-1]['item20']:
        return False
    if route.rows[-1]['area4'][:4] != '0f00':
        raise ValueError('Treatment pickup detour requires AREA15 sub 0')
    address = struct.unpack('<I', route.s.read(0x275BC0, 4))[0]
    seen, candidates = set(), []
    while address:
        slot, remainder = divmod(address - POOL_BASE, POOL_STRIDE)
        if remainder or not 0 <= slot < POOL_COUNT or address in seen:
            raise ValueError('Invalid live actor list during treatment pickup')
        seen.add(address)
        actor = route.s.read(address, POOL_STRIDE)
        callback = struct.unpack_from('<I', actor, 0x10)[0]
        if (callback in (0x15AFA0, 0x219550) and actor[4] in (0, 1)
                and struct.unpack_from('<H', actor, 0x2E)[0] == 0x20):
            candidates.append(actor)
        address = struct.unpack_from('<I', actor, 0x1C)[0]
    if len(candidates) != 1:
        raise ValueError('Expected exactly one live treatment pickup')
    actor = candidates[0]
    x, y, z = struct.unpack_from('<3f', actor, 0xB0)
    if actor[8] != 3:
        raise ValueError('Treatment pickup has an unsupported Use mode')
    descriptor = struct.unpack_from('<I', actor, 0x30)[0]
    radius = struct.unpack('<f', route.s.read(descriptor, 4))[0]
    if abs(y - route.rows[-1]['pos'][1]) >= 10:
        raise ValueError('Treatment pickup is not on the current floor')
    for index, (px, pz) in enumerate(points):
        rc.a19d_go_rock(route, px, pz, tol=0.5)
        position = route.rows[-1]['pos']
        # The final point approaches an occupied pickup. Its original Use
        # radius is the destination condition; inventory below proves Use.
        if index == len(points) - 1 and math.hypot(position[0] - x, position[2] - z) <= radius:
            break
        if math.hypot(position[0] - px, position[2] - pz) > 1.25:
            raise RuntimeError('Planned treatment pickup approach is blocked')
    rc.a13_settle(route, 3)
    px, _, pz = route.rows[-1]['pos']
    if math.hypot(px - x, pz - z) > radius:
        raise RuntimeError('Treatment pickup approach is blocked')
    pitch, yaw, roll = struct.unpack_from('<3f', actor, 0xC0)
    aligned = actor[3] == 1 or (actor[3] == 2 and actor[2] & 0xF == 7
                               and max(abs(pitch), abs(roll)) <= math.pi / 4)
    yaw = yaw - math.pi if aligned else math.atan2(x - px, z - pz)
    rc.a13_face(route, math.atan2(math.sin(yaw), math.cos(yaw)), tol=0.08)
    for _ in range(4):
        route.set_pad(0)
        for _ in range(300):
            head = route.s.read(rc.PLAYER + 4, 4)
            if head[:3] == bytes([1, 0, 1]) and rc.in_control(route.rows[-1]):
                break
            route.step(1)
        else:
            raise TimeoutError('Idle Use-polling state not reached at treatment pickup')
        route.press('CROSS', 2)
        try:
            route.until(lambda row: row['item20'] > 0 or not rc.in_control(row), 40)
            break
        except TimeoutError:
            rc.a13_settle(route, 3)
    else:
        raise TimeoutError('Treatment pickup did not take Use')
    route.until(lambda row: row['item20'] > 0, 1200)
    rc.a15b_close_page(route, 600)
    route.until(rc.in_control, 1800)
    rc.a13_settle(route, 5)
    for px, pz in reversed(points[:-1]):
        rc.a19d_go_rock(route, px, pz, tol=0.5)
        position = route.rows[-1]['pos']
        if math.hypot(position[0] - px, position[2] - pz) > 1.25:
            raise RuntimeError('Treatment pickup return path is blocked')
    rc.a13_settle(route, 5)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--out-root', type=Path, required=True, help='new directory inside build/fork_refs')
    ap.add_argument('--source-root', type=Path, action='append', default=[],
                    help='earlier supplemental root, searched in argument order before canonical captures')
    ap.add_argument('--beats', required=True, help='route_lanes group or beat names, comma-separated')
    ap.add_argument('--source-beat', action='append', default=[], metavar='BEAT=SOURCE',
                    help='continue a beat from another recorded fork branch; recorded in trace provenance')
    ap.add_argument('--idle-state-wait', action='store_true', help='wait for the original idle Use-polling state')
    ap.add_argument('--hang-state-wait', action='store_true', help='exclude hit reactions from hang endpoint/drop checks')
    ap.add_argument('--collect-medical-item', action='store_true',
                    help='collect AREA15\'s treatment pickup before the bed if the earlier recovery consumed it')
    ap.add_argument('--medical-route', type=Path,
                    help='private JSON with collision-planned approach points for the treatment pickup')
    ap.add_argument('--append', action='store_true',
                    help='add beats to an existing --out-root: a later beat resumes from an earlier beat of the same root')
    ap.add_argument('--owner-binding', action='append', default=[], metavar='SAMPLER=ANCHOR_BEAT[:INHERIT_SAMPLER]',
                    help='match sampler owners to a recorded fork anchor by unique original placement identity')
    args = ap.parse_args()
    decomp = args.decomp.resolve()
    medical_route, medical_identity = None, None
    if args.collect_medical_item:
        if args.medical_route is None:
            ap.error('--collect-medical-item requires --medical-route')
        path = private_root(decomp, args.medical_route)
        raw = path.read_bytes()
        medical_route = json.loads(raw)['points']
        if (not 1 <= len(medical_route) <= 256 or any(len(p) != 2 or
                any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in p) for p in medical_route)):
            ap.error('invalid medical route points')
        medical_identity = {'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest()}
    out = private_root(decomp, args.out_root)
    sources = [private_root(decomp, p) for p in args.source_root]
    out.mkdir(parents=True, exist_ok=args.append)
    shutil.copyfile(Path(__file__), out / 'run.py')
    sys.path.insert(0, str(decomp / 'tools'))
    import route_capture as rc
    import route_lanes as lanes
    import pcsx2_session as ps

    rc.use_fork('phase', True)
    ps.FORK_MTVU = False
    # Always use the normal owned session and the shared project run lock.
    # Do not inherit a lane or a claimed parent lock from the caller.
    ps.FORK_LANE = None
    os.environ.pop('EXTERMINATION_FORK_LOCK_HELD', None)
    os.environ['PCSX2_FORK_OWNER_LABEL'] = 'levels-route-capture'
    canonical = rc.FORK_REFS
    rc.FORK_REFS, rc.FORK_PHASE_BEATS = out, out / '_state_links'

    def source(name):
        if len(name) == 2 and name.isdigit():
            return rc.slot_path(name)
        relative = rc._legacy_beat_dir(name).relative_to(decomp / 'build') / 'state.p2s'
        for folder in [out, *sources, canonical]:
            candidate = folder / relative
            if candidate.is_file():
                return rc.resumable(candidate)
        raise FileNotFoundError('No recorded fork source for ' + name)

    rc.beat_source = source
    signal.signal(signal.SIGTERM, rc._term_to_interrupt)
    extra = args.beats.split(',')
    extra += [v.partition('=')[2] for v in args.source_beat]
    extra += [v.partition('=')[2].partition(':')[0] for v in args.owner_binding]
    beats = route_beats(rc, lanes, extra)
    bindings = []
    original_owners = {}
    for value in args.owner_binding:
        prefix, separator, anchor = value.partition('=')
        beat, _, parent = anchor.partition(':')
        if not separator or beat not in beats or not prefix.isupper() or not hasattr(rc, prefix + '_OWNERS'):
            ap.error('--owner-binding requires a sampler prefix and known anchor beat')
        if prefix in original_owners:
            ap.error('sampler may only be bound once: ' + prefix)
        original_owners[prefix] = getattr(rc, prefix + '_OWNERS').copy()
        inherit = None
        if parent:
            previous = next((b for b in bindings if b['sampler'] == parent), None)
            if previous is None:
                ap.error('inherited sampler must be bound first: ' + parent)
            inherit = {'binding': previous, 'original_owners': original_owners[parent]}
        binding = make_owner_binding(rc, prefix, beat, source(beat), inherit)
        apply_owner_binding(rc, binding)
        bindings.append(binding)
    (out / 'owner-bindings.json').write_text(json.dumps(bindings, indent=2) + '\n')
    for value in args.source_beat:
        name, separator, source_name = value.partition('=')
        if not separator or name not in beats or source_name not in beats:
            ap.error('--source-beat requires two known route beat names')
        _, fn, group = beats[name]
        beats[name] = (source_name, fn, group)
    for selector in args.beats.split(','):
        if not lanes.select(selector, beats):
            ap.error('unknown route selector: ' + selector)
    names = lanes.select(args.beats, beats)
    if not names:
        ap.error('no matching route beats')
    if any(name.split('_')[0] not in lanes.LEVEL_ORDER for name in names):
        ap.error('only level-route beats are supported')
    waits = []
    if args.idle_state_wait:
        install_idle_wait(rc, waits)
    if args.hang_state_wait:
        install_hang_wait(rc)
    earlier = out / 'progress.json'
    earlier_results = json.loads(earlier.read_text())['results'] if args.append and earlier.exists() else []
    status = {'state': 'running', 'results': earlier_results, 'planned': names,
              'idle_state_wait': args.idle_state_wait, 'hang_state_wait': args.hang_state_wait,
              'collect_medical_item': args.collect_medical_item, 'medical_route': medical_identity,
              'source_beats': args.source_beat}

    def save():
        temporary = out / 'progress.tmp'
        temporary.write_text(json.dumps(status, indent=2) + '\n')
        temporary.replace(out / 'progress.json')

    for name in names:
        source_name, fn, _ = beats[name]
        status['active'] = name
        save()
        waits.clear()
        started = time.monotonic()

        def guarded(r):
            if r.s.visible:
                raise RuntimeError('Capture session must be hidden')
            original_write = r.s.write

            def reject_write(*_args, **_kwargs):
                raise RuntimeError('Route attempted a game-memory write')

            r.s.write = reject_write
            original_walk = rc.a15b_walk
            pickup = []
            if args.collect_medical_item and name == 'a15b_03_bed':
                def walk(route, points, *a, **kw):
                    if not pickup:
                        start = route.frame_index
                        taken = collect_medical_item(rc, route, medical_route)
                        pickup.append({'taken': taken, 'start_frame': start, 'end_frame': route.frame_index})
                    return original_walk(route, points, *a, **kw)
                rc.a15b_walk = walk
            try:
                meta = fn(r)
            finally:
                r.s.write = original_write
                rc.a15b_walk = original_walk
            if r.teleports:
                raise RuntimeError('Route recorded a teleport')
            meta['levels_capture_policy'] = {'idle_state_wait': args.idle_state_wait,
                                            'waits': list(waits), 'game_memory_writes': 0,
                                            'hang_state_wait': args.hang_state_wait,
                                            'medical_item_pickup': pickup,
                                            'medical_route': medical_identity,
                                            'owner_bindings': bindings}
            return meta

        with (out / (name + '.log')).open('a' if args.append else 'x') as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            try:
                # This returns only after closing the capture session and
                # checking the saved state in a second owned session.
                rc.run_beat(name, source_name, guarded)
            except BaseException as exc:
                traceback.print_exc()
                status['state'] = 'failed'
                status['results'].append({'name': name, 'result': 'failed',
                                          'error_type': type(exc).__name__,
                                          'seconds': round(time.monotonic() - started, 3)})
                save()
                return 1
        status['results'].append({'name': name, 'result': 'passed',
                                  'seconds': round(time.monotonic() - started, 3)})
        save()
        print(name + ': captured and resume-checked', flush=True)
    status['state'] = 'complete'
    status.pop('active', None)
    save()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
