#!/usr/bin/env python3
"""export_game_over.py - compose the game-over screen (screen module 0x27).

001AD4E0 (byte-matched; em_scene_task.c em_sf_001AD4E0) loads screen module
0x27 at its step 1 (001FF080(0, 0x27): DATA.DAT chunk 0x27, the user's
extract/chunk39) and, from step 3 on, hands 001ABF90 four TEX0 words every
tick: 0x2005C00621322A00, 0x2005C08621322A40, 0x2005C20621322C00 and
0x2005C28621322C40 (em_scene_task.c push_packet_001ABF90, the instructions'
immediates). 001ABF90 draws them as four 256 x 256 sprites, top-left,
top-right, bottom-left, bottom-right, in the order the decomp's
export_startup.py models for the logo screens (the same routine, the same
canvas): the module's one PSMCT32 upload holds both the indexed art and its
palettes, so the texels and the CLUTs come from the disc alone.

This tool composes that screen with the decomp's own composer
(../Extermination/tools/export_startup.py: upload_module, compose,
write_emui; imported read-only, nothing there is changed) and writes the
port's asset. It runs natively on arm64 macOS (Python stdlib).

Optional check (developers with the capture): --capture <folder> decodes
the four textures from that capture's GS local memory (gs.bin; the DAMAGE
lane's dmg_02_flame_death end snapshot shows the game-over screen) and
requires every texel and palette entry to equal the disc's decode.

Output (disc-derived: git-ignored assets/ only):
  assets/startup/game_over.emui   EMUI version 1, 512 x 448 (the startup
                                  screens' format, em_frontend.c)
  assets/startup/game_over.json   the source's SHA-256, the TEX0 words and
                                  the check result (no pixels)

Usage (port root):
  python3 tools/export_game_over.py [--extract ../Extermination/extract]
          [--capture ../Extermination/build/c10/damage/dmg_02_flame_death]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
sys.path.insert(0, str(DECOMP / 'tools'))

import export_startup as S  # noqa: E402

MODULE = 0x27
# 001AD4E0's 001ABF90 arguments (top-left, top-right, bottom-left,
# bottom-right), the immediates of its steps 3 and 4.
TEX0S = (0x2005C00621322A00, 0x2005C08621322A40, 0x2005C20621322C00, 0x2005C28621322C40)
GS_LOCALMEM = 0x400000


def capture_local(folder: Path) -> bytes:
    blob = (folder / 'gs.bin').read_bytes()
    base = len(blob) - GS_LOCALMEM - 84       # the freeze layout (gs_vram.py localmem_base)
    if base < 0:
        raise SystemExit(f'{folder}/gs.bin: too short for GS local memory')
    return blob[base:base + GS_LOCALMEM]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extract', type=Path, default=DECOMP / 'extract')
    ap.add_argument('--out', type=Path, default=ROOT / 'assets/startup/game_over.emui')
    ap.add_argument('--capture', type=Path, help='a capture folder whose gs.bin shows the game-over screen')
    args = ap.parse_args(argv)
    paths = sorted((args.extract / f'chunk{MODULE:02d}').glob('*.bin'))
    if len(paths) != 1:
        raise SystemExit(f'expected one section in chunk{MODULE:02d}; got {len(paths)}')
    source = paths[0].read_bytes()
    local, covered = S.upload_module(paths[0])
    pixels = S.compose(local, covered, TEX0S)
    report = {'module': MODULE, 'source': str(paths[0]), 'source_sha256': hashlib.sha256(source).hexdigest(),
              'tex0': [hex(t) for t in TEX0S], 'canvas': [S.WIDTH, S.HEIGHT],
              'pixels_sha256': hashlib.sha256(pixels).hexdigest()}
    if args.capture:
        captured = capture_local(args.capture)
        everything = set(range(GS_LOCALMEM // 256))
        for t in TEX0S:
            if S.texture(captured, everything, t) != S.texture(local, covered, t):
                raise SystemExit(f'{args.capture}: TEX0 {t:#x} decodes differently from the disc upload')
        report['capture_check'] = f'{args.capture}: the four textures and palettes equal the disc decode'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    for path in (args.out, args.out.with_suffix('.json')):
        if path.is_symlink():
            path.unlink()   # a worktree's link to a shared tree: write a file of its own
    S.write_emui(args.out, pixels)
    args.out.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'wrote {args.out}: {S.WIDTH}x{S.HEIGHT} from module {MODULE:#x}'
          + (f'; {report["capture_check"]}' if args.capture else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
