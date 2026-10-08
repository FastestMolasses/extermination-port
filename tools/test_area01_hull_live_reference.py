#!/usr/bin/env python3
"""AREA01's canonical chain adapter in the original hull locks.

The fixture reuses the existing hull-lock oracle and replaces only its
RAM chain reader with the production adapter. Captures stay test-only.
"""
import ctypes as C
import json
import math
import struct
from pathlib import Path

import reference_mode as mode
import test_coll_grid_hull_reference as H
from export_area01_common import captures, read_elf

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/level2-crashes/hull-reference'

CHAIN = r'''
#include "game/em_area01_hull_live.h"
#include "game/em_collision_world.h"
typedef struct {
    const uint8_t *ram;
    size_t size;
    uint32_t (*address_of)(void *,const EmActor *);
    void *owner;
    int calls;
    EmArea01HullView view;
} HullRam;
static int hull_refusal;
static int hull_record(void *ctx,const EmActor *body,uint8_t out[EM_ACTOR_RECORD_SIZE])
{
    HullRam *h=ctx;uint32_t a=h->address_of(h->owner,body);
    if(hull_refusal==1 || !a || a>h->size || EM_ACTOR_RECORD_SIZE>h->size-a)return -1;
    memcpy(out,h->ram+a,EM_ACTOR_RECORD_SIZE);return 0;
}
static const uint8_t *hull_resource(void *ctx,uint32_t a,uint32_t *size)
{
    HullRam *h=ctx;
    if(hull_refusal==2 || a>=h->size)return NULL;
    *size=(uint32_t)(h->size-a);
    if(hull_refusal==3)*size=11;
    return h->ram+a;
}
static const uint8_t *hull_slot(void *ctx,uint32_t a,uint32_t n)
{
    HullRam *h=ctx;
    return hull_refusal==4 || a>h->size || n>h->size-a ? NULL : h->ram+a;
}
static int hull_ram_chain(void *ctx,const EmActor *body,EmCollHullChain *out)
{
    HullRam *h=ctx;
    h->view.host=(EmArea01HullHost){h,hull_record,hull_resource,hull_slot};
    ++h->calls;return em_area01_hull_chain(&h->view,body,out);
}
void bridge_hull_refuse(int mode) { hull_refusal=mode; }
static int shared_chain(void *ctx,const EmActor *body,EmCollHullChain *out)
{ (void)ctx;(void)body;(void)out;return -1; }
int bridge_world_contract(const char *emcl_path,const char *cells_path)
{
    EmCollision grid={0};EmCollisionWorldOwners shared={0};
    shared.context=&grid;shared.chain=shared_chain;
    em_collision_world_unload();em_collision_world_bind_owners(&shared);
    if(em_collision_world_bind_area_hulls(hull_ram_chain,NULL)!=-1)return -1;
    if(em_collision_load(&grid,emcl_path)<0)return -2;
    for(unsigned pass=0;pass<2;++pass) {
        if(em_collision_world_load(&grid,emcl_path,cells_path,EM_COLLISION_WORLD_SDK_PATH)<0)return -3;
        const EmCollHullWorld *h=em_collision_world_segment()->hulls;
        if(h->chain!=shared_chain || h->context!=&grid)return -4;
        if(em_collision_world_bind_area_hulls(hull_ram_chain,&shared)<0)return -5;
        em_collision_world_bind_owners(&shared);
        if(h->chain!=hull_ram_chain || h->context!=&shared)return -6;
        if(pass==0) {
            if(em_collision_world_bind_area_hulls(NULL,NULL)<0 || h->chain!=shared_chain || h->context!=&grid)return -7;
            if(em_collision_world_bind_area_hulls(hull_ram_chain,&shared)<0)return -8;
        }
        em_collision_world_unload();
    }
    em_collision_world_bind_owners(NULL);em_collision_free(&grid);return 0;
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    H.OUT = OUT
    H.BRIDGE = H.BRIDGE.replace(H.CHAIN_C, CHAIN)
    H.SOURCES += [f'src/game/em_{name}.c' for name in
                  ('area01_hull_live', 'collision_world', 'coll_list_passes_walkers',
                   'coll_list_passes', 'coll_segment_walkers', 'coll_move_original',
                   'sdk_math_original', 'sdk_soft_float', 'interaction_scan')]
    H.ELF = read_elf()
    H.MV.ELF = H.ELF
    H.MV.CODE_GRAPH = H.MV.call_graph(H.ELF, (H.GRID, H.LOCK6440, H.LOCK6AD0,
                                             H.LOCK7280, H.RANKS, H.NODE_TEST))
    H.NATIVE = n = H.build_native()
    n.bridge_hull_refuse.argtypes = [C.c_int]
    n.bridge_world_contract.argtypes = [C.c_char_p, C.c_char_p]
    assert n.bridge_world_contract(str(ROOT/'assets/area01/area01.emcl').encode(),
                                   str(ROOT/'assets/area01/area01_cells.bin').encode()) == 0
    runs = hits = refusals = 0
    actors = set()
    records = captures()
    for capture in records:
        w = H.World.__new__(H.World)
        w.beat, w.spad = capture.name, bytes(capture.spad)
        H.MV.check_code(H.ELF, capture.ram, capture.name)
        w.ee = H.MV.FloatEE(H.ELF, bytearray(capture.ram), w.spad)
        w.ram = w.ee.mem
        w.node_base = H.u32(w.spad, 0x3208)
        entries = H.class2_entries(w.ram)
        cases = []
        for actor in entries:
            if not w.ram[actor]&1 or not H.u32(w.ram, actor+0x58):
                continue
            actors.add(actor)
            polygons = H.chain_polys(w.ram, actor)
            for vertices, normal, offset in polygons[:mode.pick(12, 3)]:
                if not vertices:
                    continue
                center = [sum(v[k] for v in vertices)/len(vertices) for k in range(3)]
                length = math.sqrt(sum(x*x for x in normal))
                if not length:
                    continue
                delta = [12*x/length for x in normal]
                start = [H.f32r(center[k]+delta[k]) for k in range(3)]
                end = [H.f32r(center[k]-delta[k]) for k in range(3)]
                cases.append((start, end, f'{actor:08X}+{offset:X}'))
        # Also query the player's captured aim-height segment through the
        # complete original list, including distant zero-count chains.
        x, y, z = struct.unpack_from('<3f', w.ram, H.PLAYER+0xB0)
        cases.append(([x,y,z], [x,y,z-180], 'player aim'))
        for start, end, label in cases:
            for fn in (H.LOCK6440, H.LOCK6AD0):
                spad = H.spad_with(w, start, end, {0x31D4: (0, 4)})
                result, _ = H.compare(w, label, fn, 0x70, spad)
                hits += result != 0
                runs += 1
        if not refusals and any(w.ram[a]&1 and H.u32(w.ram,a+0x58) for a in entries):
            # Refusals belong to the provider; the original lock cannot
            # proceed with another owner's chain or a fabricated matrix.
            spad = H.spad_with(w, [41,10,-565], [41,10,-745], {0x31D4: (0,4)})
            for deny in (1,2,3,4):
                b=w.bridge();state=H.state_of(spad)
                n.bridge_hull_refuse(deny)
                assert n.bridge_lock(b,0,C.byref(state),0x70)==-1, deny
                n.bridge_hull_refuse(0);n.bridge_free(b);refusals+=1
    assert runs and hits and len(actors)>4 and refusals==4
    result=dict(captures=len(records),queries=runs,hits=hits,actors=len(actors),
                provider_refusals=refusals,world_binding_contracts=8)
    (OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('AREA01 hull adapter original reference PASS',result)


if __name__ == '__main__':
    main()
