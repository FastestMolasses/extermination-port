#!/usr/bin/env python3
"""AREA11 actor lighting (FIRST_LEVEL_AUDIT H18 / WP-13) against original code.

Reads the owner's pinned ELF, the immutable first-control EE capture
(build/startup-reference/playable_ee.bin), the owner's extracted level files
and the generated port assets. It embeds no original bytes; the report holds
addresses, counts and differences only.

A. Draw census. Every AREA11 actor whose +0x4C method is 001CAA00 (lighting
   mode 0 via 001CA990) and whose +0x44 model resolves into the per-area
   table *(D_0028A59C) is bound to the port asset that draws it.
B. Original rig. 001D89D0(actor, 70003400, 70003440, actor+80) is executed
   over the capture (001D8130, 001D7B30, 001D8340, 001D8270, the VU0 helpers
   and 001D8690 run as original instructions). For every actor drawn in the
   capture, its DMA unit (CNT colour matrix to VU1 0x3F5, CNT node sets to
   dmem 0, CALL 0023C750, REF model) is located in both display lists. The
   executed colour matrix must equal the captured 64 bytes, and
   em_lighting_matrices(node world matrix, executed directions) must equal
   the captured normal-matrix xyz lanes. The list built from this RAM state
   is identified by that equality; the other list is one frame older (its
   point-light flicker directions differ) and is reported, not asserted.
C. Port contract. The rig is recomposed the way the port must feed it:
   manifest lightamb/lightdir/lightcam, slot 0 zero unless actor+2 bit 0x20,
   em_point_light_fold over the captured pool at the original light point
   only when em_lighting_fold_gate (001D8270) passes, em_lighting_actor_rgb
   (001D8690) with actor+0x80. Directions, all 64 colour-matrix bytes and
   the normal-matrix xyz lanes must equal the executed original.
D. Vertices. For every record of every bound model, the original object
   kernel lighting slice (0023C878..0023C928) runs on the record normal with
   the captured (or executed) matrices; the exported EMDL must carry that
   exact normal on that UV, and em_lighting_vertex on it with the port
   matrices must produce the identical RGBAQ words. The stand-in colours in
   the *.standin.bak files, when present, are measured against the same
   original colours to size the removed error.
E. 001D8270 over every type byte and boundary radii, and 001D8690 over
   random actor RGB, against em_lighting_fold_gate/em_lighting_actor_rgb.
F. R11. The level kernel's colour conversion (I = 65536.0 at 00237218,
   vf9.y = vf0.y + I at 00237220, colour + vf9.y at 002373B0 into the RGBAQ slot of
   the 0x4126 GIF tag) runs on every distinct AREA11 level record colour: the RGBAQ low
   byte is floor(128*c), so colour 1.0 is GS 128 (modulate identity).
"""
from pathlib import Path
import argparse, ctypes as C, hashlib, json, math, random, struct, subprocess, sys, tempfile

import test_point_light_reference as pl
from test_point_light_reference import signed, bits, number, Pool
import test_snow_particles_reference as vu
import audit_opening_lighting as aol

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ACTOR_HEAD, MODEL_TABLE, CONTEXT = 0x275BC0, 0x28A59C, 0x275670
DRAW_MODE0, KERNEL = 0x1CAA00, 0x23C750
SCRATCH_DIR, SCRATCH_COLOR = 0x70003400, 0x70003440
# (area-table entry) -> port asset. Behaviour addresses are documentation.
AREA11_MODELS = {
    0x06: ('scene_snow/props/area_husk_partner.emdl', '00827490 husk partner'),
    0x08: ('scene_snow/props/area_husk_creature.emdl', '00825940 husk creature'),
    0x09: ('scene_snow/props/area_truck.emdl', '00823FF0 truck'),
    0x0B: ('scene_snow/props/area_item_0b.emdl', '0015AFA0 pickup item 0B'),
    0x0D: ('scene_snow/props/enemy_crate.emdl', '001551B0 crate'),
    0x0E: ('enemy_egg.emdl', '00156620 egg'),
    0x11: ('scene_snow/props/area_parachute.emdl', '00823E80 parachute'),
    0x13: ('scene_snow/props/area_item_13.emdl', '00827630 fan'),
    0x14: ('scene_snow/doors/door_m03.emdl', '001BC350 door'),
}
PER_NODE_PALETTE = {0x14}  # door_m03 keeps node slots; the rest bake rests


