#!/usr/bin/env python3
"""Generate the hook block of src/game/em_level16_boot.h and the wrappers of
src/game/em_level16_boot_internal.h from HOOKS below (the same shape the level
modules' generated blocks have; tools/test_level16_boot_reference.py reads
HOOKS for the oracle's register maps).

Usage (port root): python3 tools/gen_level16_boot.py
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# name: (arguments, result): u uint32_t, i int32_t, f float, q uint64_t
HOOKS = {
    0x001026A0: ('uuu', None), 0x001026D0: ('uuu', None), 0x00102738: ('uu', 'f'), 0x00102760: ('uu', None),
    0x001028B8: ('uuu', None), 0x001028D0: ('uuu', None), 0x00102948: ('uu', None), 0x001029C0: ('u', None),
    0x00102C58: ('uuu', None), 0x00103230: ('uuf', None), 0x0011E420: ('f', 'f'), 0x001B1380: ('uuf', 'i'),
    0x001B15D0: ('uu', 'f'), 0x001B2B10: ('uuu', None), 0x001B39F0: ('uuu', 'i'), 0x0021BB00: ('u', 'i'),
    0x0021BC40: ('u', 'i'), 0x0017B490: ('uiii', 'i'), 0x001749A0: ('uiif', None), 0x001B1470: ('f', 'f'), 0x0021BE40: ('uu', 'i'), 0x0019AA80: ('uui', 'i'),
    0x0019A570: ('uuii', 'i'), 0x001EFD90: ('iuu', None), 0x001E2BA0: ('uuuf', None), 0x001F4F40: ('i', 'u'),
    0x001AFA90: ('i', 'u'), 0x00102958: ('uu', None), 0x001B0FD0: ('u', 'i'), 0x001C6380: ('u', None),
    0x001BA1A0: ('uu', None), 0x001BA1F0: ('u', 'i'), 0x001C47A0: ('ii', None), 0x001C4760: ('ii', None),
    0x001B17A0: ('u', 'i'), 0x001AFC10: ('u', None), 0x001B1020: ('uiii', 'i'), 0x001F1110: ('ui', None),
    0x001F1180: ('u', None), 0x001B1190: ('i', None),
    0x001BA1C0: ('ui', 'i'), 0x001B10B0: ('uii', 'i'), 0x001C63E0: ('ui', None), 0x001CA6F0: ('ui', None),
    0x001BA8E0: ('ui', None), 0x001C68C0: ('u', None), 0x001BA540: ('u', None), 0x001B1EA0: ('iuui', 'i'),
    0x001FB9F0: ('iiii', 'i'), 0x001C94B0: ('uuuu', None), 0x001281C0: ('f', 'i'), 0x001C67E0: ('uiff', None), 0x001FAE70: ('i', None), 0x001BA580: ('ui', None), 0x001C64F0: ('uf', 'i'),
}
TYPES = {'u': 'uint32_t', 'i': 'int32_t', 'f': 'float', 'q': 'uint64_t'}


def params(args, result):
    out = [f'{TYPES[t]} a{k}' for k, t in enumerate(args)]
    if result:
        out.append(f'{TYPES[result]} *result')
    return out


def hook_line(address, args, result):
    return f'    int (*w_{address:08X})(void *ctx{"".join(", " + p for p in params(args, result))});'


def wrapper(address, args, result):
    # float arguments and results travel as their bits in the module
    p = [f'{"uint32_t" if t == "f" else TYPES[t]} a{k}' for k, t in enumerate(args)]
    a = ['o->h->ctx'] + [f'l16b_float(a{k})' if t == 'f' else f'a{k}' for k, t in enumerate(args)]
    name = f'0x{address:08X}u'
    body = [f'static inline int l16b_c_{address:08X}(L16B *o{"".join(", " + x for x in p)}'
            + {None: ')', 'f': ', uint32_t *out)', 'i': ', int32_t *out)', 'u': ', uint32_t *out)'}[result],
            '{', '    if (l16b_failed(o)) return -1;',
            f'    if (!o->h->w_{address:08X}) return l16b_latch(o, {name}, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);']
    if result == 'f':
        body.append('    float r = 0;')
        a.append('&r')
    elif result:
        a.append('out')
    body.append(f'    if (o->h->w_{address:08X}({", ".join(a)}) < 0) '
                f'return l16b_latch(o, {name}, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);')
    if result == 'f':
        body.append('    *out = l16b_bits(r);')
    body += ['    return 0;', '}', '']
    return '\n'.join(body)


def replace(path, begin, end, text):
    s = path.read_text()
    new = re.sub(re.escape(begin) + r'.*?' + re.escape(end), lambda _: begin + '\n' + text + end, s, flags=re.S)
    path.write_text(new)


def main():
    rows = sorted(HOOKS.items())
    replace(ROOT / 'src/game/em_level16_boot.h', '/* BEGIN GENERATED HOOKS */', '/* END GENERATED HOOKS */',
            ''.join(hook_line(a, *v) + '\n' for a, v in rows))
    replace(ROOT / 'src/game/em_level16_boot_internal.h', '/* BEGIN GENERATED WRAPPERS */',
            '/* END GENERATED WRAPPERS */', '\n'.join(wrapper(a, *v) for a, v in rows))


if __name__ == '__main__':
    main()
