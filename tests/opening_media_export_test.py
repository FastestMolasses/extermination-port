#!/usr/bin/env python3
"""Synthetic-only exporter contract tests: no disc or original assets."""
from pathlib import Path
import importlib.util
import struct
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('opening_export',Path(__file__).parents[1]/'tools/export_opening_media.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def bank():
    data=bytearray(99)
    struct.pack_into('<4I',data,0,32,1,32,16)
    struct.pack_into('<4I',data,16,0,0,0,32)
    struct.pack_into('<4I',data,32,3,1,0,0xffffffff)
    struct.pack_into('<4I',data,48,3,0,1,0xffffffff)
    struct.pack_into('<4I',data,64,32,1,3,1)
    struct.pack_into('<4I',data,80,0,0,2,3)
    data[96:99]=b'A\n\0'
    return data

class ExportTest(unittest.TestCase):
    def test_text_and_top_edge_skew(self):
        self.assertEqual(module.parse_lines(bank(),0),[(b'A\n',8)])
        data=bank();struct.pack_into('<I',data,28,0)
        self.assertEqual(module.parse_lines(data,0),[(b'A\n',0)])
    def test_unknown_markup_is_not_flattened(self):
        data=bank();struct.pack_into('<I',data,36,2)
        self.assertEqual(module.parse_lines(data,0),[(b'A\n',-1)])
        with tempfile.TemporaryDirectory() as tmp:
            record=dict(line=1,duration=1,voice=-1,speaker=0,terminal=1,text=b'A',skew=-1)
            with self.assertRaises(ValueError):module.write_dialogue(Path(tmp)/'d', [record],24,0,0)
    def test_invalid_source_bounds(self):
        for at,value in ((0,16),(4,0),(28,33),(72,1),(80,100),(84,1),(92,2)):
            with self.subTest(at=at,value=value):
                data=bank();struct.pack_into('<I',data,at,value)
                with self.assertRaises(ValueError):module.parse_lines(data,0)
        with self.assertRaises(ValueError):module.parse_lines(bank()[:-1],0)
        data=bank();data[-1]=1
        with self.assertRaises(ValueError):module.parse_lines(data,0)
    def test_binary_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'opening.emod'
            record=dict(line=102,duration=29,voice=-1,speaker=255,terminal=1,text=b'X',skew=8)
            module.write_dialogue(path,[record],24,0x606060,0x100505)
            data=path.read_bytes()
            self.assertEqual(struct.unpack_from('<4s7I',data),(b'EMOD',1,1,24,0x606060,0x100505,388,2))
            self.assertEqual(struct.unpack_from('<HHhBBIIB3x',data,32),(102,29,-1,255,1,0,1,8))
            self.assertEqual(data[52:],b'X\0')

if __name__=='__main__':unittest.main()