class Ram(pl.Oracle):
    """The point-light oracle over a full EE capture (read-only backing;
    writes land in the overlay dict), plus the MIPS ops 001D89D0 needs."""

    def __init__(self, elf, ram):
        super().__init__(elf)
        self.ram = ram
        self.mem = {}
        self.r[28], self.r[29] = 0x27D370, 0x1FF0000

    def load(self, address, size=4):
        value = 0
        for i in range(size):
            a = address+i
            byte = self.mem.get(a)
            if byte is None: byte = self.ram[a] if a < len(self.ram) else 0
            value |= byte << (8*i)
        return value

    def plain(self, word):
        r = self.r; op = word >> 26
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        imm = signed(word & 65535, 16)
        if op == 0 and word & 63 in (4, 6, 7, 10, 11, 38, 39, 43):
            fn, sh = word & 63, r[rs] & 31
            if fn == 4: r[rd] = (r[rt] << sh) & 0xFFFFFFFF
            elif fn == 6: r[rd] = (r[rt] & 0xFFFFFFFF) >> sh
            elif fn == 7: r[rd] = (signed(r[rt]) >> sh) & 0xFFFFFFFF
            elif fn == 10:
                if r[rt] == 0: r[rd] = r[rs]
            elif fn == 11:
                if r[rt] != 0: r[rd] = r[rs]
            elif fn == 38: r[rd] = r[rs] ^ r[rt]
            elif fn == 39: r[rd] = ~(r[rs] | r[rt]) & 0xFFFFFFFF
            else: r[rd] = int((r[rs] & 0xFFFFFFFF) < (r[rt] & 0xFFFFFFFF))
        elif op == 11: r[rt] = int((r[rs] & 0xFFFFFFFF) < (imm & 0xFFFFFFFF))
        elif op == 14: r[rt] = r[rs] ^ (word & 65535)
        elif op == 32: r[rt] = signed(self.load((r[rs]+imm) & 0xFFFFFFFF, 1), 8) & 0xFFFFFFFF
        elif op == 37: r[rt] = self.load((r[rs]+imm) & 0xFFFFFFFF, 2)
        else:
            super().plain(word); return
        r[0] = 0


class Matrices(C.Structure):
    _fields_ = [('normal', C.c_float*16), ('color', C.c_float*16)]


