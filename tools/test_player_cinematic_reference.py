#!/usr/bin/env python3
"""The player's special bank (001B9A00 sub 1), 00183090's initialization and clock
through 001C64F0, and 00182DF0's nonzero-+0x2F3 release: native player_pose_commit_tick /
player_pose_release against the original routines (census L22)."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from test_item_sdk_math_reference import Original
from test_interaction_scan_reference import ELF_SHA
from test_point_light_reference import bits, number

ROOT = Path(__file__).resolve().parents[1]
PLAYER, RECORD, BANK, DEFAULT, NODE = 0x8102B0, 0x1200000, 0x1400000, 0x1500000, 0x1600000
# The player's one pose owner and the translations it reaches (Makefile
# PLAYER_RECORD_POSE_SRC).
RECORD_POSE = ('em_player_record_pose', 'em_pose_host_workers', 'em_player_stage_workers',
               'em_player_floor', 'em_player_reaction', 'em_player_fall',
               'em_owner_services_original', 'em_stream_lanes_original')

BRIDGE = r'''
#include "game/em_player_pose_host.c"
EmGameState g;
static EmTransitionFade fade;
static unsigned placements;
static EmPlayerLiveActor actor;
static uint8_t d8106F3, d8106F1, d810707;
static EmPlayerStageScene stage_scene = { .d8106F1 = &d8106F1 };
static EmPlayerStageGlobals stage_globals = { .d810707 = &d810707 };
static uint8_t *special;
static unsigned face_ticks;
const EmTransitionFade *em_frame_transition(void) { return &fade; }
void em_frame_request_quit(void) { abort(); }
PLACEMENT_FUNCTION
int setup(const uint8_t *bank, uint32_t size) {
    g.model.bone_count=22; g.status.health=100;
    g.pos[0]=321;g.pos[1]=290;g.pos[2]=201;g.yaw=.7f;
    special=malloc(size);
    if(!special)return 0;
    memcpy(special,bank,size);
    return player_pose_load(PLAYER_CLIP_BANK_PATH, PLAYER_CLIP_ROW0_PATH) &&
        player_pose_attach(&actor, &d8106F3, &stage_scene, &stage_globals) && player_pose_opening_release() &&
        player_pose_acquire()==1 && player_pose_map_region(BANK_ADDRESS, size, special);
}
void snapshot(unsigned *out) {
    out[0]=actor.bytes[0x2F3];out[1]=em_live_u16(&actor,0x1F2);
    out[2]=em_live_u32(&actor,0x1F4);out[3]=em_live_u32(&actor,0x200);
    out[4]=current_clip();
    float remaining=playback_remaining();
    memcpy(out+5,&remaining,4);
    out[6]=source.acquired;
}
/* 001B9A00 sub 1's stores (em_area_script.c op0A): +0x40 the bank
 * D_0028A490[0x96], +0x1F2 the clip, +0x2F3 1, +0x1F4 the rate, +0x200 0. */
int request(float rate) {
    em_live_set_u32(&actor,0x40,BANK_ADDRESS);
    em_live_set_u16(&actor,0x1F2,1);
    actor.bytes[0x2F3]=1;
    uint32_t bits;memcpy(&bits,&rate,4);em_live_set_u32(&actor,0x1F4,bits);
    em_live_set_u32(&actor,0x200,0);
    return player_pose_special_active();
}
/* 0x70003B8F is 1 here (as in the original run): 00183090 calls no face. */
static int face(void){++face_ticks;return 1;}
int advance(unsigned *out) {
    float local[22*16];
    unsigned old=placements;
    if(player_pose_commit_tick(face,local)!=1 || !player_pose_publish(local))return 0;
    /* Actual host publication must preserve world-space matrices and never
     * multiply them by the unrelated ordinary owner position/yaw. */
    if(memcmp(local,g.player_palette,sizeof local) || placements!=old || face_ticks)return 0;
    float hip[3];
    if(!player_pose_hip(hip) || memcmp(hip,local+28,sizeof hip))return 0;
    snapshot(out);return 1;
}
int leave(unsigned *out) {
    /* 00182DF0's nonzero-+0x2F3 branch publishes the record's evaluated
     * skeleton: world matrices from its own +B0 / +C4 (0015BCF0's
     * evaluation), never the host placement. */
    unsigned old=placements;
    float record[22*16];
    if(!player_pose_release() || placements!=old || player_pose_special_active())return 0;
    if(em_player_record_pose_palette(&source.record,record)<0 ||
       memcmp(record,g.player_palette,sizeof record))return 0;
    snapshot(out);out[7]=em_live_u32(&actor,0x40);return 1;
}
int ordinary(unsigned *out) {
    if(player_pose_stage()!=0)return 0;
    snapshot(out);return 1;
}
void cleanup(void) {player_pose_unload();free(special);special=NULL;}
'''

SANITIZER_MAIN = r'''
#include "game/em_interaction_runtime.h"
#include <assert.h>
#include <stdio.h>
static unsigned host_ticks;
static int acquire_host(void *p){(void)p;return player_pose_acquire();}
/* The AREA11 host's hooks while a script owner holds the player
 * (em_area11_interaction_host.c): 00183090 on the record in both. */
static int idle_host(void *p,float *out){(void)p;return player_pose_commit_tick(face,out);}
static int release_host(void *p){(void)p;return player_pose_release();}
static int publish_host(void *p,const float *out){(void)p;return player_pose_publish(out);}
static int frame_host(void *p,EmInteractionFrameEvent event){(void)p;(void)event;return 1;}
static int cinematic_host(void *p,float *out){
    (void)p;++host_ticks;
    return player_pose_commit_tick(face,out);
}
int main(void) {
    FILE *f=fopen(BANK_FILE,"rb");assert(f);
    static uint8_t bank[BANK_SIZE];
    assert(!fseek(f,BANK_OFFSET,SEEK_SET) && fread(bank,1,sizeof bank,f)==sizeof bank);fclose(f);
    assert(setup(bank,sizeof bank));
    EmInteractionFrame frame={0};EmInteractionRuntime runtime;
    float palette[22*16];int owner=0;
    EmInteractionRuntimeHooks hooks={NULL,acquire_host,idle_host,release_host,publish_host,frame_host,NULL};
    assert(em_interaction_runtime_init(&runtime,&frame,&g.model,palette,&hooks));
    assert(em_interaction_runtime_set_cinematic_player_worker(&runtime,cinematic_host));
    assert(em_interaction_runtime_claim(&runtime,&owner));
    frame.player_ready=2;frame.ready=1;
    assert(request(.5f));
    unsigned before[8]={0},after[8]={0};snapshot(before);
    for(unsigned i=0;i<120;++i)assert(em_interaction_runtime_player_tick(&runtime,0)==0);
    snapshot(after);assert(!memcmp(before,after,sizeof before)&&host_ticks==0);
    for(unsigned i=0;i<1388;++i)assert(em_interaction_runtime_player_tick(&runtime,1)==1);
    assert(host_ticks==1388 && em_live_u32(&actor,0x200)&0x1000 && !face_ticks);
    EmScript script={0};unsigned record[16]={7,0,4};
    assert(em_interaction_runtime_frame(&runtime,&owner,&script,(unsigned char*)record)==EM_SCRIPT_ADVANCE);
    assert(frame.player_ready==1 && !frame.selector && runtime.owner==&owner);
    snapshot(before);
    assert(em_interaction_runtime_player_tick(&runtime,0)==0);
    snapshot(after);assert(!memcmp(before,after,sizeof before));
    assert(em_interaction_runtime_player_tick(&runtime,1)==1);
    assert(!runtime.owner && !frame.player_ready && !player_pose_owned());
    assert(!player_pose_special_active() && em_live_u32(&actor,0x40)==EM_PLAYER_POSE_BANK_ADDRESS);
    assert(current_clip()==0 && playback_remaining()==80);
    assert(player_pose_stage()==0 && playback_remaining()==79);
    cleanup();
    puts("Actual bank96 player/shared ownership, status freeze, world pose and release ASan/UBSan PASS");
}
'''


def main():
    decomp = ROOT.parent / 'Extermination'
    elf = (decomp / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    source = (ROOT / 'src/game/em_game.c').read_text()
    first = source.index('void palette_apply_placement(')
    last = source.index('\n}\n', first)+3
    placement = source[first:last].replace('{\n', '{\n    ++placements;\n', 1)
    folder = ROOT / 'build/player_cinematic_reference'
    folder.mkdir(parents=True, exist_ok=True)
    bridge = folder / 'bridge.c'
    defines = (f'#define BANK_ADDRESS {BANK:#x}u\n#define BANK_OFFSET 0x41000\n#define BANK_SIZE 0x27000\n'
               f'#define BANK_FILE "{decomp / "extract/chunk15/f12_id44.bin"}"\n')
    bridge.write_text(defines + BRIDGE.replace('PLACEMENT_FUNCTION', placement))
    library = folder / 'player.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-ffp-contract=off', '-Wall', '-Wextra',
                    '-Werror', '-shared', '-fPIC', '-Isrc', str(bridge),
                    *['src/game/'+name+'.c' for name in ('em_player_pose', 'em_pose_bank',
                       'em_pose_transition', 'em_player_foot_stop', 'em_camera_rotation') + RECORD_POSE],
                    '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.request.argtypes = [C.c_float]
    native.setup.argtypes = [C.c_char_p, C.c_uint32]
    for name in ('snapshot', 'advance', 'leave', 'ordinary'):
        getattr(native, name).argtypes = [C.POINTER(C.c_uint)]
    original = Original(elf)
    bank = (decomp / 'extract/chunk15/f12_id44.bin').read_bytes()[0x41000:0x68000]
    default = (decomp / 'extract/chunk28/f01_id3c.bin').read_bytes()
    original.write(BANK, bank)
    original.write(DEFAULT, default)
    original.save(0x28A490+0x96*4, BANK)
    original.save(0x28A580, DEFAULT)
    original.save(PLAYER+0x40, DEFAULT)
    original.save(PLAYER+0x110, NODE)
    original.save(PLAYER+0xC, 21, 1)
    original.save(PLAYER+0x20C, 0, 2)
    original.save(PLAYER+0x2C, 0, 2)
    original.save(PLAYER+0x3C, bits(80))
    original.save(PLAYER+4, 4, 1)
    original.save(0x70003B8F, 1, 1)
    sample = [None]
    sample_calls = []
    def select(o):
        sample[0] = number(o.f[12])
        sample_calls.append(('select', sample[0]))
    def advance(o):
        assert sample[0] is not None
        sample[0] += number(o.f[12])
        sample_calls.append(('advance', number(o.f[12])))
    original.calls.update({0x1C8710: select, 0x1C87C0: advance,
                           0x1C8D50: lambda o: sample_calls.append(('transition', number(o.f[12])))})
    assert native.setup(bank, len(bank))
    values = (C.c_uint*8)()
    native.snapshot(values)
    assert list(values)[3:7] == [0, 0, bits(80), 1]
    record = struct.pack('<IIIffIII', 10, 0, 1, .5, 0, 1, 0, 0x96) + bytes(32)
    original.write(RECORD, record)
    original.run(0x1B9A00, (0, 0, RECORD))
    assert native.request(.5)
    native.snapshot(values)
    def expected():
        return [original.load(PLAYER+0x2F3, 1), original.load(PLAYER+0x1F2, 2),
                original.load(PLAYER+0x1F4), original.load(PLAYER+0x200),
                original.load(PLAYER+0x2C, 2)&0x7fff, original.load(PLAYER+0x3C), 1]
    assert list(values)[:7] == expected(), (list(values), expected(), 'request')
    assert original.load(PLAYER+0x40) == BANK and not sample_calls
    comparisons = 0
    for tick in range(1388):
        # Original83090 special initializer returns1;5BA50 consumes that
        # result before calling the original animation clock on this callback.
        original.run(0x183090, (PLAYER,))
        assert original.r[2] == 1
        original.run(0x1C64F0, (PLAYER,), (.5,))
        original.save(PLAYER+0x200, original.r[2])
        assert native.advance(values)
        assert list(values)[:7] == expected(), (tick, list(values), expected())
        if tick == 0:
            assert sample_calls == [('select', 0), ('advance', .5)], sample_calls
            assert values[5] == bits(690.5)
        comparisons += 1
    assert values[3] & 0x1000
    # The external-bank branch must restore the ordinary default directly.
    original.calls[0x1C6150] = lambda o: o.r.__setitem__(2, 21)
    original.calls[0x182D40] = lambda o: o.r.__setitem__(2, 0)
    original.run(0x182DF0, (PLAYER,))
    assert native.leave(values)
    assert values[0] == original.load(PLAYER+0x2F3, 1) == 0
    assert values[4] == original.load(PLAYER+0x2C, 2) == 0
    assert values[5] == original.load(PLAYER+0x3C) == bits(80)
    assert values[6] == original.load(0x70003B8F, 1) == 0
    # D_0028A580 (the default bank; the port's EM_PLAYER_POSE_BANK_ADDRESS)
    assert original.load(PLAYER+0x40) == DEFAULT and values[7] == 0x00D689C0
    assert native.ordinary(values) and values[5] == bits(79)
    native.cleanup()
    fixture = folder / 'fixture.c'
    fixture.write_text(defines + BRIDGE.replace('PLACEMENT_FUNCTION', placement) + SANITIZER_MAIN)
    executable = folder / 'player_test'
    subprocess.run(['cc', '-std=c11', '-O1', '-ffp-contract=off', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined', '-Isrc', str(fixture),
                    *['src/game/'+name+'.c' for name in ('em_player_pose', 'em_pose_bank',
                       'em_pose_transition', 'em_player_foot_stop', 'em_camera_rotation',
                       'em_interaction_runtime', 'em_interaction_frame', 'em_interaction_animation',
                       'em_script') + RECORD_POSE], 'src/em_model.c', '-o', str(executable)], cwd=ROOT, check=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)
    report = {'original_request_checks': 1, 'original_player_clock_callbacks': comparisons,
              'original_release_checks': 1, 'world_palette_hip_publications': comparisons,
              'sanitizer': 'actual resources and shared ownership PASS',
              'scope': 'original request/init/clock/release; channel sampler and face worker separate'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Original cinematic player PASS:', json.dumps(report))


if __name__ == '__main__': main()
