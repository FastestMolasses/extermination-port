#!/usr/bin/env python3
"""Selected shared-service entries used by levels 1..15.

Reuse the existing original-instruction comparators without changing their
assertions. Inputs are the user's local pinned ELF, disc, and reference captures.
Outputs stay in an ignored private build lane. A selected entry is not evidence
that every later-level caller or overlay is ready.

The native platform/scene/draw adapters are reported as boundaries, separately
from native translations compared with original instructions. No emulator is
launched and no disc-derived bytes are embedded in this tool.
"""
import argparse
import ast
import ctypes as C
import hashlib
import importlib
import inspect
import json
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/later-oracle-services"
LANES = ("gs", "copy", "status", "loader", "aim", "pickup", "messages", "draw",
         "segment", "column", "boundaries")
ADDRESSES = {
    "gs": ["001008C0"], "copy": ["00121870"],
    "status": ["001AFF90", "001FF590", "001FF830", "001FFCD0"],
    "loader": ["001FF080", "001FF0D0", "00200730", "00200830", "00200890", "00200970"],
    "aim": ["001B1510"], "pickup": ["001C47E0"],
    "messages": ["001FD0E0", "001FDDB0"], "draw": ["001D3900", "001D3CF0"],
    "segment": ["001A5C30"], "column": ["001A56A0"],
    "boundaries": ["00110B80", "00111018", "001D0660", "001D3990", "001D3D90"],
}


def module(name, lane):
    m = importlib.import_module("test_" + name + "_reference")
    m.OUT = OUT / lane
    m.OUT.mkdir(parents=True, exist_ok=True)
    return m


def pinned(m, sha_name="ELF_SHA"):
    data = (ROOT.parent / "Extermination/config/SCUS_971.12").read_bytes()
    assert hashlib.sha256(data).hexdigest() == getattr(m, sha_name)
    return data


def private_status_build(m):
    """Only redirect the pre-existing builder's hard-coded output directory."""
    tree = ast.parse(inspect.getsource(m.build))
    matches = [n for n in ast.walk(tree) if isinstance(n, ast.Assign)
               and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name)
               and n.targets[0].id == "out"]
    assert len(matches) == 1
    matches[0].value = ast.Name(id="_level_services_out", ctx=ast.Load())
    namespace = dict(m.__dict__, _level_services_out=m.OUT)
    exec(compile(ast.fix_missing_locations(tree), inspect.getfile(m), "exec"), namespace)
    return namespace["build"]()


def gs():
    m = module("gs_blocks", "gs")
    counts = {"clear": 0, "env2": 0}
    m.part_c(m.build_lib(), pinned(m), counts)
    assert counts["clear"] == (200 if full() else 20)
    return counts


def copy():
    m = module("static_world", "copy")
    m.ELF = m.read_elf()
    m.rcref.ELF = m.ELF
    m.NATIVE = m.build_native()
    groups = m.unit_cases(random.Random(0x5717), dict(bg=0, grid=0, point=0, matrix=0))
    selected = [(name, [c for c in cases if c["entry"] == 0x121870]) for name, cases in groups]
    selected = [(name, cases) for name, cases in selected if cases]
    results = [r for group in selected for r in m.run_group(group)]
    assert len(results) == 12
    return {"cases": len(results), "whole_memory_and_scratchpad": True}


def status():
    m = module("status_scene", "status")
    _, lib = private_status_build(m)
    elf = pinned(m)
    counts = {"pool": m.check_pool(elf, lib)}
    ram = m.ram(m.ROUTE / "03_panel_power/eeMemory.bin")
    counts["loader_dispatches"], counts["whole_loads"] = m.check_loader(
        elf, lib, ram[m.HEADER:m.HEADER + 0x800])
    import export_module_loader as x
    disc = x.Disc(m.DECOMP / "Extermination-rebuilt.iso")
    counts["area_dispatches"], counts["area_loads"] = m.check_area(
        elf, lib, disc.sectors(disc.index[0] + 0x0F, 1))
    counts["free_001AFF90_cases"] = 40 if full() else 12
    return counts


