#!/usr/bin/env python3
"""The player's special bank (001B9A00 sub 1), 00183090's initialization and clock
through 001C64F0, and 00182DF0's nonzero-+0x2F3 release on the stage's own takeover
(+4 = 4): the native 0015BA50 +4 = 4 composition (em_player_stage_dispatch with the live
stage's pose callees, player_pose_stage_advance) and 0015B530's 00182DF0
(em_player_stage_00182DF0 through player_pose_stage_release) against the original
routines (census L22; the takeover moved onto the stage in chain C7)."""
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
/* The scripted frame is open (0x70003B8D = 2) and 00182D70 set 3B8F = 1. */
static EmPlayerStageScene stage_scene = { .spad3B8D = 2, .spad3B8F = 1, .d8106F1 = &d8106F1 };
static EmPlayerStageGlobals stage_globals = { .d810707 = &d810707 };
static uint8_t *special;
static unsigned face_ticks, routine_calls, end_calls;
static int (*end_extra)(void);
const EmTransitionFade *em_frame_transition(void) { return &fade; }
void em_frame_request_quit(void) { abort(); }
PLACEMENT_FUNCTION
/* The live stage's composition (em_player_stage_live.c): the stage host's
 * callees on the record pose, 00183090 (em_player_stage_commit), the
 * advance through player_pose_stage_advance, 0015B530 with 001837A0 (empty)
 * and 00182DF0 (player_pose_stage_release over em_player_stage_00182DF0). */
