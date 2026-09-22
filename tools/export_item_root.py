#!/usr/bin/env python3
"""Export original ITEM-root sprites and ordered layouts from F170/F2A0.

The original functions execute with draw calls intercepted; hover selection
and the separate analog-trail renderer are explicit boundaries. The latter
is retained as an ordered command, never replaced with invented artwork.
All outputs contain local game data and remain under ignored assets/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from test_panel_message_reference import Original as Base
from test_interaction_animation_reference import signed

ROOT = Path(__file__).resolve().parents[1]
RETURN, UI = 0xBADF00D, 0x900000


class Original(Base):
    def __init__(self, elf, selection):
        super().__init__(elf, 0)
        self.selection = selection
        self.mode = 0
        self.commands = []

    def collect(self, entry):
        self.r[4], self.r[31] = UI, RETURN
        pc = entry
        for _ in range(2000):
            if pc == RETURN:
                return
            if pc == 0x20A7A0:
                self.commands.append({'kind': 'background', 'tex0': self.r[4] & 0xFFFFFFFFFFFFFFFF})
                self.mode = 0
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x207D00:
                assert self.r[4] == 1
                self.mode = self.r[5]
                assert self.mode in range(4)
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x207E40:
                slot, x, y, w, h, rgba, tex0 = self.r[4:11]
                assert slot == 1
                self.commands.append({'kind': 'sprite', 'mode': self.mode, 'x': x, 'y': y,
                    'w': w, 'h': h, 'rgba': rgba & 0xFFFFFFFF,
                    'tex0': tex0 & 0xFFFFFFFFFFFFFFFF})
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x20D930:
                assert self.r[4:6] == [UI, 1]
                self.put(UI + 0x11, self.selection, 1)
                pc = self.r[31] & 0xFFFFFFFF
                continue
            if pc == 0x20AC70:
                assert self.r[5:7] == [0x700038A0, 0]
                x, y = (struct.unpack('<f', struct.pack('<I', self.get(a)))[0]
                        for a in (0x700038A0, 0x700038A4))
                self.commands.append({'kind': 'analog_trail', 'x': x, 'y': y})
                self.mode = 1  # Original trail worker's00207D00(1,1).
                pc = self.r[31] & 0xFFFFFFFF
                continue
            assert 0x20F170 <= pc < 0x20F950, hex(pc)
            word = self.get(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            target = None
            if op in (1, 4, 5, 6, 7, 20, 21):
                if op == 1:
                    assert rt in (0, 1)
                    taken = (signed(self.r[rs]) < 0) == (rt == 0)
                elif op in (4, 5, 20, 21):
                    taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                elif op == 6:
                    taken = signed(self.r[rs]) <= 0
                else:
                    taken = signed(self.r[rs]) > 0
                target = pc + 4 + signed(word & 65535, 16) * 4 if taken else pc + 8
                if op < 20 or taken:
                    self.plain(self.get(pc + 4))
                pc = target
                continue
            if op in (2, 3):
                if op == 3:
                    self.r[31] = pc + 8
                target = (word & 0x3FFFFFF) << 2
            elif op == 0 and word & 63 == 8:
                target = self.r[rs] & 0xFFFFFFFF
            if target is not None:
                self.plain(self.get(pc + 4))
                pc = target
            else:
                self.plain(word)
                pc += 4
        raise AssertionError('Original ITEM root drawer failed to return')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT.parent / 'Extermination')
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/scene_snow/panel')
    args = parser.parse_args()
    capture = args.capture or args.decomp / 'build/startup-reference/panel/root'
    sys.path.insert(0, str(args.decomp / 'tools'))
    from export_ui import decode_token_lm, pack_shelf, parse_outer
    from gs_vram import read_localmem
    elf = (args.decomp / 'config/SCUS_971.12').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    assert digest == 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
    ram = (capture / 'eeMemory.bin').read_bytes()
    assert len(ram) == 0x2000000 and ram[0x810131:0x810136] == bytes((3, 2, 0, 1, 0))
    _, local = read_localmem(capture / 'gs.bin')
    layouts = []
    for selection in range(6):
        original = Original(elf, selection)
        original.collect(0x20F170)
        original.collect(0x20F2A0)
        layouts.append(original.commands)
    tokens = list(dict.fromkeys(command['tex0'] for commands in layouts
                               for command in commands if 'tex0' in command))
    decoded = [decode_token_lm(local, token & 0xFFFFFFFF, token >> 32) for token in tokens]
    # The original trail is an untextured Gouraud fan. The white texel is
    # a backend carrier only; it does not supply any authored cursor art.
    white_index = len(tokens)
    tokens.append(0)
    decoded.append((bytes((255, 255, 255, 255)), {'w': 1, 'h': 1}))
    positions, height = pack_shelf([(meta['w'], meta['h']) for _, meta in decoded], 1024)
    width = 1024
    atlas = bytearray(width * height * 4)
    sprite_records = bytearray()
    for index, ((pixels, meta), (x, y)) in enumerate(zip(decoded, positions)):
        w, h = meta['w'], meta['h']
        sprite_records += struct.pack('<4IQ', x, y, w, h, tokens[index])
        for row in range(h):
            start = ((y + row) * width + x) * 4
            atlas[start:start + w * 4] = pixels[row * w * 4:(row + 1) * w * 4]
    command_bytes = bytearray()
    for commands in layouts:
        for command in commands:
            if command['kind'] == 'sprite':
                values = (0, command['mode'], tokens.index(command['tex0']), command['x'],
                          command['y'], command['w'], command['h'], command['rgba'])
            elif command['kind'] == 'background':
                values = (1, 0, tokens.index(command['tex0']), 0, 0, 0, 0, 0)
            else:
                values = (2, 1, 0, int(command['x']), int(command['y']), 0, 0, 0)
            command_bytes += struct.pack('<8I', *values)
    counts = [len(commands) for commands in layouts]
    text_source = (args.decomp / 'extract/chunk00/f02_id02.bin').read_bytes()
    directory, _, _, directory_offset = struct.unpack_from('<4I', text_source)
    outer = directory + struct.unpack_from('<I', text_source, directory_offset + 16)[0]
    strings, _ = parse_outer(text_source, outer, 'ITEM help group1')
    text_blob = bytearray()
    text_metadata = []
    for line in range(5):
        offset, _, _, size = struct.unpack_from('<4I', text_source, outer + 16 + line * 16)
        record_base = outer + struct.unpack_from('<I', text_source, outer)[0] + offset
        spans = []
        for index in range(size // 16):
            tag, color, at, _ = struct.unpack_from('<4I', text_source, record_base + index * 16)
            assert tag == 2, 'Unsupported original help markup'
            rgb = struct.unpack_from('<I', elf, 0x26EC10 + color * 4 - 0x100000 + 0x300)[0]
            spans.append((at, rgb))
        value = strings[line]
        text_blob += struct.pack('<II', len(value) + 1, len(spans))
        text_blob += b''.join(struct.pack('<II', at, color) for at, color in spans)
        text_blob += value + b'\0'
        text_metadata.append({'group': 1, 'line': line, 'bytes': len(value), 'spans': spans})
    payload = sprite_records + struct.pack('<6I', *counts) + command_bytes + text_blob + atlas
    header = struct.pack('<4s11I', b'EMIR', 2, width, height, len(tokens),
                         sum(counts), 6, len(payload), 5, len(text_blob), white_index, 0)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'item_root.emir').write_bytes(header + payload)
    source = {'elf_sha256': digest, 'capture_sha256': hashlib.sha256(ram).hexdigest(),
              'callbacks': ['0020F170', '0020F2A0'], 'layout_counts': counts,
              'texture_count': len(tokens) - 1, 'layouts': layouts, 'help': text_metadata,
              'boundaries': ['hover quantizer0020D930', 'analog trail0020AC70'],
              'atlas': [{'tex0': hex(token), 'xy': position, 'wh': [meta['w'], meta['h']]}
                        for token, position, (_, meta) in zip(tokens, positions, decoded)]}
    (args.out / 'item_root_source.json').write_text(json.dumps(source, indent=2) + '\n')
    print(json.dumps({'original_layouts': 6, 'command_counts': counts,
                      'textures': len(tokens) - 1, 'help_strings': 5, 'source': str(capture)}))


if __name__ == '__main__':
    main()
