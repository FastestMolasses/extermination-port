"""Which recordings of the original the port's tests compare with.

One switch for every test and checker: EM_REFERENCE.

  fork (default)  the PCSX2 agent-debug fork's re-recordings
                  (../Extermination/build/fork_refs/ and the fork states in
                  ../Extermination/build/startup-reference/fork-states/;
                  decomp docs/PCSX2_FORK.md "Retiring the 2.6.3 app")
  v263            the PCSX2 2.6.3 recordings (../Extermination/build/ and the
                  user's save-state slots), kept on disk and usable explicitly

The fork sets keep the v2.6.3 layout, so capture('s87/route') is
build/fork_refs/s87/route on the fork and build/s87/route on v2.6.3. Two sets
moved: the fb2 pixel points (s87/c7cap/fb2 -> fork_refs/pixels/fb2) and the
GS conformance captures (b16 -> fork_refs/pixels/b16).

A set with no fork recording stays on its v2.6.3 data in both modes through
v263(rel, why): the call names the reason (docs/LEVEL_SMOKE.md
"References: fork"). A test never falls back from a missing fork file to
the v2.6.3 one silently.

The fork's main-loop counter is FORK_COUNTER_OFFSET lower than v2.6.3's from
first control through route 14 and its side groups; relative counters are
equal up to a load that runs longer on the fork.
"""
import os
from pathlib import Path

DECOMP = Path(__file__).resolve().parents[2] / 'Extermination'
BUILD = DECOMP / 'build'
FORK = BUILD / 'fork_refs'
FORK_STATES = BUILD / 'startup-reference' / 'fork-states'
SSTATES = BUILD / 'startup-reference' / 'portable-data' / 'sstates'

MODE = os.environ.get('EM_REFERENCE', 'fork')
if MODE not in ('fork', 'v263'):
    raise SystemExit(f'EM_REFERENCE={MODE!r}: expected fork or v263')
FORK_MODE = MODE == 'fork'

# v2.6.3 counter minus the fork's, first control .. route 14 (decomp
# docs/PCSX2_FORK.md checklist item 4).
FORK_COUNTER_OFFSET = 784

# Sets whose fork copy lives at another path than the v2.6.3 one.
_MOVED = {
    's87/c7cap/fb2': 'pixels/fb2',
    'b16': 'pixels/b16',
}

# The user slots the port's tests read, and the fork state that replaces
# each (decomp PCSX2_FORK.md checklist item 1; loop-top states).
FORK_SLOT_STATES = {
    1: 'slot01_title',
    2: 'phase/slot02_opening',
    4: 'phase/slot04_first_control',
    14: 'phase/slot14_status_hub',
}


def capture(rel):
    """The capture set (or file) rel, a path relative to the decomp's build/
    in the v2.6.3 layout, in the selected reference."""
    rel = str(rel).strip('/')
    if not FORK_MODE:
        return BUILD / rel
    for old, new in _MOVED.items():
        if rel == old or rel.startswith(old + '/'):
            return FORK / (new + rel[len(old):])
    return FORK / rel


def v263(rel, why):
    """The v2.6.3 capture rel in both modes: no fork recording of it exists
    (why says which, for the reader of the test)."""
    assert why, 'name why this set stays on v2.6.3'
    return BUILD / str(rel).strip('/')


def slot_dir(n):
    """The fork state folder for user slot n (state.p2s, eeMemory.bin,
    scratchpad.bin); fork mode only."""
    return FORK_STATES / FORK_SLOT_STATES[n]


def slot_state(n):
    """The save state standing for user slot n: the fork state's state.p2s,
    or the v2.6.3 slot file."""
    if FORK_MODE:
        return slot_dir(n) / 'state.p2s'
    return SSTATES / f'SCUS-97112 (0AE679AF).{n:02d}.p2s'


def label():
    """One word for banners and caches."""
    return MODE