def loader():
    m = module("module_loader", "loader")
    elf, lib = m.read_elf_bytes(), m.build()
    counts = {"poll": m.check_poll(elf, lib), "dma": m.check_dma(elf, lib),
              "packet_and_restore": m.check_packet(elf, lib)}
    pack, disc_pack = m.OUT / "modules.emml", m.OUT / "modules_disc.emml"
    cmd = [sys.executable, str(ROOT / "tools/export_module_loader.py"), "--out"]
    subprocess.run(cmd + [str(pack), "--capture", str(m.ROUTE / "03_panel_power")],
                   check=True, capture_output=True)
    subprocess.run(cmd + [str(disc_pack)], check=True, capture_output=True)
    a, b = pack.read_bytes(), disc_pack.read_bytes()
    assert len(a) == len(b) and [i for i in range(len(a)) if a[i] != b[i]] == [0x24, 0x25, 0x26]
    counts["whole_load_dispatches"] = m.check_whole_loads(elf, lib, pack)
    counts["pinned_whole_loads"] = m.check_pinned_loads(elf, lib, pack, disc_pack)
    counts["new_game_host"] = m.check_new_game_loads(elf, lib, pack)
    counts["new_game_drive"] = m.check_new_game_loads(elf, lib, pack, measured=True)
    return counts


def aim():
    m = module("aim_fire_leaves", "aim")
    m.main()
    return {"full_existing_leaf_suite": True,
            "excluded_domain": "huge positive angles for which original subtraction does not terminate"}


def pickup():
    m = module("pickup_items", "pickup")
    elf, lib = pinned(m), m.build_library()
    cases = m.consume_cases(random.Random(0x1C47E0))
    assert all(0x810C64 + c[0] < m.REGION + m.REGION_SIZE for c in cases)
    for case in cases:
        m.consume_case(elf, lib, *case)
    return {"consume_cases": len(cases), "whole_inventory_bytes_and_return": True}


def messages():
    m = module("message_presenter_rest", "messages")
    m.cs.OUT = m.OUT / "cue"
    m.main()  # Retains all fourteen branch-site and fail-stop assertions.
    cs = m.cs
    lib, _ = cs.build_native()
    elf = m.read_elf()
    ram = (cs.REF / "panel/eeMemory.bin").read_bytes()
    advances = m.md.original_advances(elf, ram)
    cue = cs.section_d_cue(elf, lib, ram, None, advances, random.Random(0xD0E0))
    return {"presenter": json.loads((m.OUT / "result.json").read_text()), "cue_branches": cue}


def draw():
    m = module("owner_draw", "draw")
    m.ELF, m.LIB = pinned(m), m.build_library()
    base = m.refs.capture("s87/route")
    for beat in m.BEATS:
        path = base / beat
        if not (path / "eeMemory.bin").exists() or not (path / "scratchpad.bin").exists():
            continue
        ram = (path / "eeMemory.bin").read_bytes()
        assert ram[0x810700] == 11
        for lo, hi in ((0x1D37D0, 0x1D3DB0), (0x1D1F80, 0x1D1FF0)):
            assert ram[lo:hi] == m.ELF[lo - 0x100000 + 0x300:hi - 0x100000 + 0x300]
        m.CAP[beat] = (ram, (path / "scratchpad.bin").read_bytes())
    assert m.CAP, "missing local owner-draw captures"
    m.load_bank()
    items = [(n, a, near) for beat in m.CAP for n, a in m.indicator_owners(beat) for near in (0, 1)]
    if not full():
        items = m.select(items, 32, 0xCABA0, axes=(lambda i: i[0], lambda i: i[2]))
    results = m.parallel_map(m.case_indicator, items)
    assert any(r["unit"] for r in results)
    assert any(r["kind"] == "clip" for r in results)
    return {"indicator_draws": len(results),
            "kinds": {k: sum(r["kind"] == k for r in results) for k in ("plain", "clip", "culled")},
            "native_entries": ["em_owner_draw_001D3900", "em_owner_draw_001D3CF0"]}


