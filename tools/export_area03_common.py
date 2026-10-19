"""AREA03 sub-0 inputs for the existing descriptor-driven asset pipelines.

Only the two recorded arrival snapshots are covered. Source addresses name
the original tables; their contents are always read from the local disc.
The turret's four 001A2370 calls use *(node + 0x11C) + 0x90
(func_overlay_AREA03_008238F0.c). No AREA03 overlay C calls 0019C6F0,
001BC150 or 001BC240. Unexplained cell or overlay mutations still fail.
"""
from pathlib import Path
import re
import struct

import export_area13_level as LV
import export_area13_tables as TB
import export_area13_sfx as SX
import level_asset_profiles as P

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'assets/levels/03'
SCRATCH = ROOT/'build/levels/area03-assets'
A13, C = LV.A13, LV.C
SCRIPTS = (0x827170, 0x827630, 0x8282D0, 0x828BD0, 0x828CD0)
_check_ctx_block = LV.E22.check_ctx_block


def retained_colour(elf):
    """001D4440's three colour stores from the preceding AREA15 owner.

    AREA15 00826850 installs draw class 12 for kind 0x0D. Its a15b_00
    capture retains that node and its colour. The original integer
    conversion executes; unrelated display-list workers are boundaries.
    This derives the bytes, rather than treating them as an ignored range.
    """
    import test_effect_manager_reference as E
    cap = C.Capture(A13.DECOMP/'build/s87/route_a15b/a15b_00_event')
    nodes = [n for _, n in LV.E02.pool(cap.ram)
             if C.u32(cap.ram, n+0x10) == 0x826850
             and C.u32(cap.ram, n+0x4C) == 0x1CB2B0]
    if len(nodes) != 1 or cap.ram[nodes[0]+0x0D] != 0x0D:
        raise ValueError('retained AREA15 class-12 source is missing or ambiguous')
    node = nodes[0]
    ram = bytearray(cap.ram)
    ram[0x251C20:0x251C2C] = bytes([0x5A])*12
    oracle = E.Oracle(elf, ram, bytearray(cap.spad))

    def boundary(machine):
        machine.set32(2, 0)

    for address in (0x1D2090, 0x1D6930, 0x1D1F80, 0x1D6580,
                    0x1D1F20, 0x1D6BA0, 0x1D1FF0, 0x1D2910):
        oracle.stubs[address] = boundary
    oracle.run(0x1D4440, (3, C.u32(cap.ram, node+0x44), node+0x80))
    return bytes(oracle.ram[0x251C20:0x251C2C])


def check_ctx_block(caps, elf):
    """Keep the shared room checks; derive its additional mutable words."""
    import export_render_context as ERC
    colour = retained_colour(elf)
    original = ERC.verify

    def verify(blocks, ram):
        if ram[0x251C20:0x251C2C] != colour:
            raise ValueError('retained colour differs from original 001D4440')
        expected = []
        for address, data in blocks:
            data = bytearray(data)
            if address <= 0x251C20 and 0x251C2C <= address+len(data):
                offset = 0x251C20-address
                data[offset:offset+12] = colour
            expected.append((address, data))
        return original(expected, ram)

    ERC.verify = verify
    try:
        rows = _check_ctx_block(caps, elf)
    finally:
        ERC.verify = original
    for row in rows:
        row['retained_colour_original_writer'] = '001D4440'
        row['retained_colour_bytes_verified'] = 12
    return rows


def source_rules():
    """Refuse a changed source call set instead of assuming an empty rule."""
    sources = sorted((A13.DECOMP/'src/overlays/AREA03').glob('*.c'))
    if not sources:
        raise ValueError('AREA03 decomp C is unavailable')
    for path in sources:
        text = path.read_text()
        if re.search(r'(?m)^\s*(?:asm|__asm__)\b|^\s*\.(?:word|byte|short|long)\b', text):
            # The pre-existing init unit includes the census padding. Its
            # standalone oracle covers the init; do not interpret its body.
            if path.name == 'func_overlay_AREA03_00823500.c':
                continue
            raise ValueError('AREA03 source rules require readable C')
        for name in ('0019C6F0', '001BC150', '001BC240'):
            if re.search(r'\bfunc_'+name+r'\s*\(', text):
                raise ValueError('AREA03 gained a cell-flag or overlay-door caller')
    # Independently reject a direct call to these workers anywhere in the
    # pinned overlay, including the init unit. No instruction is displayed.
    overlay = A13.read_overlay(A13.current())
    for offset in range(0x40, len(overlay)-3, 4):
        word = struct.unpack_from('<I', overlay, offset)[0]
        if word >> 26 in (2, 3) and (word & 0x3FFFFFF) << 2 in (0x19C6F0, 0x1BC150, 0x1BC240):
            raise ValueError('AREA03 original has an unmodelled cell-flag or door call')


def flag_calls(ram):
    if tuple(ram[0x810700:0x810702]) != (3, 0):
        raise ValueError('AREA03 flag rules received another area')
    return ()


def install():
    P.area03(0)
    source_rules()
    target = A13.current()
    A13.OUT, A13.SCRATCH = OUT, SCRATCH
    target.out = OUT/'sub0'
    LV.PREVIOUS['area03'] = A13.DECOMP/'build/s87/route_a19e/a19e_06_panel24'
    LV.CREATURES['area03'] = 0x823930
    LV.FLAG_OWNERS['area03'] = None
    LV.flag_calls = flag_calls
    LV.E22.check_ctx_block = check_ctx_block
    TB.PLACEMENTS['area03'] = 0x8277F0
    TB.GROUPS['area03'] = ((0x826900, 15),)
    TB.SPAWN_SUB0['area03'] = (0x24B950, 4)
    TB.DOOR_ROW['area03'] = 0x24DFD0
    TB.SCRIPT_ENTRIES['area03'] = SCRIPTS
    TB.WRITERS['area03'] = ()
    TB.DOOR_BEHAVIOURS = (0x1BC350, 0x1BB860, 0x1BD9F0, 0x1BD560)
    TB.EXPLICIT_MODELS = {}
    LV.install(target)
    return C


install()
