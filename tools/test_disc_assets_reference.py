#!/usr/bin/env python3
"""Check that the first level's assets come from the user's disc alone.

docs/DISC_TEXTURES.md section 9 ("every first-level asset from the disc").
The textures themselves are tools/test_disc_textures_reference.py's; this
test covers the rest of what the capture-free exporters rebuild:

A. The resource table. tools/export_disc_textures_gs.py ResourceTable
   rebuilds D_0028A490[0 .. 0xAF) (resource slots and the loader cursors
   D_0028A734 .. D_0028A748) from the disc with the loaders' rules; every
   word must equal the captured RAM (quick: 3 AREA11 captures; full: the
   playable, opening and handoff images and route beats 00..14). Its
   cursors must equal export_module_loader.py's AREA11_SEEDS, and the
   bytes the model leaves at the static bank D_0028A490[0x44] must equal
   the captured bank (quick: route 04; full: every capture).
B. The weather bits. 001B0250's record word D_0024D650[11][0] + 0x1C,
   masked 0x0E000070, must equal the captured D_008106C8's bits (with the
   event byte D_00810788 clear) in every capture checked.
C. The exporters, run without a capture (their --no-verify / optional
   check off) into scratch, each output against the pinned SHA-256 of the
   capture-derived file (PINNED_SHA256, taken from assets/ before the
   exporters became disc-first):
     quick: the fence door, Roger's resources and encounter files, the
       flame, the snow, the panel, the ITEM root, the AREA11 props and
       pickup bodies / lights;
     full: also the status hub (atlas equal; EMHS equal except the arc
       words 00208AD0 writes before they are read, export_status_hub.py
       ARC_WORDS_WRITTEN), the Roger banks (EMRS v2: its 0xAF table words
       and regions equal the v1 file's), the player / world / static
       models, the module loader pack, the weapon sprite sheets (the
       decomp's export_props.py --fx --p2s over the rebuilt memory), and
       player.emdl: the decomp's export_native.py --attach bake with
       --p2s over the rebuilt memory is byte-identical to the same bake
       with --gsdump extract/gsdump/frame1.gs (when that dump exists), and
       every texture of the installed player.emdl equals it.
D. Controls, each of which must be caught: the resource table without the
   title's module 1 (slot 6), 001FB3E0's bank end without its 0x40, the
   area load's cursor from the resident offset, the weather word of the
   wrong room entry masked differently (entry 0x30 bytes off), an EMHS word
   outside ARC_WORDS_WRITTEN, and a one-byte change in an exporter's
   output.

The ELF, the disc image and the captures are the user's own local files;
nothing original is embedded. Needs the disc image (default
../Extermination/Extermination-rebuilt.iso) and the route captures.
"""
from __future__ import annotations

import hashlib
import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import export_disc_textures_gs as G  # noqa: E402
from reference_mode import FULL, banner, parallel_map  # noqa: E402

DECOMP = G.DECOMP
sys.path.insert(0, str(DECOMP / 'tools'))
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
OUT = ROOT / 'build/disc_assets_reference'
PY = sys.executable
VENV = DECOMP / '.venv/bin/python'