def segment():
    m = module("coll_segment_walkers", "segment")
    m.cp.OUT = m.OUT
    emcl, verified, _ = m.cp.export_emcl()
    m.G.update(elf=m.read_elf(), native=m.build_native(), emcl=emcl)
    n = 1500 if full() else 90
    results = m.reference_mode.parallel_map(m.run_units, [
        ("06_hill_slide", n, 41), ("08_truck_crossing", n, 42)])
    outcomes = {}
    for r in results:
        for k, v in r["round"].items():
            outcomes[k] = outcomes.get(k, 0) + v
    assert {k[2] for k in outcomes if k[1] == 1} == {0x2000, 0x4000, 0x8000}
    assert {k[1] for k in outcomes} == {0, 1}
    return {"round_cases": 2 * n, "outcomes": {str(k): v for k, v in outcomes.items()},
            "export_verified": verified}


def native_body(path, name):
    """Compile the current native body, never a handwritten replacement of it."""
    source = (ROOT / path).read_text()
    # All selected functions have ordinary single-line C signatures.
    match = re.search(r"^(?:static )?(?:int|float|uint16_t)\s+" + re.escape(name)
                      + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, (path, name)
    start = match.start()
    tokens = re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
                         source[match.end() - 1:], re.S)
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if not depth:
                end = match.end() - 1 + token.end()
                body = source[start:end]
                return body, {"path": path, "function": name,
                              "body_sha256": hashlib.sha256(body.encode()).hexdigest()}
    raise AssertionError(("unterminated native function", path, name))


def compile_fixture(lane, name, source):
    path = OUT / lane
    path.mkdir(parents=True, exist_ok=True)
    c_path, lib_path = path / (name + ".c"), path / (name + ".dylib")
    c_path.write_text(source)
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-ffp-contract=off", "-shared", "-fPIC", "-Isrc", str(c_path),
                    "-o", str(lib_path)], cwd=ROOT, check=True)
    return C.CDLL(str(lib_path))