static EmPlayerStageHost host;
static EmPlayerStageMajor4 major4;
static EmPlayerStageRelease release;
static EmPlayerStageWorkers workers;
/* 0x70003B8F is 1 here (as in the original run): 00183090 calls no face. */
static int face(void *c){(void)c;++face_ticks;return 0;}
static int advance_worker(void *c, EmPlayerLiveActor *a, float step, uint32_t *flags)
{(void)c;(void)a;return player_pose_stage_advance(step, flags);}
static int routine_001837A0(void *c, EmPlayerLiveActor *a){(void)c;(void)a;++routine_calls;return 0;}
static int routine_unbound(void *c, EmPlayerLiveActor *a){(void)c;(void)a;return -1;}
static int routine_00182DF0(void *c, EmPlayerLiveActor *a){(void)c;return player_pose_stage_release(a);}
/* 00182DF0's special branch reads no row lookup and the record's +1C is 0. */
static int no_lookup(void *c, EmPlayerLiveActor *a, int a1, int a2, int a3, int16_t *clip)
{(void)c;(void)a;(void)a1;(void)a2;(void)a3;(void)clip;return -1;}
static int no_link(void *c, uint32_t word, uint8_t value){(void)c;(void)word;(void)value;return -1;}
static int no_clip_zero(void *c, EmPlayerLiveActor *a){(void)c;(void)a;return -1;}   /* 00174AB0: not on the special branch */
/* D_0028A580: the player bank the record pose attaches at (the captured +0x40). */
static int bank_word(void *c, uint32_t *word){(void)c;*word=EM_PLAYER_POSE_BANK_ADDRESS;return 0;}
/* 001C6150(+0x44): the player model's 21 nodes (the original side hooks 001C6150 to 21). */
static int node_count(void *c, uint32_t model, uint8_t *count){(void)c;(void)model;*count=21;return 0;}
static int row_a00(void *c, unsigned index, int16_t *clip){
    (void)c;
    const uint8_t *p = player_pose_record_bytes(0x00248A00u + 2u * index, 2);
    if (!p) return -1;
    *clip = (int16_t)(uint16_t)(p[0] | p[1] << 8);
    return 0;
}
static int row_c90(void *c, int clip, int16_t *value){(void)c;return player_pose_row0(clip, value);}
static int end_hook(void *c){(void)c;++end_calls;return end_extra ? end_extra() : 1;}
int setup(const uint8_t *bank, uint32_t size) {
    g.model.bone_count=22; g.status.health=100;
    g.pos[0]=321;g.pos[1]=290;g.pos[2]=201;g.yaw=.7f;
    special=malloc(size);
    if(!special)return 0;
    memcpy(special,bank,size);
    if (!player_pose_load(PLAYER_CLIP_BANK_PATH, PLAYER_CLIP_ROW0_PATH) ||
        !player_pose_attach(&actor, &d8106F3, &stage_scene, &stage_globals) || !player_pose_record_displayed())
        return 0;
    host.stage = &stage_scene; host.globals = &stage_globals;
    EmPlayerStageCallees *c = &host.callees;
    c->context = player_pose_record_host();
    c->w001D0C70 = face;
    c->bone_init = em_pose_host_stage_bone_init;
    c->clip_init = em_pose_host_stage_clip_init;
    c->request = em_pose_host_stage_request;
    c->clip_lookup = no_lookup;
    c->link1C = no_link;
    em_player_stage_workers_bind(&workers, &host);
    workers.advance = advance_worker;
    major4.stage = &stage_scene;
    for (int i = 0; i < EM_PLAYER_MAJOR4_COUNT; ++i) major4.routine[i] = routine_unbound;
    major4.routine[EM_PLAYER_MAJOR4_001837A0] = routine_001837A0;
    major4.routine[EM_PLAYER_MAJOR4_00182DF0] = routine_00182DF0;
    workers.major[4] = em_player_stage_0015B530;
    workers.major_context[4] = &major4;
    release = (EmPlayerStageRelease){ &host, NULL, bank_word, node_count, row_a00, row_c90, no_clip_zero };
    player_pose_set_release_worker(em_player_stage_00182DF0, &release);
    player_pose_set_takeover_end_hook(end_hook, NULL);
    /* 0015B130's prelude admitted the player (+4 = 4; the original side
     * holds +4 = 4 too). */
    actor.bytes[4] = 4;
    return player_pose_takeover_admitted() && player_pose_map_region(BANK_ADDRESS, size, special);
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
/* 0015BCF0's animate step and the display of the record's pose: world
 * matrices from the record's own evaluation, never the host placement. */
static int display(void) {
    unsigned old=placements;
    float record[22*16];
    if(player_pose_animate()<0 || player_pose_display()!=1 || placements!=old)return 0;
    if(em_player_record_pose_palette(&source.record,record)<0 ||
       memcmp(record,g.player_palette,sizeof record))return 0;
    float hip[3];
    return player_pose_hip(hip) && !memcmp(hip,g.player_palette+28,sizeof hip);
}
/* One +4 = 4 stage: 0015BA50's 00183090 and advance, then 0015B530's
 * 001837A0 (+5 = 0 under 0x70003B8D). */
int advance(unsigned *out) {
    unsigned calls=routine_calls;
    if(em_player_stage_dispatch(&actor,&workers)!=0 || routine_calls!=calls+1 || face_ticks || !display())return 0;
    snapshot(out);return 1;
}
/* 0015B530 with 0x70003B8D clear: 00182DF0 (its nonzero-+0x2F3 branch). */
int leave(unsigned *out) {
    stage_scene.spad3B8D=0;
    unsigned ends=end_calls;
    if(em_player_stage_0015B530(&major4,&actor)!=0 || end_calls!=ends+1 || player_pose_special_active() ||
       !display())return 0;
    snapshot(out);out[7]=em_live_u32(&actor,0x40);
    out[8]=actor.bytes[4];out[9]=actor.bytes[5];out[10]=actor.bytes[6];out[11]=actor.bytes[0x1F0];
    out[12]=actor.bytes[0xC];out[13]=em_live_u16(&actor,0x20C);out[14]=stage_scene.spad3B8F;
    return 1;
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
static EmInteractionRuntime runtime;
static int frame_host(void *p,EmInteractionFrameEvent event){(void)p;(void)event;return 1;}
static int end_runtime(void){return em_interaction_runtime_staged_release(&runtime);}
int main(void) {
    FILE *f=fopen(BANK_FILE,"rb");assert(f);
    static uint8_t bank[BANK_SIZE];
    assert(!fseek(f,BANK_OFFSET,SEEK_SET) && fread(bank,1,sizeof bank,f)==sizeof bank);fclose(f);
    assert(setup(bank,sizeof bank));
    EmInteractionFrame frame={0};float palette[22*16];int owner=0;
    EmInteractionRuntimeHooks hooks={NULL,NULL,NULL,NULL,NULL,frame_host,NULL};
    assert(em_interaction_runtime_init(&runtime,&frame,&g.model,palette,&hooks));
    /* A script owner's token (its op07 opened the frame): staged. The
     * runtime takes nothing; the stage performs the takeover. */
    frame.selector=2;
    assert(em_interaction_runtime_claim_scripted(&runtime,&owner) && runtime.staged);
    assert(request(.5f));
    unsigned before[16]={0},after[16]={0};snapshot(before);
    for(unsigned i=0;i<120;++i)assert(em_interaction_runtime_player_tick(&runtime,1)==0);
    snapshot(after);assert(!memcmp(before,after,sizeof before) && runtime.owner==&owner);
    for(unsigned i=0;i<1388;++i)assert(advance(after));
    assert(routine_calls==1388 && em_live_u32(&actor,0x200)&0x1000 && !face_ticks);
    /* The selector clears: the next stage's 0015BA50 commits and advances,
     * then 0015B530's 00182DF0 releases and the end hook ends the token. */
    end_extra=end_runtime;
    frame.selector=0;stage_scene.spad3B8D=0;
    assert(em_player_stage_dispatch(&actor,&workers)==0);
    assert(!runtime.owner && !runtime.staged && !player_pose_owned() && end_calls==1);
    assert(actor.bytes[4]==1 && !actor.bytes[5] && !actor.bytes[0x1F0] && !stage_scene.spad3B8F);
    assert(!player_pose_special_active() && em_live_u32(&actor,0x40)==EM_PLAYER_POSE_BANK_ADDRESS);
    assert(current_clip()==0 && playback_remaining()==80);
    assert(player_pose_stage()==0 && playback_remaining()==79);
    cleanup();
    puts("Actual bank96 player on the stage's own takeover, staged token and release ASan/UBSan PASS");
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
                       'em_pose_transition', 'em_player_foot_stop', 'em_camera_rotation',
                       'em_effect_original') + RECORD_POSE],
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
    values = (C.c_uint*16)()
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
        # Original 00183090's special initializer returns 1; 0015BA50 consumes
        # that result before calling the original animation clock.
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
    # 0015B530 with 0x70003B8D clear: 00182DF0's external-bank branch must
    # restore the ordinary default directly, and its tail release the player.
    original.save(0x70003B8D, 0, 1)
    original.calls[0x1C6150] = lambda o: o.r.__setitem__(2, 21)
    original.run(0x182DF0, (PLAYER,))
    assert native.leave(values)
    assert values[0] == original.load(PLAYER+0x2F3, 1) == 0
    assert values[4] == original.load(PLAYER+0x2C, 2) == 0
    assert values[5] == original.load(PLAYER+0x3C) == bits(80)
    assert values[6] == 0 and values[14] == original.load(0x70003B8F, 1) == 0
    assert [values[8], values[9], values[10], values[11], values[12], values[13]] == \
        [original.load(PLAYER+4, 1), original.load(PLAYER+5, 1), original.load(PLAYER+6, 1),
         original.load(PLAYER+0x1F0, 1), original.load(PLAYER+0xC, 1), original.load(PLAYER+0x20C, 2)], \
        ('00182DF0 record', list(values)[8:14])
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
                       'em_script', 'em_effect_original') + RECORD_POSE], 'src/em_model.c', '-o', str(executable)], cwd=ROOT, check=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)
    report = {'original_request_checks': 1, 'original_player_clock_callbacks': comparisons,
              'original_release_checks': 1, 'world_palette_hip_publications': comparisons,
              'sanitizer': 'actual resources, the staged token and the stage release PASS',
              'scope': 'original request/init/clock/release; channel sampler and face worker separate'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Original cinematic player PASS:', json.dumps(report))


if __name__ == '__main__': main()
