#!/usr/bin/env python3
"""Export original AREA11 elevator scripts without rewriting their commands.

Generated EMSC bytes retain original addresses; scene/controller patches the
three height fields as original00827B10 does for either elevator position.
All outputs contain owner-supplied data and must remain ignored.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BASE, END = 0x82A750, 0x82AB10


def export(overlay):
    if len(overlay) != 0x7800 or overlay[:4] != b'MWo3':
        raise ValueError('Expected original SCUS-97112 AREA11 overlay')
    arena = struct.unpack_from('<I', overlay, 8)[0]
    if arena != 0x823500:
        raise ValueError('Unexpected overlay load base')
    data = overlay[BASE-arena:END-arena]
    expected = [7, 1, 4, 0, 10, 10, 9, 0, 0x80000007,
                7, 1, 4, 13, 12, 0x80000007]
    if [struct.unpack_from('<I', data, i*64)[0] for i in range(15)] != expected:
        raise ValueError('Original elevator script shape differs')
    if struct.unpack_from('<I', data, 0x82A8D4-BASE)[0] != 0x828050:
        raise ValueError('Unexpected elevator movement callback')
    return struct.pack('<4s4I', b'EMSC', 1, BASE, BASE, len(data)) + data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--overlay', type=Path,
        default=ROOT.parent/'Extermination/extract/OVERLAY/AREA11.BIN')
    parser.add_argument('--out', type=Path, default=ROOT/'assets/scene_snow/elevator.emsc')
    parser.add_argument('--decomp', type=Path, default=ROOT.parent/'Extermination',
        help='Matching original executable/text resources for the refusal message')
    args = parser.parse_args()
    blob = export(args.overlay.read_bytes())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    from export_interaction_message import export_message
    message = export_message(args.decomp, 0x1A, args.out.parent/'elevator_refusal.emod')
    report = {'base': BASE, 'end': END, 'records': 15,
              'powered_entry': BASE, 'refusal_entry': 0x82A990,
              'height_fields': [0x82A7C4, 0x82A844, 0x82A944],
              'sha256': hashlib.sha256(blob).hexdigest(), 'refusal_message': message}
    path = ROOT/'build/elevator_reference/export.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2)+'\n')
    print('Exported15 original elevator script records')


if __name__ == '__main__':
    main()