# The capture-derived files (their SHA-256), pinned from assets/ before the
# exporters became disc-first, so the check stays independent of assets/.
PINNED_SHA256 = {
    'scene_snow/door_original/model.emdl':  # sha256
        'd5b0e9c8cb1dedc7dfb933de7e9012f476bbeefd1c0cc6c41d4ff44477e002d2',
    'scene_snow/door_original/channels.empc':  # sha256
        '9b7da35543a3c2bb8980c67f4f55daedac876876c12e944db36d181cf21743bc',
    'scene_snow/door_original/source.emdo':  # sha256
        '84c26fc5a7cf1441e4e95867fb909600acd2b1464ffcb0c36ee7aedd57d9ab80',
    'scene_snow/roger/channels.empc':  # sha256
        '43f57fbb422e537f5e3df43fb3cba7160957b5056bb44779999d75c5079139cb',
    'scene_snow/roger/programs.emsc':  # sha256
        '79eed095c996e921ffe16e21b78cfaaac583a6c6ac39e0a1277c094840b7fa01',
    'scene_snow/roger/trigger.empg':  # sha256
        '84608d5663014eb267e19b01f9e2e515c02662aa2f9adde17f9601e8125c89a7',
    'scene_snow/roger/roger.emdl':  # sha256
        '7125d62a98ba361a25f29c305f7849f080d9207e47e84691c545a02a593b8991',
    'scene_snow/roger/encounter_roger.empc':  # sha256
        'ecea8e91609600b5605040ba0de801a538f9d7b23ca9e84fc9edccc40611ab09',
    'scene_snow/roger/encounter_player.empc':  # sha256
        'dd0bb850ad487bfc618689c088ce4c38ff1c72de84de57d7d007eb92ec0178f5',
    'scene_snow/roger/encounter_camera.emcc':  # sha256
        '0d2cd35df40bae5ed21464656990e0ec1797c5d38b7ef4b59fe6d70464f07eba',
    'scene_snow/roger/camera_projection.emcp':  # sha256
        '6d8827cd46a0d1fd4261a18e30f66c8f6a58eb3264f50af1fdbb1d1e9177267a',
    'scene_snow/area11_effect.emef':  # sha256
        '795186413bee888f8f79dd37bbc61361fab6d22b9da28a50a57381bf9df76717',
    'scene_snow/snow.emsn':  # sha256
        'b372815fa238155661309e32a5bd9a500d6d5a97bea08f7496595677af453602',
    'scene_snow/panel/scripts.emsc':  # sha256
        '718786eb3d62d89522ebca1e4a3b4818204f41b8f7e06dff5b7387603cc1dc61',
    'scene_snow/panel/battery.emba':  # sha256
        '405e617fb6c9272a6e712d81fb7dc98fffa8b77d613510fbae97442af8ed2b29',
    'scene_snow/panel/player_15c.bin':  # sha256
        '008f58cc0c8d501abdb6d5b0dc4a8072b4b6b7d05662b6f86d30437ed14387e1',
    'scene_snow/panel/item_root.emir':  # sha256
        '951c903c461d62eb7e1008e58ee9f942bde0cad9194d89330d9d937e9ae5a5ab',
    'scene_snow/props/area_item_04.emdl':  # sha256
        '54793013cf2fef5839e2a22a33f9f607f56e56ef13b5fe144e83079240e21544',
    'scene_snow/props/area_elevator.emdl':  # sha256
        '70b81c7dc9a1d09843c6400538a4d4821dd076c3a979d891e2b14e26f99e67d9',
    'scene_snow/props/area_indicator_10.emdl':  # sha256
        'e564ef111ca56a4fd66c365ca935d49dde01f754d064335bab2e64acd51dcf85',
    'scene_snow/props/item_75.emdl':  # sha256
        '016a71d85eaa488fe656b6ae68948c4592af938db0bb3f32408acc2797c0cf0a',
    'scene_snow/props/item_72.emdl':  # sha256
        '08e6bd0825e41a1608c30bb0d9b50ab2aac4a2aaf8a55583faa63bd8ff1335d0',
    'scene_snow/props/item_73.emdl':  # sha256
        '579964403a522877f6c1fc54c5c1f2b8b72910d1a1476fe648314762b62ad564',
    'scene_snow/props/panel_cell18.emcb':  # sha256
        '628bd60907b451348938acb7fff937c79e1842eea7577f50ed24c226f62a3e31',
    'scene_snow/panel/status_hub_atlas.emha':  # sha256
        '687e18c4a5177d7c5946c26194784bb127cab257908d4e07990c8cf4c21f787e',
    'scene_snow/player_model.emom':  # sha256
        '13d6fd5c110326a89e0940158d24ead25f5b81c1079c43158d1609ba6d84f7e1',
    'scene_snow/world_models.emwm':  # sha256
        '2b59e06d014001bbd2eab0eef4569d3b2c794328e81d101f58ddaa46fddf7139',
    'scene_snow/static_world.emsw':  # sha256
        '568ba63d44998d910a3da9d3ff91b912776a2254f9c6ae88bd85c7f42226f049',
    'module_loader/modules.emml':  # sha256 (EMML version 2 since chain step H7: module 3 and AREA11)
        'cb4865ad6d04daebc9e4080a6e7f1f9051c67c01c51a2451ca3f53f29e0f9232',
    'fx/flash_ball.emtx':  # sha256
        '74873c3eab48bbd94c075639915662852ee7ff88d1426c5db80221aa4728759a',
    'fx/flash_puff.emtx':  # sha256
        '373a77806be96a93c6f315cd290a824b668b2a601f42b84ed457ca29247d5e7d',
    'fx/flash_star.emtx':  # sha256
        '5bdc8a399fb2bf36c246aae64bfad645028b365ac8973ebf9f68dff9e35363fd',
    'fx/laser_dot.emtx':  # sha256
        '440ef73296405910edba324c747c4c4839155f66abd2b1e05e5b1475735d6ea6',
}
# The capture-derived EMHS v2 and EMRS v1 differ from the disc versions by
# construction: the EMHS in the arc words EMHS_CAPTURE_WORDS (a subset of
# export_status_hub.py ARC_WORDS_WRITTEN; pinned below as the digest of the
# capture-derived file with those words zeroed), the EMRS in its table length
# (v1 held 0xC0 words taken from a capture; v2 the 0xAF words the disc gives).
PINNED_EMHS_ZEROED_SHA256 = '57d21185d6514a52a1fad40ef92eef2c7420167dfe081031a1027f165905f299'  # sha256
PINNED_EMRS_V1_TABLE_SHA256 = '44485430f1420059b77d096f67ba7ae579274dfdae539ea5549800c1dea2eef6'  # sha256 of words 0..0xAF
PINNED_EMRS_V1_REGIONS_SHA256 = '2967c9f179b8a56ca0d7890318d40333b569bf127efa5bdcbc48aa64fbdedd6a'  # sha256
# The arc words where the disc EMHS differs from the capture-derived one
# (export_status_hub.py over the status-hub capture): centres and angles.
EMHS_CAPTURE_WORDS = {0, 1, 3, 24, 25, 48, 49, 51, 72, 73, 74, 75}

