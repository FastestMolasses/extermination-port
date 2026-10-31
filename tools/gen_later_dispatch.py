#!/usr/bin/env python3
"""Generate src/game/em_area01_runtime_later.inc: the fallback dispatch of the
later levels' boot translations.

The level ports (em_area04/06/22_port, em_level8_port .. em_level14_port,
em_level15_*) translate
boot functions from the instructions, each proven by its own original-
instruction oracle (tools/test_level*_reference.py), most of them unbound.
The shared composition reaches them only where a run needs them: an address
no earlier owner handles ends in the host's missing-worker path, which calls
em_area01_runtime_later before the fail-stop. One case per entry point, from
the prototypes of the modules' headers: integer parameters from a0.., float
parameters from f12.., `sp` the caller's stack pointer, a *result pointer
v0 or f0. Nested hooks (tools/gen_mid_route_bridge.py) run below the entry's
own frame, read here from its first instruction in the user's pinned ELF
(only the number lands in the output), and EIGHTH_NEST.

Overlay addresses are left out: they mean another function in each area.

Usage (port root; native arm64 macOS): python3 tools/gen_later_dispatch.py
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'src/game/em_area01_runtime_later.inc'
ELF = ROOT.parent / 'Extermination/config/SCUS_971.12'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
# header, hook-table prefix (em_mid_route_bridge.inc), hooks type, fault type
SOURCES = (('em_area04_port.h', 'm04', 'EmArea04PortHooks', 'EmArea04PortFault'),
           ('em_area22_port.h', 'm22', 'EmArea22PortHooks', 'EmArea22PortFault'),
           ('em_area06_port.h', 'm06', 'EmArea06PortHooks', 'EmArea06PortFault'),
           ('em_level8_port.h', 'm08', 'EmLevel8PortHooks', 'EmLevel8PortFault'),
           ('em_level9_port.h', 'm09', 'EmLevel9PortHooks', 'EmLevel9PortFault'),
           ('em_level10_port.h', 'm10', 'EmLevel10PortHooks', 'EmLevel10PortFault'),
           ('em_level11_port.h', 'm11', 'EmLevel11PortHooks', 'EmLevel11PortFault'),
           ('em_level12_port.h', 'm12', 'EmLevel12PortHooks', 'EmLevel12PortFault'),
           ('em_level13_port.h', 'm13', 'EmLevel13PortHooks', 'EmLevel13PortFault'),
           ('em_level14_port.h', 'm14', 'EmLevel14PortHooks', 'EmLevel14PortFault'),
           ('em_level15_creatures.h', 'm15', 'EmLevel15CreaturesHooks', 'EmLevel15CreaturesFault'),
           ('em_level15_fountain.h', 'm15f', 'EmLevel15FountainHooks', 'EmLevel15FountainFault'),
           ('em_level15_motion.h', 'm15m', 'EmLevel15MotionHooks', 'EmLevel15MotionFault'),
           ('em_level15_perception.h', 'm15p', 'EmLevel15PerceptionHooks', 'EmLevel15PerceptionFault'),
           ('em_level15_support.h', 'm15s', 'EmLevel15SupportHooks', 'EmLevel15SupportFault'))
PROTO = re.compile(r'^int (em_(?:level|area)\w+?_([0-9A-F]{8}))\(([^;]*?)\);', re.M | re.S)
OVERLAY = 0x823500
CASTS = {'uint32_t': '(uint32_t)', 'int32_t': '(int32_t)', 'uint64_t': ''}


def frame(elf, address):
    word = struct.unpack_from('<I', elf, address - 0x100000 + 0x300)[0]
    return 0x10000 - (word & 0xFFFF) if word >> 16 == 0x27BD else 0


def case(elf, name, address, params, prefix, hooks, fault):
    args, ints, floats, result = [], 0, 0, None
    for p in params:
        typ, var = p.rsplit(' ', 1)
        if var.startswith('*'):
            if var != '*result':
                return None
            result = typ
            args.append('&out_' + {'int32_t': 'i', 'uint32_t': 'u', 'float': 'f'}[typ])
        elif var == 'sp':
            args.append('c->sp')
        elif typ == 'float':
            args.append(f'em_ee_float(c->f[{floats}])')
            floats += 1
        elif typ in CASTS:
            args.append(f'{CASTS[typ]}c->a[{ints}]')
            ints += 1
        else:
            return None
    size = frame(elf, address)
    lines = [f'    case 0x{address:08X}u: {{',
             f'        {hooks} h;',
             f'        {fault} f = {{0, 0}};',
             f'        if (c->na < {ints}u || c->nf < {floats}u || c->sp < 0x{size:X}u + EIGHTH_NEST) goto refused;',
             f'        {prefix}_hooks(r, &h);',
             f'        r->sp = c->sp - 0x{size:X}u - EIGHTH_NEST;',
             f'        rc = {name}(&h, {", ".join(args + ["&f"]) });',
             '        where = f.address;',
             '        code = f.code;']
    if result == 'float':
        lines.append('        has = 2;')
    elif result:
        lines.append(f'        has = {1 if result == "int32_t" else 3};')
    lines += ['        break;', '    }']
    return '\n'.join(lines)


def main():
    elf = ELF.read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    seen, cases = set(), []
    for header, prefix, hooks, fault in SOURCES:
        text = (ROOT / 'src/game' / header).read_text()
        for name, addr, params in PROTO.findall(text):
            address = int(addr, 16)
            ps = [' '.join(p.split()) for p in params.split(',')]
            if address >= OVERLAY or address in seen:
                continue
            body = case(elf, name, address, ps[1:-1], prefix, hooks, fault)
            if body is None:
                print(f'left out: {name}({", ".join(ps[1:-1])})')
                continue
            seen.add(address)
            cases.append((address, body))
    cases.sort()
    out = ['/* Generated by tools/gen_later_dispatch.py: the later levels\' boot',
           ' * translations as the fallback of an unbound address (see the tool).',
           ' * Do not edit by hand. */',
           'static int dispatch_later(EmArea01Runtime *r, EmArea01Call *c, int *out)',
           '{',
           '    int32_t out_i = 0;',
           '    uint32_t out_u = 0;',
           '    float out_f = 0;',
           '    int rc = -1, code = 0, has = 0;',
           '    uint32_t where = 0;',
           '    const uint32_t saved = r->sp;',
           '    switch (c->function) {',
           *[body for _, body in cases],
           '    default:',
           '        return 0;',
           '    }',
           '    r->sp = saved;',
           '    if (rc < 0) {',
           '        fprintf(stderr, "em_area01: level port %08X fault code %d at %08X\\n", (unsigned)c->function, code,',
           '                (unsigned)where);',
           '        *out = fail(r, where ? where : c->function);',
           '        return 1;',
           '    }',
           '    if (has == 1) c->v0 = (uint64_t)(int64_t)out_i;',
           '    if (has == 3) c->v0 = (uint64_t)(int64_t)(int32_t)out_u;',
           '    if (has == 2) memcpy(&c->f0, &out_f, 4);',
           '    *out = 0;',
           '    return 1;',
           'refused:',
           '    *out = fail(r, c->function);',
           '    return 1;',
           '}',
           '']
    OUT.write_text('\n'.join(out))
    print(f'{OUT.relative_to(ROOT)}: {len(cases)} entries')
    return 0


if __name__ == '__main__':
    sys.exit(main())
