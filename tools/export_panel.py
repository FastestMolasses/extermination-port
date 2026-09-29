#!/usr/bin/env python3
"""Export original type24 panel scripts, BATTERY page, text and clip15C.

All output is local disc data and belongs in ignored assets/. The BATTERY
page's textures are decoded from the GS memory of the BATTERY page state
rebuilt from the user's own disc (tools/export_disc_textures_gs.py: the
first level's world plus 001FF830(0x21)'s upload of the BATTERY module;
docs/DISC_TEXTURES.md); each must read only blocks a disc upload writes.
No PCSX2 capture is needed. With --capture DIR (optional, developers: the
BATTERY confirmation page capture, eeMemory.bin + gs.bin) the tool also
checks the capture's module byte and UI table and that every token decodes
identically from the captured GS memory.
The clip is retained as original channel bytes; this exporter does not
guess a root-motion conversion or silently recenter the interaction.

Usage (port root): python3 tools/export_panel.py [--iso FILE | --disc DIR] [--capture DIR]
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
SCRIPT_BASE, SCRIPT_END = 0x246F20, 0x247E20
SCRIPT_ENTRIES = (0x246F20, 0x2477A0, 0x247BA0, 0x247BE0, 0x247DA0)
ELF_HASH = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
BATTERY_MODULE = 0x21   # the BATTERY page module (the panel capture's D_00810146)


def elf_read(elf: bytes, address: int, size: int) -> bytes:
    at = address - 0x100000 + 0x300
    return elf[at:at + size]


def battery_tokens(elf: bytes) -> list:
    """Stable IDs: frame table 0..15 (D_00265C50), three label/icon triples
    16..24 (D_00265CD0), original row highlight 25 and scrolling background
    26 (the two constants the page code builds)."""
    tokens = [struct.unpack('<Q', elf_read(elf, 0x265C50 + i * 8, 8))[0] for i in range(16)]
    tokens += [struct.unpack('<Q', elf_read(elf, 0x265CD0 + i * 8, 8))[0] for i in range(9)]
    tokens += [0x20042D05A1322000, 0x20043C859D422150]
    return tokens


def battery_emba(elf: bytes, decode_token, extract: Path) -> tuple:
    """(battery.emba bytes, tokens, positions, decoded). decode_token(token)
    -> (RGBA8 bytes, {'w', 'h'}) of one resident page texture."""
    from export_ui import pack_shelf, parse_outer
    tokens = battery_tokens(elf)
    decoded = [decode_token(t) for t in tokens]
    # The original No cursor is an untextured rectangle, drawn after the
    # decor. A white helper texel preserves that order in the native
    # decor queue without changing its geometry/color or inventing art.
    tokens.append(0)
    decoded.append((b'\xff\xff\xff\xff', {'w': 1, 'h': 1}))
    positions, height = pack_shelf([(m['w'], m['h']) for _, m in decoded], 1024)
    width = 1024
    atlas = bytearray(width * height * 4)
    records = bytearray()
    for index, ((rgba, meta), (u, v), token) in enumerate(zip(decoded, positions, tokens)):
        w, h = meta['w'], meta['h']
        records += struct.pack('<6IQ', index, u, v, w, h, 0, token)
        for y in range(h):
            start = ((v + y) * width + u) * 4
            atlas[start:start + w * 4] = rgba[y * w * 4:(y + 1) * w * 4]
    u32 = lambda a: struct.unpack_from('<I', elf_read(elf, a, 4))[0]  # noqa: E731
    data = (extract / 'chunk00/f02_id02.bin').read_bytes()
    directory, _, _, directory_off = struct.unpack_from('<4I', data)
    texts = []

    def text_record(source, outer, line, label):
        lines, _ = parse_outer(source, outer, label)
        off, _, _, size = struct.unpack_from('<4I', source, outer + 16 + line * 16)
        record_base = outer + struct.unpack_from('<I', source, outer)[0] + off
        spans = []
        for i in range(size // 16):
            tag, color, at, _ = struct.unpack_from('<4I', source, record_base + i * 16)
            assert tag == 2, 'Unsupported original text markup; do not flatten'
            spans.append((at, u32(0x26EC10 + color * 4)))
        return lines[line], spans
    for group, line in ((5, 0), (5, 8), (5, 9), (3, 27), (3, 28), (3, 29)):
        outer = directory + struct.unpack_from('<I', data, directory_off + group * 16)[0]
        texts.append(text_record(data, outer, line, f'group{group}'))
    global_bank = (extract / 'chunk03/f14_id16.bin').read_bytes()
    texts.append(text_record(global_bank, 0, 0x18, 'global'))
    # The terminal line 0x80000018 itself runs on the message service
    # (tools/export_message_data.py exports its tables and banks, WP-8).
    outer = directory + struct.unpack_from('<I', data, directory_off + 5 * 16)[0]
    texts.append(text_record(data, outer, 0x19, 'group5'))
    # Acquisition state 3 retains group 4 while the acquired row is selected.
    outer = directory + struct.unpack_from('<I', data, directory_off + 4 * 16)[0]
    for line in (0x1B, 0x1C, 0x1D):
        texts.append(text_record(data, outer, line, 'group4'))
    text_blob = bytearray()
    for value, spans in texts:
        text_blob += struct.pack('<II', len(value) + 1, len(spans))
        text_blob += b''.join(struct.pack('<II', at, rgb) for at, rgb in spans)
        text_blob += value + b'\0'
    # Header then fixed 32-byte TEX0 records, length-prefixed strings,
    # original shared background state, and RGBA8 sheet.
    background = elf_read(elf, 0x2655A0, 3 * 32)
    payload = records + text_blob + background + atlas
    header = struct.pack('<4s7I', b'EMBA', 2, width, height, len(tokens),
                         len(texts), len(text_blob), len(payload))
    return header + payload, tokens, positions, decoded


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', type=Path, default=ROOT.parent/'Extermination')
    ap.add_argument('--iso', type=Path, help='the disc image (default ../Extermination/Extermination-rebuilt.iso)')
    ap.add_argument('--disc', type=Path, help='a mounted disc or a copy of its DATA/ directory')
    ap.add_argument('--capture', type=Path, help='optional cross-check: the BATTERY page capture folder '
                    '(e.g. ../Extermination/build/startup-reference/panel)')
    ap.add_argument('--out', type=Path, default=ROOT/'assets/scene_snow/panel')
    args = ap.parse_args()
    args.decomp = args.decomp.resolve()
    sys.path.insert(0, str(args.decomp/'tools'))
    sys.path.insert(0, str(ROOT/'tools'))
    from audio_export import ElfImage
    from export_ui import decode_token_lm
    import export_disc_textures_gs as G
    elf = ElfImage(args.decomp/'config/SCUS_971.12')
    assert hashlib.sha256(elf.data).hexdigest() == ELF_HASH, 'Unexpected original ELF'
    extract = args.decomp/'extract'
    fl = G.FirstLevel(G.Disc(args.iso, args.disc), extract)
    page = fl.page(fl.world(), BATTERY_MODULE)
    lm = bytes(page.lm)

    def decode(token):
        lo, hi = token & 0xFFFFFFFF, token >> 32
        if not G.reads_only_covered(page, lambda m: decode_token_lm(m, lo, hi)[0]):
            raise SystemExit(f'token {token:#018x}: reads GS blocks no disc upload writes')
        return decode_token_lm(lm, lo, hi)
    emba, tokens, positions, decoded = battery_emba(elf.data, decode, extract)
    capture_sha = None
    if args.capture:
        from gs_vram import read_localmem
        ram = (args.capture/'eeMemory.bin').read_bytes()
        assert len(ram) == 0x2000000
        assert ram[0x810146] == BATTERY_MODULE, 'Snapshot is not the original BATTERY module'
        assert elf.read(0x265C50, 0xC8) == ram[0x265C50:0x265D18], 'UI table changed'
        _, captured = read_localmem(args.capture/'gs.bin')
        for token in tokens[:-1]:
            if decode_token_lm(captured, token & 0xFFFFFFFF, token >> 32)[0] != decode(token)[0]:
                raise SystemExit(f'token {token:#018x}: the capture differs from the disc decode')
        capture_sha = hashlib.sha256(ram).hexdigest()
    args.out.mkdir(parents=True, exist_ok=True)

    program = elf.read(SCRIPT_BASE, SCRIPT_END-SCRIPT_BASE)
    assert struct.unpack_from('<I', program, 0)[0] == 7
    assert struct.unpack_from('<I', program, 0x247DE0-SCRIPT_BASE)[0] == 0x80000007
    (args.out/'scripts.emsc').write_bytes(struct.pack('<4sIIII', b'EMSC', 1,
        SCRIPT_BASE, SCRIPT_ENTRIES[0], len(program)) + program)
    (args.out/'battery.emba').write_bytes(emba)

    bank = (extract/'chunk28/f01_id3c.bin').read_bytes()
    count = struct.unpack_from('<I',bank)[0]
    offsets = struct.unpack_from(f'<{count}I',bank,4)
    clip_start, clip_end = offsets[0x15C:0x15E]
    assert clip_start == 0x1C2E50 and clip_end > clip_start
    clip = bank[clip_start:clip_end]
    bones, frames = struct.unpack_from('<HH',clip)
    assert (bones,frames)==(21,121)
    (args.out/'player_15c.bin').write_bytes(clip)
    report = {'elf_sha256':ELF_HASH,'texels':'disc (FirstLevel.world() + page 0x21)',
              'capture_sha256':capture_sha,
              'script_entries':[hex(x) for x in SCRIPT_ENTRIES],
              'script_sha256':hashlib.sha256(program).hexdigest(),
              'camera_command':{'callback':'0018CBD0','distance':'current camera+0C',
                                'solve_modes':[5,1],'camera_A0':120},
              'animation':{'bank':'chunk28/f01_id3c.bin','id':348,
                           'offset':clip_start,'size':len(clip),
                           'bones':bones,'frames':frames,'rate':1.0,
                           'sha256':hashlib.sha256(clip).hexdigest(),
                           'root_motion':'original bytes, no guessed conversion'},
              'atlas':[{'id':i,'tex0':f'{token:016x}','x':uv[0],'y':uv[1],
                        'w':m['w'],'h':m['h']} for i,(token,uv,(_,m)) in
                       enumerate(zip(tokens,positions,decoded))],
              'text_sources':['5:0','5:8','5:9','3:27','3:28','3:29','global:18','5:25',
                              '4:27','4:28','4:29']}
    (args.out/'source.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Panel: original scripts, {len(tokens)} textures (from the disc'
          + (', identical to the capture' if args.capture else '') + f') and clip15C -> {args.out}')


if __name__ == '__main__': main()