QUICK_CAPTURES = ('startup-reference/playable_ee.bin', 's87/route/04_elevator_ride/eeMemory.bin',
                  's87/route/14_roger_encounter/eeMemory.bin')


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def captures() -> list:
    base = DECOMP / 'build'
    if not FULL:
        return [base / c for c in QUICK_CAPTURES]
    out = [REF / n for n in ('playable_ee.bin', 'opening_ee.bin', 'handoff_ee.bin')]
    return out + sorted(p for p in ROUTE.glob('*/eeMemory.bin') if p.parent.name[:2].isdigit()
                        and int(p.parent.name[:2]) <= 14)


def read_words(path: Path, address: int, count: int) -> list:
    with open(path, 'rb') as f:
        f.seek(address)
        return list(struct.unpack(f'<{count}I', f.read(4 * count)))


# ======================================================================
# A / B. The resource table and the weather bits
# ======================================================================

def check_table(disc) -> int:
    import export_module_loader as eml
    table = G.ResourceTable(disc)
    n = 0
    for path in captures():
        got = read_words(path, G.TABLE_ADDRESS, G.TABLE_WORDS)
        bad = [i for i in range(G.TABLE_WORDS) if got[i] != table.words[i]]
        assert not bad, (path, [(hex(i), hex(got[i]), hex(table.words[i])) for i in bad])
        n += G.TABLE_WORDS
    assert eml.disc_seeds(G.ISO_PATH) == eml.AREA11_SEEDS
    # the static bank the model leaves at D_0028A490[0x44] (0x2D6FE0 bytes)
    bank_at, size = table.words[0x44], 0x2D6FE0
    bank = table.resident_bytes(bank_at, size)
    for path in captures()[1:2] if not FULL else captures():
        with open(path, 'rb') as f:
            f.seek(bank_at)
            assert f.read(size) == bank, (path, 'static bank')
    return n


def weather_word(elf: bytes, entry: int = 0, stride: int = 0x30) -> int:
    rd = lambda a: struct.unpack_from('<I', elf, a - 0x100000 + 0x300)[0]  # noqa: E731
    room = rd(rd(0x24D650 + 11 * 4) + 0 * 4)
    return rd(room + entry * stride + 0x1C) & 0x0E000070


def check_weather(elf: bytes) -> int:
    want = weather_word(elf)
    assert want == 0x10
    n = 0
    for path in captures():
        with open(path, 'rb') as f:
            f.seek(0x8106C8)
            word = struct.unpack('<I', f.read(4))[0]
            f.seek(0x810788)
            flag = f.read(1)[0]
        assert flag == 0 and word & 0x0E000070 == want, (path, hex(word))
        n += 1
    return n


