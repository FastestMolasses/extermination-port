#!/usr/bin/env python3
"""Synthetic checks for route sampler identity binding; no disc data."""
import struct
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from record_level_routes import (POOL_BASE, POOL_STRIDE, apply_owner_binding,
                                 inherited_load_chain, install_hang_wait, match_owners, route_beats)


def fixture(slots):
    ram = bytearray(0x820000)
    addresses = [POOL_BASE + slot * POOL_STRIDE for slot in slots]
    struct.pack_into('<I', ram, 0x275BC0, addresses[0] if addresses else 0)
    for index, address in enumerate(addresses):
        ram[address] = 1
        ram[address + 2:address + 4] = bytes([5, index + 1])
        ram[address + 0x9A] = index
        struct.pack_into('<I', ram, address + 0x10, 0x123456)
        struct.pack_into('<I', ram, address + 0x1C, addresses[index + 1] if index + 1 < len(addresses) else 0)
    return ram, addresses


class BindingTests(unittest.TestCase):
    def test_explicit_side_beat_does_not_change_normal_group(self):
        fn = lambda: None
        lanes = SimpleNamespace(all_beats=lambda: {'a13b_00': ('source', fn, 'a13b')}, LEVEL_ORDER=['a13b'])
        rc = SimpleNamespace(A13B_BEATS=[('a13b_s0', 'a13b_00', fn)])
        self.assertNotIn('a13b_s0', route_beats(rc, lanes))
        self.assertIn('a13b_s0', route_beats(rc, lanes, ['a13b_s0']))

    def test_damage_pause_is_not_a_hang_endpoint(self):
        class Route:
            def __init__(self):
                self.frame = 0
                self.rows = []
                self.s = SimpleNamespace(read=lambda _a, _n: bytes([2 if self.frame <= 25 else 1, 16, 1, 0]))

            def step(self, _n):
                self.frame += 1
                self.rows.append({'p5': 7 if self.frame <= 25 else 16, 'm1F0': 33,
                                  'pos': [max(0, min(25, self.frame - 25)), 0, 0]})
                return self.rows[-1]

            def stick_toward(self, *_args):
                pass

            def set_pad(self, *_args):
                pass

        rc, route = SimpleNamespace(PLAYER=0), Route()
        install_hang_wait(rc)
        rc.a19c_traverse(route, 100, 0, limit=100)
        self.assertGreaterEqual(route.frame, 70)

    def test_moved_slots_ignore_motion_but_preserve_identity(self):
        reference, old = fixture([1, 2])
        actual, new = fixture([8, 11])
        actual[new[0] + 0xB0:new[0] + 0xB4] = b'move'
        actual[new[0] + 5] = 7
        self.assertEqual(match_owners(dict(zip(['a', 'b'], old)), reference, actual), dict(zip(['a', 'b'], new)))

    def test_changed_model_rejected(self):
        reference, old = fixture([1])
        actual, new = fixture([8])
        actual[new[0] + 3] += 1
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            match_owners({'a': old[0]}, reference, actual)

    def test_ambiguous_identity_rejected(self):
        reference, old = fixture([1])
        actual, new = fixture([8, 11])
        actual[new[1] + 3] = actual[new[0] + 3]
        actual[new[1] + 0x9A] = actual[new[0] + 0x9A]
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            match_owners({'a': old[0]}, reference, actual)

    def test_different_area_rejected(self):
        reference, old = fixture([1])
        actual, _ = fixture([8])
        actual[0x810700] = 1
        with self.assertRaisesRegex(ValueError, 'different area'):
            match_owners({'a': old[0]}, reference, actual)

    def test_cycle_rejected(self):
        reference, old = fixture([1])
        actual, new = fixture([8])
        struct.pack_into('<I', actual, new[0] + 0x1C, new[0])
        with self.assertRaisesRegex(ValueError, 'Invalid live'):
            match_owners({'a': old[0]}, reference, actual)

    def test_only_named_owner_spans_change_and_reapplication_is_safe(self):
        owners = {'a': 100}
        rc = SimpleNamespace(TEST_OWNERS=owners, TEST_SPANS=[('global', 25, 4), ('a:h', 100, 16), ('a:p', 276, 16)])
        binding = {'sampler': 'TEST', 'owners': {'a': 300}}
        apply_owner_binding(rc, binding)
        apply_owner_binding(rc, binding)
        self.assertIs(rc.TEST_OWNERS, owners)
        self.assertEqual(rc.TEST_SPANS, [('global', 25, 4), ('a:h', 300, 16), ('a:p', 476, 16)])

    def test_inherited_slots_require_same_area_ancestry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            parent, child = root / 'parent', root / 'child'
            parent.mkdir()
            child.mkdir()
            record = {'rows': [{'area4': '0d000001'}, {'area4': '0d000101'}],
                      'source_state': str(parent / 'state.p2s')}
            path = child / 'trace.json'
            path.write_text(json.dumps(record))
            self.assertEqual(len(inherited_load_chain(child / 'eeMemory.bin', parent / 'eeMemory.bin', '0d00')), 1)
            record['rows'][1]['area4'] = '13000101'
            path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, 'crossed an area'):
                inherited_load_chain(child / 'eeMemory.bin', parent / 'eeMemory.bin', '0d00')

    def test_inherited_ancestry_cycle_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'trace.json').write_text(json.dumps({'rows': [{'area4': '0d000001'}],
                                                       'source_state': str(root / 'state.p2s')}))
            with self.assertRaisesRegex(ValueError, 'Cycle'):
                inherited_load_chain(root / 'eeMemory.bin', root / 'missing/eeMemory.bin', '0d00')


if __name__ == '__main__':
    unittest.main()