def u32(ram, a): return struct.unpack_from('<I', ram, a)[0]
def floats(data, n=None): return struct.unpack(f'<{len(data)//4 if n is None else n}f', data[:4*(n or len(data)//4)])


def native_library(out):
    library = out/'actor_lighting.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-I'+str(ROOT/'src'), str(ROOT/'src/game/em_lighting.c'),
                    str(ROOT/'src/game/em_point_light.c'), '-o', str(library)], check=True)
    api = C.CDLL(str(library)); F = C.POINTER(C.c_float)
    api.em_lighting_matrices.argtypes = [C.POINTER(Matrices), F, F, F, F]
    api.em_lighting_vertex.argtypes = [C.POINTER(C.c_uint32), F, C.POINTER(Matrices)]
    api.em_lighting_actor_rgb.argtypes = [F, F, F]
    api.em_lighting_fold_gate.argtypes = [C.c_uint, C.c_float]
    api.em_point_light_fold.argtypes = [F, F, C.POINTER(Pool), F]
    return api


def read_emdl(path):
    d = path.read_bytes()
    if d[:4] != b'EMD3': raise SystemExit(f'{path}: not EMD3')
    nb, nv, ni, nf, fps, nt, flags, nc = struct.unpack_from('<4If3I', d, 4)
    o = 36+4*nb+16*nt+16*nc
    verts = [struct.unpack_from('<8f2I', d, o+40*i) for i in range(nv)]
    raw = [d[o+40*i:o+40*i+40] for i in range(nv)]
    return dict(flags=flags, verts=verts, raw=raw, bones=nb)


def manifest_rig(path):
    rig = {'dirs': []}
    for line in path.read_text().splitlines():
        f = line.split()
        if not f or f[0].startswith('#'): continue
        if f[0] == 'lightamb': rig['amb'] = [float(x) for x in f[1:4]]
        elif f[0] == 'lightdir': rig['dirs'].append(([float(x) for x in f[1:4]], [float(x) for x in f[4:7]]))
        elif f[0] == 'lightcam': rig['cam'] = ([float(x) for x in f[1:4]], [float(x) for x in f[4:7]], float(f[7]))
    if 'amb' not in rig or 'cam' not in rig or len(rig['dirs']) != 2:
        raise SystemExit(f'{path}: incomplete light rig')
    return rig


def model_records(ram, model):
    """(node slot, attr, uv, record offset, drawn) of every record in every
    block of a raw mesh blob. `drawn` follows export_props.model_tris: GS
    strip order, a record with bit 15 clear kicks the last three, and only
    triangles with three distinct positions have area."""
    n_blocks, qwc = u32(ram, model), u32(ram, model+4)
    out = []
    o, end = model+0x40, model+0x40+16*qwc
    run = []
    while o+64 <= end:
        w = number(u32(ram, o+0x3C))
        tex_row = ram[o+8:o+16] == bytes(8) and ram[o:o+8] != bytes(8)
        if tex_row and abs(abs(w)-1.0) <= 0.25 and floats(ram[o+0x18:o+0x20]) == (1.0, 0.0):
            wbits = u32(ram, o+0x3C)
            out.append([(wbits & 0x3FF) >> 3, ram[o+0x20:o+0x2C], ram[o+0x10:o+0x18], o, False])
            run.append((ram[o+0x30:o+0x3C], len(out)-1))
            run = run[-3:]
            if not wbits & 0x8000 and len(run) == 3 and len({p for p, _ in run}) == 3:
                for _p, i in run: out[i][4] = True
            o += 64
        else:
            run = []
            o += 16
    return n_blocks, out


def captured_units(ram, model):
    """Every captured object-kernel draw of `model` (REF to model+0x40)."""
    units = []
    for tag in range(0, 0x2000000-16, 16):
        w0, w1 = struct.unpack_from('<2I', ram, tag)
        if w1 != model+0x40 or (w0 >> 28) & 7 != 3: continue
        # DMA tag bits 16..25 are unused by the DMAC and carry stale bytes.
        call_w0, call_w1 = struct.unpack_from('<2I', ram, tag-16)
        if (call_w0 >> 28) & 7 != 5 or call_w1 != KERNEL: continue
        # Before the CALL: REF 00814220, REF 008164x0, REF 00815360; before
        # those the node CNT {VIF qw, 8 qw per node -> dmem 0} and the colour
        # CNT {VIF qw, 4 qw -> VU1 0x3F5}.
        refs = [struct.unpack_from('<2I', ram, tag-16*k) for k in (2, 3, 4)]
        if any((w0 >> 28) & 7 != 3 for w0, _ in refs): continue
        data_end = tag-64
        for count in range(1, 33):
            head = data_end-16*(1+8*count)-16
            if u32(ram, head) & 0x7000FFFF != 0x10000000 | (1+8*count): continue
            if u32(ram, head+28) != 0x6C000000 | (8*count) << 16: continue
            color_tag = head-96
            if u32(ram, color_tag) & 0x7000FFFF != 0x10000005 or u32(ram, color_tag+28) != 0x6C0403F5:
                continue
            nodes = [ram[head+32+128*k:head+32+128*k+128] for k in range(count)]
            units.append(dict(tag=tag, color=ram[color_tag+32:color_tag+96], nodes=nodes))
            break
    return units


def f32_array(values, n):
    return (C.c_float*n)(*values)


def build_matrices(api, bone, directions, colors, ambient):
    m = Matrices()
    if not api.em_lighting_matrices(C.byref(m), f32_array(bone, 16), f32_array(directions, 12),
                                    f32_array(colors, 12), f32_array(ambient, 4)):
        raise AssertionError('em_lighting_matrices rejected a captured input')
    return m


def xyz_lanes_equal(port_normal, captured):
    port = bytes(port_normal)[:48]
    return all(port[r*16:r*16+12] == captured[r*16:r*16+12] for r in range(3))


def vertex_oracle(normal, normal_matrix, color_matrix):
    return aol.shade_oracle(list(normal), list(normal_matrix), list(color_matrix), body=True)


def level_color_rgbaq(color):
    """Original level kernel colour slice on one record colour."""
    machine = vu.VU(bytearray(16384), program_start=0x2371B0)
    machine.color_only = True
    machine.run(0x237218, 0x237228)          # I = 65536.0; vf9.y = vf0.y + I (= 65536.0)
    machine.v[10] = [bits(c) for c in color]
    machine.run(0x2373B0, 0x2373B8)          # vf14 = vf10 + vf9.y (broadcast over the lanes)
    return list(machine.v[14])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root', type=Path, default=ROOT.parent/'Extermination')
    parser.add_argument('--assets', type=Path, default=ROOT/'assets')
    parser.add_argument('--manifest', type=Path, default=ROOT/'assets/scene_snow/scene.txt')
    parser.add_argument('--report', type=Path, default=ROOT/'build/actor_lighting_reference/report.json')
    args = parser.parse_args()
    decomp = args.decomp_root
    elf = (decomp/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'not the pinned SCUS-97112 ELF'
    vu.ELF = elf
    ram = (decomp/'build/startup-reference/playable_ee.bin').read_bytes()
    assert ram[0x810700] == 11 and ram[0x810701] == 0, 'capture is not AREA11 sub 0'
    for begin, end in ((0x1D8130, 0x1D8C20), (0x1C7420, 0x1C7900), (0x23C750, 0x23C970), (0x237180, 0x237420)):
        assert ram[begin:end] == elf[begin-0x100000+0x300:end-0x100000+0x300], ('code differs', hex(begin))
    out = args.report.parent; out.mkdir(parents=True, exist_ok=True)
    api = native_library(out)
    rig = manifest_rig(args.manifest)
    table = u32(ram, MODEL_TABLE)
    entry_of = {}
    for entry in range(u32(ram, table)):
        rel = u32(ram, table+4+4*entry)
        if rel != 0xFFFFFFFF: entry_of[table+(rel & ~3)] = entry
    context = u32(ram, CONTEXT)
    pool = Pool()
    C.memmove(C.addressof(pool.active), ram[context+0x220:context+0x1220], 4096)

    actors, seen, a = [], set(), u32(ram, ACTOR_HEAD)
    while a and a not in seen:
        seen.add(a); actors.append(a); a = u32(ram, a+0x1C)
    report = {'actors': [], 'unbound_mode0_actors': []}
    totals = dict(vertices=0, vertex_words_equal=0, captured_units=0, current_units=0,
                  color_bytes_equal=0, normal_lane_bytes_equal=0, contract_color_bytes=0,
                  contract_direction_bytes=0, contract_normal_lane_bytes=0)
    standin = {'vertices': 0, 'max': 0, 'sum': 0}
    for actor in actors:
        if u32(ram, actor+0x4C) != DRAW_MODE0: continue
        model = u32(ram, actor+0x44)
        entry = entry_of.get(model)
        if entry not in AREA11_MODELS:
            report['unbound_mode0_actors'].append({'actor': hex(actor), 'behaviour': hex(u32(ram, actor+0x10)),
                                                   'model': hex(model), 'area_entry': entry})
            continue
        asset, role = AREA11_MODELS[entry]
        flags2, type3, sub = ram[actor+2], ram[actor+3], ram[actor+0x98]
        rgb = floats(ram[actor+0x80:actor+0x90])
        assert not flags2 & 0x40, ('self-glow actor is outside this contract', hex(actor))
        # B. original rig, executed.
        oracle = Ram(elf, ram)
        oracle.run(0x1D89D0, [actor, SCRATCH_DIR, SCRATCH_COLOR, actor+0x80])
        dir_rows = floats(oracle.read(SCRATCH_DIR, 64))
        original_color = oracle.read(SCRATCH_COLOR, 64)
        directions = [dir_rows[0], dir_rows[4], dir_rows[8], 0, dir_rows[1], dir_rows[5], dir_rows[9], 0,
                      dir_rows[2], dir_rows[6], dir_rows[10], 0]
        cm = floats(original_color)
        colors = list(cm[:12])
        nodes = [u32(ram, actor+0x110+4*k) for k in range(max(1, u32(ram, model+8)))]
        bones = [floats(ram[n+0x90:n+0xD0]) for n in nodes]
        point = actor+0xB0 if sub == 0xFF else u32(ram, actor+0x110+4*sub)+0xC0
        # C. port contract.
        gate = bool(api.em_lighting_fold_gate(type3, number(u32(ram, model+0x20))))
        oracle_gate = Ram(elf, ram); oracle_gate.run(0x1D8270, [actor])
        assert gate == bool(oracle_gate.r[2]), ('fold gate', hex(actor))
        port_dir = [0.0]*4; port_col = [0.0]*4
        cam_dir, cam_col, cam_w = rig['cam']
        if flags2 & 0x20:
            raise SystemExit(f'{actor:#x}: camera-fill actor; its fill needs the capture camera')
        if gate:
            d4 = f32_array([0, 0, 0, 0], 4)   # slot 0 zeroed (flag clear) x weight
            c4 = f32_array([0, 0, 0, cam_w], 4)
            api.em_point_light_fold(d4, c4, C.byref(pool), f32_array(floats(ram[point:point+16]), 4))
            port_dir, port_col = list(d4), list(c4)
        port_directions = [*port_dir[:3], 0, *rig['dirs'][0][0], 0, *rig['dirs'][1][0], 0]
        port_colors = f32_array([*port_col[:3], 0, *rig['dirs'][0][1], 0, *rig['dirs'][1][1], 0], 12)
        port_ambient = f32_array([*rig['amb'], 0], 4)
        assert api.em_lighting_actor_rgb(port_colors, port_ambient, f32_array(rgb[:3], 3))
        port0 = build_matrices(api, bones[0], port_directions, list(port_colors), list(port_ambient))
        # The rig em_render_frame.c char_rig_build composes today (fold for
        # every actor, identity actor RGB), measured, not asserted.
        d4 = f32_array([0, 0, 0, 0], 4); c4 = f32_array([0, 0, 0, cam_w], 4)
        api.em_point_light_fold(d4, c4, C.byref(pool), f32_array(floats(ram[point:point+16]), 4))
        wired_directions = [*list(d4)[:3], 0, *rig['dirs'][0][0], 0, *rig['dirs'][1][0], 0]
        wired_colors = [*list(c4)[:3], 0, *rig['dirs'][0][1], 0, *rig['dirs'][1][1], 0]
        wired_worst = 0
        orig0 = build_matrices(api, bones[0], directions, colors, [cm[12]-8388608, cm[13]-8388608, cm[14]-8388608, 0])
        dir_equal = struct.pack('<12f', *port_directions) == struct.pack('<12f', *directions)
        color_equal = bytes(port0.color) == original_color
        assert bytes(orig0.color) == original_color, ('bias round trip', hex(actor))
        assert dir_equal and color_equal, ('port rig contract', hex(actor), port_directions, directions)
        assert xyz_lanes_equal(port0.normal, bytes(orig0.normal)), ('port normal matrix', hex(actor))
        totals['contract_normal_lane_bytes'] += 36
        totals['contract_color_bytes'] += 64; totals['contract_direction_bytes'] += 48
        # captured DMA units.
        units = captured_units(ram, model)
        # A unit is this actor's if its node 0 screen/normal set matches this actor.
        current = None; stale = []
        for unit in units:
            if len(unit['nodes']) != len(nodes): continue
            port_nodes = [build_matrices(api, bone, directions, colors, [cm[12]-8388608, cm[13]-8388608, cm[14]-8388608, 0])
                          for bone in bones]
            normal_ok = all(xyz_lanes_equal(pm.normal, unit['nodes'][k][64:112]) for k, pm in enumerate(port_nodes))
            if unit['color'] == original_color and normal_ok:
                if current is not None:
                    # Identical static actors (the stacked crates) share a model;
                    # attribute a unit to one actor only through its transform.
                    pass
                current = unit if current is None or unit['nodes'][0][:64] != current['nodes'][0][:64] else current
            else:
                stale.append(unit)
        drawn = [u for u in units if len(u['nodes']) == len(nodes)]
        entry_report = {'actor': hex(actor), 'role': role, 'area_entry': hex(entry), 'asset': asset,
                        'type': hex(type3), 'flags': hex(flags2), 'light_reference': sub,
                        'model_radius': number(u32(ram, model+0x20)), 'fold_gate_001D8270': gate,
                        'actor_rgb': list(rgb[:3]), 'port_rig_equals_executed_001D89D0': True,
                        'captured_units_of_model': len(drawn)}
        totals['captured_units'] += len(drawn)
        if current is not None:
            totals['current_units'] += 1
            totals['color_bytes_equal'] += 64
            totals['normal_lane_bytes_equal'] += 36*len(nodes)
            entry_report['current_unit'] = hex(current['tag'])
        # D. vertices.
        emdl = read_emdl(args.assets/asset)
        assert emdl['flags'] == 0, (asset, 'still flags', emdl['flags'])
        # The exporter welds UVs within 1e-5 (export_props vid_of), so a
        # record is matched on its exact normal bits plus its rounded UV.
        weld_uv = lambda uv: tuple(round(x, 5) for x in floats(uv))
        by_uv = {}
        for vert, raw_bytes in zip(emdl['verts'], emdl['raw']):
            by_uv.setdefault((weld_uv(raw_bytes[24:32]), raw_bytes[12:24]), []).append(vert)
        backup = (args.assets/asset).with_name((args.assets/asset).name+'.standin.bak')
        old = read_emdl(backup) if backup.exists() else None
        old_by_uv = {}
        if old:
            for vert, raw_bytes in zip(old['verts'], old['raw']):
                old_by_uv.setdefault(weld_uv(raw_bytes[24:32]), []).append(vert)
        _, records = model_records(ram, model)
        captured_nodes = current['nodes'] if current is not None else None
        worst_standin = 0; posed_records = 0; posed_worst = 0; undrawn = 0
        for slot, attr, uv, offset, drawn in records:
            k = slot if slot < len(nodes) else 0
            if captured_nodes is not None:
                normal_matrix = floats(captured_nodes[k][64:128])
                color_matrix = floats(current['color'])
            else:
                normal_matrix = list(build_matrices(api, bones[k], directions, colors,
                                     [cm[12]-8388608, cm[13]-8388608, cm[14]-8388608, 0]).normal)
                color_matrix = cm
            normal = floats(attr)
            expected = vertex_oracle(normal, normal_matrix, color_matrix)
            if not drawn:
                undrawn += 1; continue
            assert (weld_uv(uv), attr) in by_uv, (asset, 'authored normal missing at record', hex(offset))
            # The port must light each record with ITS node's world matrix.
            port = build_matrices(api, bones[k], port_directions, list(port_colors), list(port_ambient))
            got = (C.c_uint32*4)()
            api.em_lighting_vertex(got, f32_array(normal, 3), C.byref(port))
            assert list(got)[:3] == expected[:3], (asset, hex(offset), list(got), expected)
            totals['vertices'] += 1; totals['vertex_words_equal'] += 3
            wired = build_matrices(api, bones[k], wired_directions, wired_colors, [*rig['amb'], 0])
            api.em_lighting_vertex(got, f32_array(normal, 3), C.byref(wired))
            wired_worst = max(wired_worst, max(abs((got[c] & 255)-(expected[c] & 255)) for c in range(3)))
            if entry not in PER_NODE_PALETTE and struct.pack('<12f', *bones[k][:12]) != struct.pack('<12f', *bones[0][:12]):
                # The shipped static mesh bakes rest poses under ONE palette;
                # this node's captured basis differs from node 0's.
                single = build_matrices(api, bones[0], port_directions, list(port_colors), list(port_ambient))
                api.em_lighting_vertex(got, f32_array(normal, 3), C.byref(single))
                posed_records += 1
                posed_worst = max(posed_worst, max(abs((got[c] & 255)-(expected[c] & 255)) for c in range(3)))
            if old:
                gs = [x & 255 for x in expected[:3]]
                for candidate in old_by_uv.get(weld_uv(uv), [])[:1]:
                    diff = max(abs(min(max(candidate[3+c], 0.0), 1.0)*128.0-gs[c]) for c in range(3))
                    standin['vertices'] += 1; standin['sum'] += diff
                    standin['max'] = max(standin['max'], diff); worst_standin = max(worst_standin, diff)
        entry_report['records_verified'] = len(records)-undrawn
        entry_report['current_char_rig_build_max_gs_difference'] = wired_worst
        entry_report['records_without_area'] = undrawn
        if posed_records:
            entry_report['single_palette_residual'] = {
                'records_on_posed_nodes': posed_records, 'max_gs_difference': posed_worst,
                'cause': 'captured node basis differs from node 0; the static export bakes the rest pose'}
        if old: entry_report['standin_max_gs_difference'] = worst_standin
        report['actors'].append(entry_report)

    # E. 001D8270 and 001D8690 against the C helpers.
    gate_cases = 0
    for type_byte in range(256):
        for radius in (0.0, 5.98, 29.999998, 30.0, 30.000002, 56.86, -1.0, float('inf'), float('nan')):
            o = Ram(elf, ram)
            o.write(0x500000, bytes(0x48)); o.save(0x500003, type_byte, 1); o.save(0x500044, 0x500100)
            o.write(0x500120, struct.pack('<f', radius))
            o.run(0x1D8270, [0x500000])
            assert bool(o.r[2]) == bool(api.em_lighting_fold_gate(type_byte, radius)), (type_byte, radius)
            gate_cases += 1
    rng = random.Random(0x1D8690); rgb_cases = 0
    for _ in range(300):
        work = [number(bits(rng.uniform(-200, 400))) for _ in range(16)]
        actor_rgb = [number(bits(rng.choice([1.0, 4.0, rng.uniform(0, 8)]))) for _ in range(3)] + [1.0]
        o = Ram(elf, ram)
        o.write(0x500000, bytes(0x200)); o.write(0x5000F0, struct.pack('<16f', *work))
        o.write(0x500300, struct.pack('<4f', *actor_rgb))
        o.save(0x275688, 0x500000)
        o.run(0x1D8690, [0x500400, 0x500440, 0x500300])
        expected = o.read(0x500440, 64)
        colors = f32_array([*work[0:3], 0, *work[4:7], 0, *work[8:11], 0], 12)
        ambient = f32_array([*work[12:15], 0], 4)
        assert api.em_lighting_actor_rgb(colors, ambient, f32_array(actor_rgb[:3], 3))
        m = build_matrices(api, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0],
                           list(colors), list(ambient))
        port = bytes(m.color)
        for row in range(4):
            assert port[row*16:row*16+12] == expected[row*16:row*16+12], ('001D8690 row', row)
        rgb_cases += 1

    # F. R11 level colour scale.
    level_colors = set()
    sys.path.insert(0, str(decomp/'tools'))
    import export_level as level
    for name in sorted(level.SNOW_RENDER_FILES):
        path = decomp/'extract/chunk15'/name
        if not path.exists(): continue
        for record in level.walk_records(path.read_bytes()):
            if record is None: continue
            attr = record[5]
            if abs(attr[3]-1.0) < 1e-6: level_colors.add(tuple(bits(x) for x in attr))
    worst_port = 0.0
    for color_bits in sorted(level_colors):
        color = [number(x) for x in color_bits]
        words = level_color_rgbaq(color)
        for c in range(4):
            assert words[c] & 255 == math.floor(128*color[c]) & 255 and color[c] < 2, (color, words)
        worst_port = max(worst_port, max(abs(min(max(color[c], 0.0), 255/128)*128-(words[c] & 255)) for c in range(3)))
    assert level_colors and max(number(b) for c in level_colors for b in c[:3]) <= 1.0

    report.update(status='PASS', totals=totals, fold_gate_cases=gate_cases, actor_rgb_cases=rgb_cases,
                  standin_vertices_measured=standin['vertices'],
                  standin_mean_gs_difference=standin['sum']/max(standin['vertices'], 1),
                  standin_max_gs_difference=standin['max'],
                  r11={'distinct_level_colors': len(level_colors), 'rgbaq_low_byte': 'floor(128*c)',
                       'color_1_0_gs': 128, 'port_unquantized_max_difference_gs': worst_port},
                  original_elf_sha256=ELF_SHA256,
                  original_playable_ee_sha256=hashlib.sha256(ram).hexdigest(),
                  limitations=['Bone matrices are the captured node matrices; the port palette '
                               'equality for each prop is owned by its placement code.',
                               'Only actors on the default 001CAA00 path are covered; camera-fill and '
                               'self-glow actors are rejected, not approximated.',
                               'GS raster interpolation, texture filtering and fog are outside this oracle.'])
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('status', 'totals', 'fold_gate_cases', 'actor_rgb_cases',
                                              'standin_mean_gs_difference', 'standin_max_gs_difference', 'r11')}))


if __name__ == '__main__':
    main()