# ======================================================================
# C. The exporters without a capture
# ======================================================================

def run(args, cwd=ROOT):
    r = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode:
        raise RuntimeError(f'{args}: exit {r.returncode}\n{r.stdout[-2000:]}')
    return r.stdout


def job(name):
    """One exporter run into OUT/<name>; returns (name, {pinned file: sha256})."""
    out = OUT / name
    out.mkdir(parents=True, exist_ok=True)
    freeze = str(OUT / 'first_level_gs.bin')
    t = ROOT / 'tools'
    scene = out / 'scene_snow'
    if name == 'door':
        run([PY, str(t / 'export_door_original.py'), '--scene', str(scene), '--gs', freeze, '--no-verify'])
        files = {f'scene_snow/door_original/{f}': scene / 'door_original' / f
                 for f in ('model.emdl', 'channels.empc', 'source.emdo')}
    elif name == 'roger':
        r = scene / 'roger'
        run([PY, str(t / 'export_roger_resources.py'), '--out', str(r), '--gs', freeze, '--no-verify'])
        run([PY, str(t / 'export_roger_encounter_actor.py'), '--out', str(r / 'encounter_roger.empc'), '--no-verify'])
        run([PY, str(t / 'export_roger_cinematic.py'), '--out', str(r), '--no-verify'])
        files = {f'scene_snow/roger/{f}': r / f for f in (
            'channels.empc', 'programs.emsc', 'trigger.empg', 'roger.emdl', 'encounter_roger.empc',
            'encounter_player.empc', 'encounter_camera.emcc', 'camera_projection.emcp')}
    elif name == 'effect_snow':
        scene.mkdir(parents=True, exist_ok=True)
        run([str(VENV if VENV.exists() else PY), str(t / 'export_area11_effect.py'), '--out', str(scene)])
        run([PY, str(t / 'export_snow.py'), '--out', str(scene)])
        files = {'scene_snow/area11_effect.emef': scene / 'area11_effect.emef',
                 'scene_snow/snow.emsn': scene / 'snow.emsn'}
    elif name == 'panel':
        p = scene / 'panel'
        run([PY, str(t / 'export_panel.py'), '--out', str(p)])
        run([PY, str(t / 'export_item_root.py'), '--out', str(p)])
        files = {f'scene_snow/panel/{f}': p / f for f in ('scripts.emsc', 'battery.emba', 'player_15c.bin',
                                                           'item_root.emir')}
    elif name == 'props':
        # The manifest the two exporters patch: step 10's (export_level.py's
        # pickup lines, from the ELF and overlay), copied as their base.
        scene.mkdir(parents=True, exist_ok=True)
        (scene / 'scene.txt').write_bytes((ROOT / 'assets/scene_snow/scene.txt').read_bytes())
        run([PY, str(t / 'export_area11_props.py'), '--scene', str(scene), '--gs', freeze])
        run([PY, str(t / 'export_pickup_lights.py'), '--scene', str(scene), '--gs', freeze])
        files = {f'scene_snow/props/{f}': scene / 'props' / f for f in (
            'area_item_04.emdl', 'area_elevator.emdl', 'area_indicator_10.emdl', 'item_75.emdl', 'item_72.emdl',
            'item_73.emdl', 'panel_cell18.emcb')}
    elif name == 'hub':
        p = scene / 'panel'
        run([PY, str(t / 'export_status_hub.py'), '--out', str(p)])
        import export_status_hub as H
        emhs = (p / 'status_hub.emhs').read_bytes()
        assert H.arc_words_differing(emhs, emhs) == set()
        return name, {'scene_snow/panel/status_hub_atlas.emha': sha((p / 'status_hub_atlas.emha').read_bytes())}, \
            {'emhs': emhs}
    elif name == 'banks':
        run([PY, str(t / 'export_roger_banks.py'), '--out', str(out / 'resources.emrs'), '--no-verify'])
        data = (out / 'resources.emrs').read_bytes()
        head = struct.unpack_from('<4s7I', data)
        assert head[:4] == (b'EMRS', 2, G.TABLE_ADDRESS, G.TABLE_WORDS), head
        return name, {}, {'table': sha(data[0x20:0x20 + 4 * G.TABLE_WORDS]),
                          'regions': sha(data[0x20 + 4 * G.TABLE_WORDS:])}
    elif name == 'models':
        run([PY, str(t / 'export_player_model.py'), '--out', str(scene / 'player_model.emom'), '--no-verify'])
        run([PY, str(t / 'export_world_models.py'), '--out', str(scene / 'world_models.emwm'), '--no-verify'])
        run([PY, str(t / 'export_static_world.py'), '--out', str(scene / 'static_world.emsw'), '--no-verify'])
        run([PY, str(t / 'export_module_loader.py'), '--out', str(out / 'module_loader/modules.emml')])
        files = {'scene_snow/player_model.emom': scene / 'player_model.emom',
                 'scene_snow/world_models.emwm': scene / 'world_models.emwm',
                 'scene_snow/static_world.emsw': scene / 'static_world.emsw',
                 'module_loader/modules.emml': out / 'module_loader/modules.emml'}
    elif name == 'fx':
        run([str(VENV if VENV.exists() else PY), str(DECOMP / 'tools/export_props.py'), '--fx', '--p2s', freeze,
             '--fx-outdir', str(out / 'fx')], cwd=DECOMP)
        files = {f'fx/{f}': out / 'fx' / f for f in ('flash_ball.emtx', 'flash_puff.emtx', 'flash_star.emtx',
                                                    'laser_dot.emtx')}
    elif name == 'player':
        return name, {}, {'player': player_emdl(freeze)}
    else:
        raise AssertionError(name)
    return name, {k: sha(v.read_bytes()) for k, v in files.items()}, {}


