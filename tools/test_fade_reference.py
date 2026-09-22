#!/usr/bin/env python3
"""Compare native fade logic against the user's local decomp C.

The generated oracle contains locally supplied original-function C and
stays in ignored build/. Only hardware packet emission is removed; all
state updates and pre-update drawing predicates remain from that source.
This is optional evidence in addition to the asset-free `make test-fade`.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = ["001AED80", "001AEDB0", "001AEDE0", "001AEE10", "001AEE40",
             "001AEB60", "001AEBA0"]


def body(decomp: Path, address: str) -> str:
    path = decomp / "src" / f"func_{address}.c"
    source = path.read_text()
    match = re.search(rf"^void func_{address}\(", source, re.MULTILINE)
    if match is None:
        raise ValueError(f"Cannot locate original function in {path}")
    return source[match.start():]


PRELUDE = r'''
#include "game/em_fade.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static union { max_align_t align; unsigned char bytes[0xc8]; } original;
static union { max_align_t align; unsigned char bytes[8]; } screen;
#define D_0028A8E0 ((char *)original.bytes)
#define D_0028A9A0 ((short *)(original.bytes + 0xc0))
#define D_0028A9A2 ((unsigned char *)(original.bytes + 0xc2))
#define D_0028A9A3 ((signed char *)(original.bytes + 0xc3))
#define D_0028A9A4 ((short *)(original.bytes + 0xc4))
#define D_0028A9A6 ((short *)(original.bytes + 0xc6))
#define D_0028A8D0 ((short *)screen.bytes)
#define D_0028A8D2 ((short *)(screen.bytes + 2))
#define D_0028A8D4 ((int *)(screen.bytes + 4))
'''

DRIVER = r'''
static void load(const EmTransitionFade *f) {
    D_0028A9A0[0] = f->substate;
    D_0028A9A2[0] = f->colour;
    D_0028A9A3[0] = f->mode;
    D_0028A9A4[0] = f->level;
    D_0028A9A6[0] = f->step;
}
static int same(const EmTransitionFade *f) {
    return f->substate == D_0028A9A0[0] && f->colour == D_0028A9A2[0] &&
           f->mode == D_0028A9A3[0] && f->level == D_0028A9A4[0] &&
           f->step == D_0028A9A6[0];
}
static int screen_same(const EmScreenFade *f) {
    return f->state == D_0028A8D0[0] && f->step == D_0028A8D2[0] &&
           f->level == D_0028A8D4[0];
}
int main(void) {
    static const short levels[] = {-32768,-1,0,1,4,64,128,251,252,255,32767};
    static const short steps[] = {-32768,-255,-4,0,4,255,32767};
    static const unsigned char colours[] = {0,1,255};
    unsigned long checks = 0;
    for (int mode = -1; mode <= 7; ++mode)
    for (int sub = -1; sub <= 4; ++sub)
    for (unsigned l = 0; l < sizeof levels / sizeof *levels; ++l)
    for (unsigned s = 0; s < sizeof steps / sizeof *steps; ++s)
    for (unsigned c = 0; c < sizeof colours / sizeof *colours; ++c) {
        EmTransitionFade f = {sub,colours[c],mode,levels[l],steps[s]};
        load(&f);
        for (int tick = 0; tick < 3; ++tick) {
            int reference_draw = oracle_transition_tick();
            int native_draw = em_transition_fade_tick(&f);
            if (reference_draw != native_draw || !same(&f)) {
                fprintf(stderr, "transition mismatch: mode=%d sub=%d level=%d step=%d tick=%d\n",
                        mode,sub,levels[l],steps[s],tick);
                return 1;
            }
            ++checks;
        }
        for (int arm = 0; arm < 5; ++arm) {
            f = (EmTransitionFade){sub,colours[c],mode,levels[l],steps[s]};
            load(&f);
            switch (arm) {
            case 0: func_001AED80(colours[c]); em_transition_fade_clear(&f,colours[c]); break;
            case 1: func_001AEDB0(colours[c]); em_transition_fade_full(&f,colours[c]); break;
            case 2: func_001AEDE0(steps[s],colours[c]); em_transition_fade_out(&f,steps[s],colours[c]); break;
            case 3: func_001AEE10(steps[s],colours[c]); em_transition_fade_in(&f,steps[s],colours[c]); break;
            case 4: func_001AEE40(steps[s]); em_transition_fade_flash(&f,steps[s]); break;
            }
            if (!same(&f)) { fprintf(stderr,"transition arm mismatch %d\n",arm); return 1; }
            ++checks;
        }
    }
    for (int state = -1; state <= 4; ++state)
    for (unsigned l = 0; l < sizeof levels / sizeof *levels; ++l)
    for (unsigned s = 0; s < sizeof steps / sizeof *steps; ++s)
    for (int gate = 0; gate < 4; ++gate) {
        EmScreenFade f = {state,steps[s],levels[l]};
        D_0028A8D0[0] = f.state; D_0028A8D2[0] = f.step; D_0028A8D4[0] = f.level;
        unsigned char request = gate & 1 ? 2 : 1;
        unsigned char suppress = (gate >> 1) & 1;
        int reference_draw = oracle_screen_tick(request,suppress);
        int native_draw = em_screen_fade_tick(&f,request,suppress);
        if (reference_draw != native_draw || !screen_same(&f)) {
            fprintf(stderr,"screen mismatch state=%d level=%d step=%d\n",state,levels[l],steps[s]);
            return 1;
        }
        ++checks;
        func_001AEB60(steps[s]); em_screen_fade_out(&f,steps[s]);
        if (!screen_same(&f)) { fputs("screen out mismatch\n",stderr); return 1; }
        ++checks;
        func_001AEBA0(steps[s]); em_screen_fade_in(&f,steps[s]);
        if (!screen_same(&f)) { fputs("screen in mismatch\n",stderr); return 1; }
        ++checks;
    }
    printf("fade_reference: PASS (%lu original-C comparisons)\n", checks);
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decomp", type=Path, default=ROOT.parent / "Extermination")
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    code = PRELUDE + "\n".join(body(args.decomp, address) for address in FUNCTIONS)

    # Retain the complete switch but omit PS2 display-list stores, whose
    # 32-bit pointers cannot execute on a native 64-bit host. Fail loudly
    # if the local original function no longer has this known boundary.
    transition = body(args.decomp, "001AEE70")
    boundary = "    *(int *)((char *)(D_00810E80[0] * 0x60)"
    if transition.count(boundary) != 3:
        raise ValueError("Unexpected original transition packet tail")
    transition = transition[:transition.index(boundary)] + "    return st != 0;\n}\n"
    transition = transition.replace("void func_001AEE70(void)", "int oracle_transition_tick(void)", 1)
    code += transition

    screen = body(args.decomp, "001AEBE0")
    boundary = "    p = D_0028A7B0 +"
    if screen.count(boundary) != 1:
        raise ValueError("Unexpected original screen packet tail")
    screen = screen[:screen.index(boundary)] + (
        "    return st != 0 && (request != 2 || suppress == 0);\n}\n")
    screen = screen.replace("void func_001AEBE0(void)",
                            "int oracle_screen_tick(unsigned char request, unsigned char suppress)", 1)
    code += screen + DRIVER

    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    source = build / "fade_reference.c"
    binary = build / "fade_reference"
    source.write_text(code)
    subprocess.run([args.cc, "-O2", "-std=c11", "-Isrc", str(source),
                    "src/game/em_fade.c", "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