def column():
    """Direct 001A56A0 versus the real static column_round, including stale output."""
    from test_actor_collision_reference import EE, read_elf, bits
    source, provenance = [], []
    for name in ("ee_add", "ee_sub", "ee_mul", "ee_square_sum", "rd_f", "rd_u16", "column_round"):
        body, receipt = native_body("src/game/em_actor_collision.c", name)
        source.append(body)
        provenance.append(receipt)
    lib = compile_fixture("column", "column", """
#include <stdint.h>
#include <string.h>
#include "game/em_ee_float.h"
""" + "\n".join(source) + """
int test_column(const uint8_t *p, uint32_t x, uint32_t z, uint32_t *out) {
    return column_round(p, em_ee_float(x), em_ee_float(z), (float *)out, (float *)(out+4));
}
""")
    lib.test_column.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_void_p]
    rng = random.Random(0x1A56A0)
    cases = []
    for kind in (0x4000, 0x4800, 0x8000, 0x8800):
        for radius in (0.0, -0.0, 1.0, -2.0, 1e-39, 3e18):
            for x, z in ((0, 0), (radius, 0), (0, radius), (radius, radius)):
                centre = [0.0, 2.0, 0.0, radius, -3.0]
                bx = bits(x)
                for dx in (0, -1, 1):
                    cases.append((kind, [bits(v) for v in centre], (bx + dx) & 0xFFFFFFFF, bits(z)))
    special = (0, 0x80000000, 1, 0x80000001, 0x7FFFFF, 0x807FFFFF,
               0x7F800001, 0xFF800001, 0x7F7FFFFF, 0xFF7FFFFF)
    for i in range(4000 if full() else 160):
        data = [bits(rng.uniform(-100, 100)) for _ in range(5)]
        x, z = bits(rng.uniform(-150, 150)), bits(rng.uniform(-150, 150))
        if i % 5 == 0:
            data[rng.randrange(5)] = rng.choice(special)
        if i % 7 == 0:
            x = rng.choice(special)
        cases.append((rng.choice((0x4000, 0x4800, 0x8000, 0x8800)), data, x, z))
    ee = EE(read_elf())
    q, out, prim = 0x610000, 0x620000, 0x630000
    hits = {0: 0, 1: 0}
    legal = set(range(out, out + 16)) | set(range(0x7000319C, 0x700031A0)) | set(range(0x700031AC, 0x700031B0))
    for index, (kind, data, x, z) in enumerate(cases):
        image = struct.pack("<HH5I", kind, 0, *data)
        for offset, byte in enumerate(image):
            ee.save(prim + offset, byte, 1)
        ee.save(q, x)
        # The caller 0019BC40 packs horizontal coordinates into q[0:2].
        ee.save(q + 4, z)
        stale = [rng.randrange(1 << 32) for _ in range(6)]
        for i, word in enumerate(stale):
            ee.save(out + 4 * i if i < 4 else (0x7000319C, 0x700031AC)[i - 4], word)
        writes = set()
        save = ee.save
        def tracked(address, value, size=4):
            writes.update(range(address, address + size))
            save(address, value, size)
        ee.save = tracked
        ee.call(0x1A56A0, (q, out, prim))
        ee.save = save
        assert writes <= legal, ("column unmodelled store", sorted(writes - legal))
        native = (C.c_uint32 * 6)(*stale)
        got = lib.test_column(image, x, z, native)
        want = [ee.load(out + 4 * i) for i in range(4)] + [ee.load(0x7000319C), ee.load(0x700031AC)]
        assert got == ee.r[2] and list(native) == want, (
            "column", index, hex(kind), [hex(v) for v in data], hex(x), hex(z),
            got, ee.r[2], [hex(v) for v in native], [hex(v) for v in want])
        hits[got] += 1
    assert all(hits.values())
    return {"cases": len(cases), "outcomes": hits, "native": provenance,
            "compared": "return and all six output words; no original worker hooks",
            "decomp_boundary": "001A56A0 has no readable C companion; original local ELF executed without displaying assembly"}