PLAYER_CLIPS = ('349,2,3,69,67,75,272,273,283,51,274,275,276,277,278,279,280,281,282,1,267,268,269,270,271,'
                '0,450,10,70,68,30,31,32,33,86,87,42,92,452,455,53,54,94,115,375,36,44,45,46')


def emdl_textures(data: bytes):
    nb = struct.unpack_from('<I', data, 4)[0]
    nt = struct.unpack_from('<I', data, 24)[0]
    at = 36 + 4 * nb
    entries = [struct.unpack_from('<4I', data, at + 16 * i) for i in range(nt)]
    total = sum(4 * w * h for w, h, _o, _z in entries)
    return entries, data[len(data) - total:]


def player_emdl(freeze: str) -> dict:
    """STARTUP.md step 6 with the rebuilt memory (--p2s) and, when the
    user's GS dump exists, with --gsdump: the two bakes must be identical;
    the installed player.emdl's textures must equal the disc bake's."""
    out = OUT / 'player'
    base = [str(VENV if VENV.exists() else PY), str(DECOMP / 'tools/export_native.py'), '--attach', '--no-glow',
            '--mesh', 'extract/chunk28/f00_id3b.bin', '--anim', 'extract/chunk28/f01_id3c.bin',
            '--clips', PLAYER_CLIPS]
    run(base + ['--p2s', freeze, '--out', str(out / 'disc.emdl')], cwd=DECOMP)
    disc = (out / 'disc.emdl').read_bytes()
    result = {'disc': sha(disc)}
    dump = DECOMP / 'extract/gsdump/frame1.gs'
    if dump.exists():
        run(base + ['--gsdump', str(dump), '--out', str(out / 'gsdump.emdl')], cwd=DECOMP)
        assert (out / 'gsdump.emdl').read_bytes() == disc, 'player.emdl: the GS dump and the disc differ'
        result['gsdump'] = 'identical'
    installed = ROOT / 'assets/player.emdl'
    if installed.exists():
        assert emdl_textures(installed.read_bytes()) == emdl_textures(disc), 'player.emdl textures'
        result['installed_textures'] = 'identical'
    return result


# ======================================================================
# D. Controls
# ======================================================================

