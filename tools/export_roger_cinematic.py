#!/usr/bin/env python3
"""Export the original bank96 encounter camera and player source channels.

The user's disc-derived output stays under ignored assets. This does not
install the encounter's script, player, audio or camera ownership workers.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

from export_opening_camera import export as export_camera

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
BANK = 0x41000


def main():
    sys.path.insert(0, str(DECOMP / 'tools'))
    from export_opening_actors import OpeningClip
    source = DECOMP / 'extract/chunk15/f12_id44.bin'
    data = source.read_bytes()
    ram = (DECOMP / 'build/startup-reference/playable_ee.bin').read_bytes()
    runtime = struct.unpack_from('<I', ram, 0x28A490 + 0x96 * 4)[0]
    assert runtime and data[BANK:BANK+64] == ram[runtime:runtime+64]
    assert struct.unpack_from('<I', data, BANK)[0] == 3
    header = BANK + (struct.unpack_from('<I', data, BANK+8)[0] & ~3)
    clip = OpeningClip(data, header)
    bones, duration, following, blend = struct.unpack_from('<HHhh', data, header)
    assert (bones, duration, following, blend) == (21, 691, -2, 0)
    assert struct.unpack_from('<I', data, header+20)[0] == 0
    payload = bytearray(struct.pack('<4sIII21iHHhH', b'EMPC', 1, bones, 1,
                                   *clip.parents, 1, duration, following, blend))
    keys = 0
    for bone in range(bones):
        for channel in (clip.rotation[bone], clip.translation[bone], clip.scale[bone]):
            payload += struct.pack('<I', len(channel.keys))
            keys += len(channel.keys)
            for time, values, hold in channel.keys:
                payload += struct.pack('<HH4f', time, int(hold), *values,
                                       *((0.,) * (4-len(values))))
    output = ROOT / 'assets/scene_snow/roger'
    output.mkdir(parents=True, exist_ok=True)
    player_path = output / 'encounter_player.empc'
    player_path.write_bytes(payload)
    camera_path = output / 'encounter_camera.emcc'
    camera = export_camera(source, BANK, camera_path)
    assert camera['duration'] == duration
    report = {'source_sha256': hashlib.sha256(data).hexdigest(), 'bank': BANK,
              'runtime_bank': runtime, 'player_header': header, 'player_keys': keys,
              'player_sha256': hashlib.sha256(payload).hexdigest(), 'camera': camera,
              'scope': 'original resources; encounter script and host ownership separate'}
    folder = ROOT / 'build/roger_cinematic'
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'export.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Roger encounter bank96: {duration} source frames, {keys} player keys exported')


if __name__ == '__main__':
    main()