def boundaries():
    """Native adapter bodies with explicit mocks at their platform/worker boundary."""
    from test_actor_collision_reference import EE, read_elf
    elf = read_elf()
    bodies, provenance = {}, []
    entries = (
        ("pad", "src/game/em_frame.c", "pad_00110B80"),
        ("act", "src/game/em_pad_actuator.c", "actuator_00111018"),
        ("scene", "src/game/em_scene_bindings.c", "state0_001D0660"),
        ("draw", "src/game/em_owner_draw_live.c", "w_001D3990"),
        ("clip", "src/game/em_owner_draw_live.c", "w_001D3D90"))
    for label, path, name in entries:
        bodies[label], receipt = native_body(path, name)
        provenance.append(receipt)
    padlib = compile_fixture("boundaries", "pad", """
#include <stdint.h>
#include <stdio.h>
static float big, small; static int duration, calls;
static int report(const char *s) { (void)s; return -1; }
static void em_gamepad_rumble(float b,float s,int d) { big=b;small=s;duration=d;calls++; }
""" + bodies["pad"] + "\n" + bodies["act"] + """
int test_pad(int port,int slot,int32_t *ret) { return pad_00110B80(0,port,slot,ret); }
int test_act(int port,int slot,const uint8_t *act,float *result) {
    calls=0; int rc=actuator_00111018(0,port,slot,act);
    result[0]=big;result[1]=small;result[2]=(float)duration;result[3]=(float)calls;return rc;
}
""")
    padlib.test_pad.argtypes = [C.c_int, C.c_int, C.POINTER(C.c_int32)]
    padlib.test_act.argtypes = [C.c_int, C.c_int, C.c_void_p, C.POINTER(C.c_float)]
    e = EE(elf)
    buffer = 0x610000
    e.hooks[0x110AB8] = lambda o: o.r.__setitem__(2, buffer)
    state_map = {}
    for status in range(256):
        for mode in (0, 1, 2, 0xFF):
            e.save(buffer + 0x70, status, 1)
            e.save(buffer + 0x71, mode, 1)
            e.call(0x110B80, (0, 0))
            assert e.r[2] == (5 if status == 6 and mode == 2 else status)
            state_map[(status, mode)] = e.r[2]
    ret = C.c_int32(-99)
    assert padlib.test_pad(0, 0, C.byref(ret)) == 0 and ret.value == state_map[(6, 0)]
    for port, slot in ((1, 0), (0, 1), (-1, 0), (0, -1)):
        ret.value = -99
        assert padlib.test_pad(port, slot, C.byref(ret)) == -1 and ret.value == -99
    rpc_calls = []
    def rpc(o):
        rpc_calls.append(tuple(o.r[4:12]))
        o.r[2] = 0
    e.hooks = {0x10E8A8: rpc}
    actuator_cases = 0
    for big in (0, 1, 2, 0xFF):
        for small in range(256):
            act = bytes((big, small, 0x12, 0x34, 0x56, 0x78))
            for i, byte in enumerate(act):
                e.save(buffer + i, byte, 1)
            e.save(0x279A94, 0)
            rpc_calls.clear()
            e.call(0x111018, (0, 0, buffer))
            assert len(rpc_calls) == 1
            assert e.read(0x279A8C, 6) == act and e.load(0x279A80) == 7
            assert e.load(0x279A84) == e.load(0x279A88) == 0
            result = (C.c_float * 4)()
            assert padlib.test_act(0, 0, act, result) == 0
            assert list(result) == [float(bool(big)), C.c_float(small / 255).value, -1.0, 1.0]
            actuator_cases += 1
    for port, slot in ((1, 0), (0, 1), (-1, 0), (0, -1)):
        result = (C.c_float * 4)()
        assert padlib.test_act(port, slot, bytes(6), result) == -1 and result[3] == 0

    # Production types are retained for model lookup and channel-3 delegation.
    drawlib = compile_fixture("boundaries", "draw", """
#include <stdint.h>
#include <string.h>
#include "game/em_owner_draw_original.h"
static struct { const EmWorldModels *bank; EmOwnerDraw draw; } L;
static EmWorldModel model;
static int found, called, which, channel, response;
static uint32_t address, w04, fault;
static int report(uint32_t a,const char *s) { (void)s;fault=a;return -1; }
const EmWorldModel *em_world_models_of(const EmWorldModels *b,const EmOwnerModel *m) {
    return b && m==&model.model && found ? &model : 0;
}
static int ch3_1F80(void *c,int32_t a,int32_t b,int32_t d) { (void)c;(void)a;(void)b;(void)d;return 0; }
int em_owner_draw_001D3900(EmOwnerDraw *d,int32_t ch,uint32_t a,uint32_t w,
    int (*f)(void *,int32_t,int32_t,int32_t),void *ctx) {
    if(d!=&L.draw||f!=ch3_1F80||ctx)return -2;
    called++;which=0;channel=ch;address=a;w04=w;return response;
}
int em_owner_draw_001D3CF0(EmOwnerDraw *d,int32_t ch,uint32_t a,uint32_t w,
    int (*f)(void *,int32_t,int32_t,int32_t),void *ctx) {
    int r=em_owner_draw_001D3900(d,ch,a,w,f,ctx);which=1;return r;
}
""" + bodies["draw"] + "\n" + bodies["clip"] + """
int test_draw(int clip,int have_bank,int lookup,int ret,uint32_t a,uint32_t w,uint32_t *result) {
    L.bank=have_bank?(const EmWorldModels *)&model:0;found=lookup;response=ret;called=0;fault=0;
    model.address=a;model.w04=w;
    int rc=clip?w_001D3D90(0,&model.model):w_001D3990(0,&model.model);
    result[0]=(uint32_t)called;result[1]=(uint32_t)which;result[2]=(uint32_t)channel;
    result[3]=address;result[4]=w04;result[5]=fault;return rc;
}
""")
    drawlib.test_draw.argtypes = [C.c_int] * 4 + [C.c_uint32] * 2 + [C.POINTER(C.c_uint32)]
    draw_cases = 0
    for clip in (0, 1):
        for a in (0, 0x1800000, 0xFFFFFFFF):
            calls = []
            def draw_hook(o):
                calls.append((o.r[4] & 0xFFFFFFFF, o.r[5] & 0xFFFFFFFF))
            e.hooks = {(0x1D3900, 0x1D3CF0)[clip]: draw_hook}
            e.call((0x1D3990, 0x1D3D90)[clip], (a,))
            assert calls == [(3, a)]
            for w in (0, 0x82, 0xFFFFFFFF):
                for response in (0, -1):
                    result = (C.c_uint32 * 6)()
                    assert drawlib.test_draw(clip, 1, 1, response, a, w, result) == response
                    assert list(result) == [1, clip, *calls[0], w, 0]
                    draw_cases += 1
        for bank, lookup in ((0, 0), (0, 1), (1, 0)):
            result = (C.c_uint32 * 6)()
            assert drawlib.test_draw(clip, bank, lookup, 0, 0, 0, result) == -1
            assert result[0] == 0 and result[5] == (0x1D3990, 0x1D3D90)[clip]

    # The whole production scene module owns other globals; isolate the actual
    # callback body at its documented worker seam, not a replacement algorithm.
    scenelib = compile_fixture("boundaries", "scene", """
#include <stdint.h>
typedef struct { int unused; } State0Build;
static struct { uint32_t fault; } s_area01_state;
static struct { uint8_t d810700,d810701; } s_state;
static int area01_state_memory;
enum { EM_SCENE_FAULT_NULL_WORKER=1, EM_SCENE_FAULT_WORKER_FAILED=2 };
static int world, arrival, failed, count, log_[8];
static uint32_t fault_address; static int fault_code;
static int world_scene(void) { return world; }
static int arrival_scene(void) { return arrival; }
static int em_area11_bindings_effects_attach(void) { log_[count++]=1;return failed==1?-1:0; }
static uint32_t em_effects_live_fault(void) { return 0; }
static void *em_module_loader_live(void) { return &s_state; }
static int em_area01_state_bind_area(void *s,int mem,void *l,uint8_t a) {
    if(s!=&s_area01_state||mem!=area01_state_memory||l!=&s_state||a!=s_state.d810700)return -9;
    log_[count++]=2;return failed==2?-1:0;
}
static int em_area01_state_001E7780(void *s,uint8_t a,uint8_t b) {
    if(s!=&s_area01_state||a!=s_state.d810700||b!=s_state.d810701)return -9;
    log_[count++]=3;return failed==3?-1:0;
}
static void em_area01_state_detach(void *s) { if(s==&s_area01_state)log_[count++]=4; }
static int state0_fault(State0Build *b,uint32_t address,int code) {
    (void)b;fault_address=address;fault_code=code;return -1;
}
""" + bodies["scene"] + """
int test_scene(int w,int a,int fail,uint32_t pre_fault,uint32_t *result) {
    State0Build b={0};world=w;arrival=a;failed=fail;count=0;fault_address=0;fault_code=0;
    s_area01_state.fault=pre_fault;s_state.d810700=14;s_state.d810701=2;
    int rc=state0_001D0660(&b);
    result[0]=(uint32_t)count;for(int i=0;i<count;i++)result[i+1]=(uint32_t)log_[i];
    result[6]=fault_address;result[7]=(uint32_t)fault_code;return rc;
}
""")
    scenelib.test_scene.argtypes = [C.c_int] * 3 + [C.c_uint32, C.POINTER(C.c_uint32)]
    original_calls = []
    e.hooks = {a: (lambda o, a=a: original_calls.append(a)) for a in (0x1F0310, 0x1E7780)}
    e.call(0x1D0660)
    assert original_calls == [0x1F0310, 0x1E7780]
    scene_cases = 0
    for world in (0, 1):
        for arrival in (0, 1):
            for fail in (0, 1, 2, 3):
                for prior in (0, 0x812345):
                    expected = [1] if world else []
                    error = world and fail == 1
                    fault = 0x1F0310 if error else 0
                    code = 1 if error else 0
                    if not error:
                        if arrival:
                            expected += [2]
                            if fail == 2:
                                error = True
                            else:
                                expected += [3]
                                error = fail == 3
                            if error:
                                fault, code = prior or 0x1E7780, 2
                        else:
                            expected += [4]
                    result = (C.c_uint32 * 8)()
                    assert scenelib.test_scene(world, arrival, fail, prior, result) == (-1 if error else 0)
                    assert result[0] == len(expected) and list(result)[1:1 + len(expected)] == expected
                    assert list(result)[6:] == [fault, code]
                    scene_cases += 1
    return {
        "native": provenance,
        "00110B80": {"original_status_cases": len(state_map), "native_cases": 5,
                    "limitation": "native reports only connected stable port 0/slot 0; other original libpad states remain a hardware boundary"},
        "00111018": {"original_payload_and_native_rumble_cases": actuator_cases, "native_refusals": 4,
                    "limitation": "native motor mapping only; original RPC success/error lifecycle is not translated"},
        "001D3990_001D3D90": {"delegations": draw_cases, "native_refusals": 6,
                            "limitation": "actual native wrappers and original channel-3 arguments checked; model resolution and host channel persistence are separate boundaries"},
        "001D0660": {"native_cases": scene_cases, "original_call_order": [hex(a) for a in original_calls],
                    "limitation": "actual scene adapter branch/fault contract checked with worker mocks; original workers and all later scene predicates are not proved here"},
    }