def controls(disc, elf: bytes) -> int:
    caught = []
    path = captures()[0]
    got = read_words(path, G.TABLE_ADDRESS, G.TABLE_WORDS)
    # 1. no title module 1 (its pointer word, slot 6)
    orig = G.ResourceTable._module

    def skip_title(self, caller, sector, base):
        return base if sector == 1 else orig(self, caller, sector, base)
    G.ResourceTable._module = skip_title
    try:
        caught.append(('no module 1', G.ResourceTable(disc).words != got))
    finally:
        G.ResourceTable._module = orig
    # 2. 001FB3E0's bank end without its + 0x40
    orig_end = G.bank_end
    G.bank_end = lambda head, address: (address + G.u32(head, 0x10)) & ~0x3F
    try:
        caught.append(('bank end', G.ResourceTable(disc).words != got))
    finally:
        G.bank_end = orig_end
    # 3. the area's pointer words from the region start instead of the
    #    resident base (the extract-naming slip of DISC_TEXTURES.md 2)
    t = G.ResourceTable(disc)
    area = [(s, b) for c, sec, b, _n, slots in t.loads if sec == 15 for s in slots]
    top = G.top_block(disc, 15)
    shifted = list(t.words)
    for slot, _b in area:
        shifted[slot] -= top.resident
    caught.append(('resident base', shifted != got))
    # 4. the weather word read with a record stride of 0x20
    caught.append(('weather stride', weather_word(elf, 1, 0x20) != 0x10 or weather_word(elf, 2, 0x28) != 0x10))
    # 5. an EMHS word outside ARC_WORDS_WRITTEN, and a byte outside the arc block
    import export_status_hub as H
    a = bytearray(24 + 384 + 8)
    b = bytearray(a)
    b[24 + 4 * 5] ^= 1
    caught.append(('emhs word', H.arc_words_differing(bytes(a), bytes(b)) == {5} and 5 not in H.ARC_WORDS_WRITTEN))
    c = bytearray(a)
    c[24 + 384 + 2] ^= 1
    try:
        H.arc_words_differing(bytes(a), bytes(c))
        caught.append(('emhs outside the arc block', False))
    except SystemExit:
        caught.append(('emhs outside the arc block', True))
    # 6. a one-byte change in an exporter's output against its pin
    data = bytearray((OUT / 'door/scene_snow/door_original/source.emdo').read_bytes())
    data[20] ^= 1
    caught.append(('output byte', sha(bytes(data)) != PINNED_SHA256['scene_snow/door_original/source.emdo']))
    missed = [n for n, ok in caught if not ok]
    assert not missed, ('controls not caught', missed)
    return len(caught)


def main() -> int:
    if not G.ISO_PATH.exists():
        raise SystemExit(f'{G.ISO_PATH}: the disc image is required')
    missing = [p for p in captures() if not p.exists()]
    if missing:
        raise SystemExit(f'missing captures: {missing}')
    OUT.mkdir(parents=True, exist_ok=True)
    elf = G.ELF_PATH.read_bytes()
    disc = G.Disc()
    G.first_level_freeze(OUT / 'first_level_gs.bin', G.FirstLevel(disc).world())
    words = check_table(disc)
    weather = check_weather(elf)
    jobs = ['door', 'roger', 'effect_snow', 'panel', 'props']
    if FULL:
        jobs += ['hub', 'banks', 'models', 'fx', 'player']
    cost = {'hub': 20, 'player': 4, 'models': 4, 'props': 3, 'panel': 3}
    results = parallel_map(job, jobs, cost=lambda j: cost.get(j, 2))
    files = 0
    extra = {}
    for name, digests, more in results:
        for key, digest in digests.items():
            assert PINNED_SHA256[key] == digest, ('differs from the capture-derived file', key)
            files += 1
        extra[name] = more
    if FULL:
        import export_status_hub as H
        emhs = bytearray(extra['hub']['emhs'])
        for w in EMHS_CAPTURE_WORDS:
            emhs[24 + 4 * w:28 + 4 * w] = bytes(4)
        assert sha(bytes(emhs)) == PINNED_EMHS_ZEROED_SHA256, 'EMHS outside the capture-moment arc words'
        assert EMHS_CAPTURE_WORDS <= H.ARC_WORDS_WRITTEN
        assert extra['banks'] == {'table': PINNED_EMRS_V1_TABLE_SHA256, 'regions': PINNED_EMRS_V1_REGIONS_SHA256}, \
            extra['banks']
        files += 3
    n_controls = controls(disc, elf)
    banner(f'{len(captures())} captures x {G.TABLE_WORDS} D_0028A490 words ({words} compared)',
           f'{weather} weather words', f'{len(jobs)} exporter groups, {files} files equal to the capture-derived',
           f'{n_controls} controls caught',
           'player.emdl ' + (str(extra['player']['player']) if FULL else 'in full mode'))
    print('PASS test_disc_assets_reference')
    return 0


if __name__ == '__main__':
    sys.exit(main())
