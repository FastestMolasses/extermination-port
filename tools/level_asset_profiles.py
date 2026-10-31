"""Select one area export profile per process, preserving existing readers.

Profiles name capture coverage, not native support. The later reload profiles
retain their separate load maps and original comparisons.
"""
import importlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUBS = {'00': (0, 1), '01': (0,), '02': (0, 1), '03': (0,), '07': (0,), '08': (0, 2, 3),
        '04': (0,), '06': (0,), '13': (0,), '13x': (0,),
        '15': (0, 1), '19': (0,), '19x': (0,), '19s1': (1,), '22': (0,)}
_active = None


def configure(profile, sub):
    global _active
    if profile not in SUBS or sub not in SUBS[profile]:
        raise ValueError('unrecorded profile/subarea')
    if _active is not None and _active != profile:
        raise ValueError('different area profiles require separate processes')
    _active = profile
    if profile == '03':
        return area03(sub)
    if profile in ('07', '08'):
        import export_area08_common as A
        return A.install('area'+profile, sub)
    module = importlib.import_module('export_area'+profile+'_common')
    if profile == '01':
        return module
    if profile == '13':
        return module.configure('area13')
    if profile in ('13x', '19', '19x', '19s1'):
        return module.install()
    if profile == '15':
        return module.install(sub)
    return module.configure(sub)


def area03(sub):
    """AREA03's two retained snapshots; the same descriptor-driven reader.

    Only sub 0 is recorded. A later sub must have its own original evidence.
    """
    import export_area13_common as A
    overlay_sha256 = 'ea5cba26e5ea53bd0809177fb04d0ea549fa02c2bb01d86108a1ab1ce02ca361'
    target = A.Target('area03', 3, sub, 'AREA03.BIN', 0x5C00, 4, overlay_sha256,
        'chunk07', True, ROOT/'assets/levels/03/sub0', 'AREA03')
    A.TARGETS['area03'] = target
    base = A.DECOMP/'build/s87'
    # Resolve by recorded group/beat prefix; suffix spelling is metadata,
    # whereas the area bytes and loaded overlay below establish identity.
    paths = []
    for group, prefix in (('a19e', 'a19e_07_'), ('a03', 'a03_00_')):
        found = sorted(p for p in (base/f'route_{group}').iterdir()
                       if p.name.startswith(prefix) and (p/'eeMemory.bin').is_file())
        if len(found) != 1:
            raise ValueError('missing or ambiguous AREA03 snapshot')
        paths += found
    A.capture_paths = lambda: paths
    common = A.configure(target)
    A.read_overlay(target)
    for capture in common.captures():
        problems, _ = A.relocation_problems(capture)
        if problems:
            raise ValueError('AREA03 relocation entries differ from descriptor delivery')
    return common


def output(profile, sub):
    return ROOT/'assets/levels'/profile/f'sub{sub}'
