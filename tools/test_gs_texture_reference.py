#!/usr/bin/env python3
"""em_gs_texture (the status pages' GS memory and TEX0 decode) against the
disc model and the decomp's decoder.

For the world image and every page module state the export holds (and the
00200970(1) restore after a module), this test:
  - rebuilds the GS memory with tools/export_disc_textures_gs.py's
    FirstLevel (the original upload sequence replayed from the disc);
  - applies the same steps to em_gs_texture and decodes candidate TEX0 words with em_gs_texture_decode and with the
    decomp's export_ui.decode_token_lm (the decoder of every other status
    atlas), and requires equal pixels, or a refusal exactly when the
    decode would read a block no upload wrote (reads_only_covered).
The candidates are every 8-byte word of the export's .data windows whose
TEX0 is a PSMT8 / PSMT4 form with a PSMCT32 CSM1 CLUT at CSA 0 (the page
tables' TEX0 rows), plus the page translations' own TEX0 constants.

Default: the world and module 0x23 over every sixth candidate (about 3 s);
EM_TEST_FULL=1: every candidate over every module, then the restore after
each (about 2 minutes).
"""
import ctypes as C
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import export_disc_textures_gs as G  # noqa: E402
import export_status_pages as E  # noqa: E402

sys.path.insert(0, str(G.DECOMP / 'tools'))
from export_ui import decode_token_lm  # noqa: E402


def library():
    out = ROOT / 'build/gs_texture_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('gs.dylib' if sys.platform == 'darwin' else 'gs.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
                    'src/game/em_gs_texture.c', '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.em_gs_texture_load.restype = C.c_void_p
    n.em_gs_texture_load.argtypes = [C.c_char_p]
    for name in ('em_gs_texture_reset', 'em_gs_texture_free'):
        getattr(n, name).argtypes = [C.c_void_p]
    n.em_gs_texture_apply.argtypes = [C.c_void_p, C.c_uint]
    n.em_gs_texture_decode.argtypes = [C.c_void_p, C.c_uint64, C.c_char_p, C.c_size_t]
    n.em_gs_texture_size.argtypes = [C.c_uint64, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
    return n


def candidates(windows):
    """TEX0 words of the .data windows and the translations' constants."""
    words = set()
    for start, data in windows:
        for at in range(0, len(data) - 7, 4):
            words.add(struct.unpack_from('<Q', data, at)[0])
    for path in sorted((ROOT / 'src/game').glob('em_status_pages_*.c')) + [ROOT / 'src/game/em_area01_ui_pages.c']:
        for hi, lo in re.findall(r'TEX\((0x[0-9A-Fa-f]+)u?, (0x[0-9A-Fa-f]+)u?\)', path.read_text()):
            words.add(int(hi, 16) << 32 | int(lo, 16))
        for value in re.findall(r'(0x[0-9A-Fa-f]{9,16})', path.read_text()):
            words.add(int(value, 16))
    out = []
    for t in sorted(words):
        lo, hi = t & 0xFFFFFFFF, t >> 32
        psm = lo >> 20 & 0x3F
        if psm in (0x13, 0x14) and (hi >> 19) & 0x3FF == 0 and (lo >> 26 & 0xF) <= 10 and \
                (((lo >> 30) & 3) | ((hi & 3) << 2)) <= 10:
            out.append(t)
    return out


def check_state(n, gs_native, image, label, tokens):
    decoded = refused = 0
    for t in tokens:
        w, h = C.c_uint32(), C.c_uint32()
        assert n.em_gs_texture_size(t, C.byref(w), C.byref(h)) == 1
        buf = C.create_string_buffer(w.value * h.value * 4)
        ok = n.em_gs_texture_decode(gs_native, t, buf, len(buf))
        pixels, _ = decode_token_lm(bytes(image.lm), t & 0xFFFFFFFF, t >> 32)
        resident = G.reads_only_covered(image, lambda lm, t=t: decode_token_lm(bytes(lm), t & 0xFFFFFFFF, t >> 32)[0])
        if ok:
            assert resident and buf.raw == pixels, (label, hex(t), resident)
            decoded += 1
        else:
            assert not resident, (label, hex(t), 'refused a resident texture')
            refused += 1
    return decoded, refused


def main():
    full = os.environ.get('EM_TEST_FULL') == '1'
    asset = ROOT / 'assets/status_pages/status_pages.emsp'
    if not asset.exists():
        raise SystemExit(f'{asset} missing: run python3 tools/export_status_pages.py')
    n = library()
    gs = n.em_gs_texture_load(str(asset).encode())
    assert gs, 'em_gs_texture_load refused the export'
    fl = G.FirstLevel(G.Disc(None, None), G.EXTRACT, check_extract=False)
    world = fl.world()
    elf = G.ELF_PATH.read_bytes()
    windows = [(s, elf[s - E.ELF_BASE + E.ELF_OFFSET:e - E.ELF_BASE + E.ELF_OFFSET]) for s, e in E.DATA_WINDOWS]
    tokens = candidates(windows)
    if not full:
        tokens = tokens[::6]
    modules = E.MODULES if full else [0x23]
    states = [('world', [], world)]
    for m in modules:
        states.append((f'module {m:#x}', [m], fl.page(world, m)))
        if full:
            states.append((f'module {m:#x} + restore', [m, E.RESTORE], fl.page_close(fl.page(world, m))))
    total_decoded = total_refused = 0
    for label, steps, image in states:
        n.em_gs_texture_reset(gs)
        for s in steps:
            assert n.em_gs_texture_apply(gs, s) == 1, (label, s)
        d, r = check_state(n, gs, image, label, tokens)
        total_decoded += d
        total_refused += r
    assert n.em_gs_texture_apply(gs, 0x77) == 0
    n.em_gs_texture_free(gs)
    print(f'mode {"full" if full else "quick"}: {len(states)} GS states x {len(tokens)} TEX0 candidates: '
          f'{total_decoded} decodes equal decode_token_lm, {total_refused} refused (non-resident)')
    print('gs texture reference: PASS')


if __name__ == '__main__':
    main()