def full():
    return os.environ.get("EM_TEST_FULL", "") not in ("", "0")


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lane", choices=LANES)
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    assert not (ROOT / "build").is_symlink(), "build must be owned by this worktree"
    OUT = args.out.resolve()
    assert OUT.is_relative_to((ROOT / "build").resolve()), "output must stay under this worktree build"
    OUT.mkdir(parents=True, exist_ok=True)
    for lane in LANES:
        assert (OUT / lane).resolve().is_relative_to(OUT), "lane symlink leaves private output"
        for suffix in (".json", ".log"):
            assert not (OUT / (lane + suffix)).is_symlink(), "report/log must not be a symlink"
    jobs = int(os.environ.get("EM_TEST_JOBS", "2"))
    assert 1 <= jobs <= 2, "this suite allows at most two workers"
    os.environ["EM_TEST_JOBS"] = str(jobs)
    if not args.lane:
        report = {}
        for lane in LANES:
            path = OUT / (lane + ".log")
            with path.open("w") as log:
                p = subprocess.run([sys.executable, __file__, "--lane", lane, "--out", str(OUT)],
                                   stdout=log, stderr=subprocess.STDOUT)
            report[lane] = {"exit": p.returncode, "log": str(path)}
            print(lane, "PASS" if p.returncode == 0 else "FAIL", flush=True)
        (OUT / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
        return int(any(r["exit"] for r in report.values()))
    started = time.monotonic()
    report_path = OUT / (args.lane + ".json")
    report_path.write_text(json.dumps(dict(status="RUNNING", lane=args.lane)) + "\n")
    result = globals()[args.lane]()
    report = dict(status="PASS", lane=args.lane, mode="full" if full() else "quick",
                  addresses=ADDRESSES[args.lane], seconds=round(time.monotonic() - started, 3),
                  evidence="native boundary" if args.lane == "boundaries" else "original instruction",
                  checks=result)
    report_path.write_text(json.dumps(report, indent=2, default=str) + "\n")
    print(json.dumps(report, default=str), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
