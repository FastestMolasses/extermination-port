#!/usr/bin/env python3
"""Original player bank request, deferred initialization, clock and release."""
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

BRIDGE = r'''
#include "game/em_player_pose_host.c"
EmGameState g;
static EmTransitionFade fade;
static EmPoseBank cinematic;
static unsigned placements;
const EmTransitionFade *em_frame_transition(void) { return &fade; }
void em_frame_request_quit(void) { abort(); }
PLACEMENT_FUNCTION
int setup(void) {
    g.model.bone_count=22; g.status.health=100;
    g.pos[0]=321;g.pos[1]=290;g.pos[2]=201;g.yaw=.7f;
    return player_pose_load("assets/player_channels.empc") && player_pose_opening_release() &&
        em_pose_bank_load(&cinematic,"assets/scene_snow/roger/encounter_player.empc") &&
        player_pose_acquire()==1;
}
void snapshot(unsigned *out) {
    out[0]=source.cinematic_mode;out[1]=source.cinematic_clip;
    memcpy(out+2,&source.cinematic_rate,4);out[3]=source.pose.flags;
    out[4]=source.pose.playback.clip->id;
    memcpy(out+5,&source.pose.playback.remaining,4);
    out[6]=source.pose.acquired;
}
int request(float rate) { return player_pose_cinematic_request(&cinematic,1,rate); }
int advance(unsigned *out) {
    float local[22*16];
    unsigned old=placements;
    if(player_pose_cinematic_tick(local,0)!=1 || !player_pose_publish(local))return 0;
    /* Actual host publication must preserve world-space matrices and never
     * multiply them by the unrelated ordinary owner position/yaw. */
    if(memcmp(local,g.player_palette,sizeof local) || placements!=old)return 0;
    float hip[3];
    if(!player_pose_hip(hip) || memcmp(hip,local+28,sizeof hip))return 0;
    snapshot(out);return 1;
}
int leave(unsigned *out) {
    unsigned old=placements;
    if(!player_pose_release() || placements!=old+1 || player_pose_cinematic_active())return 0;
    snapshot(out);return 1;
}
int ordinary(unsigned *out) {
    if(player_pose_stage()!=0)return 0;
    snapshot(out);return 1;
}
void cleanup(void) {player_pose_unload();em_pose_bank_free(&cinematic);}
'''

SANITIZER_MAIN = r'''
#include "game/em_interaction_runtime.h"
#include <assert.h>
static unsigned face_ticks;
static int acquire_host(void *p){(void)p;return player_pose_acquire();}
static int idle_host(void *p,float *out){(void)p;return player_pose_idle_tick(out);}
static int release_host(void *p){(void)p;return player_pose_release();}
static int publish_host(void *p,const float *out){(void)p;return player_pose_publish(out);}
static int frame_host(void *p,EmInteractionFrameEvent event){(void)p;(void)event;return 1;}
static int cinematic_host(void *p,float *out){
    (void)p;++face_ticks; /* Explicit face boundary: body/ownership fixture. */
    return player_pose_cinematic_active()?player_pose_cinematic_tick(out,0):player_pose_idle_tick(out);
}
int main(void) {
    assert(setup());
    EmInteractionFrame frame={0};EmInteractionRuntime runtime;
    float palette[22*16];int owner=0;
    EmInteractionRuntimeHooks hooks={NULL,acquire_host,idle_host,release_host,publish_host,frame_host,NULL};
    assert(em_interaction_runtime_init(&runtime,&frame,&g.model,palette,&hooks));
    assert(em_interaction_runtime_set_cinematic_player_worker(&runtime,cinematic_host));
    assert(em_interaction_runtime_claim(&runtime,&owner));
    frame.player_ready=2;frame.ready=1;
    assert(request(.5f));
    unsigned before[7],after[7];snapshot(before);
    for(unsigned i=0;i<120;++i)assert(em_interaction_runtime_player_tick(&runtime,0)==0);
    snapshot(after);assert(!memcmp(before,after,sizeof before)&&face_ticks==0);
    for(unsigned i=0;i<1388;++i)assert(em_interaction_runtime_player_tick(&runtime,1)==1);
    assert(face_ticks==1388 && source.pose.flags&0x1000);
    EmScript script={0};unsigned record[16]={7,0,4};
    assert(em_interaction_runtime_frame(&runtime,&owner,&script,(unsigned char*)record)==EM_SCRIPT_ADVANCE);
    assert(frame.player_ready==1 && !frame.selector && runtime.owner==&owner);
    snapshot(before);
    assert(em_interaction_runtime_player_tick(&runtime,0)==0);
    snapshot(after);assert(!memcmp(before,after,sizeof before));
    assert(em_interaction_runtime_player_tick(&runtime,1)==1);
    assert(!runtime.owner && !frame.player_ready && !player_pose_owned());
    assert(!player_pose_cinematic_active() && source.pose.bank==&source.bank);
    assert(source.pose.playback.clip->id==0 && source.pose.playback.remaining==80);
    assert(player_pose_stage()==0 && source.pose.playback.remaining==79);
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
    bridge.write_text(BRIDGE.replace('PLACEMENT_FUNCTION', placement))
    library = folder / 'player.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-ffp-contract=off', '-Wall', '-Wextra',
                    '-Werror', '-shared', '-fPIC', '-Isrc', str(bridge),
                    *['src/game/'+name+'.c' for name in ('em_player_pose', 'em_pose_bank',
                       'em_pose_transition', 'em_player_foot_stop', 'em_camera_rotation')],
                    '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.request.argtypes = [C.c_float]
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
    assert native.setup()
    values = (C.c_uint*7)()
    native.snapshot(values)
    assert list(values)[3:] == [0, 0, bits(80), 1]
    record = struct.pack('<IIIffIII', 10, 0, 1, .5, 0, 1, 0, 0x96) + bytes(32)
    original.write(RECORD, record)
    original.run(0x1B9A00, (0, 0, RECORD))
    assert native.request(.5)
    native.snapshot(values)
    def expected():
        return [original.load(PLAYER+0x2F3, 1), original.load(PLAYER+0x1F2, 2),
                original.load(PLAYER+0x1F4), original.load(PLAYER+0x200),
                original.load(PLAYER+0x2C, 2)&0x7fff, original.load(PLAYER+0x3C), 1]
    assert list(values) == expected(), (list(values), expected(), 'request')
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
        assert list(values) == expected(), (tick, list(values), expected())
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
    assert original.load(PLAYER+0x40) == DEFAULT
    assert native.ordinary(values) and values[5] == bits(79)
    native.cleanup()
    fixture = folder / 'fixture.c'
    fixture.write_text(BRIDGE.replace('PLACEMENT_FUNCTION', placement) + SANITIZER_MAIN)
    executable = folder / 'player_test'
    subprocess.run(['cc', '-std=c11', '-O1', '-ffp-contract=off', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined', '-Isrc', str(fixture),
                    *['src/game/'+name+'.c' for name in ('em_player_pose', 'em_pose_bank',
                       'em_pose_transition', 'em_player_foot_stop', 'em_camera_rotation',
                       'em_interaction_runtime', 'em_interaction_frame', 'em_interaction_animation',
                       'em_script')], 'src/em_model.c', '-o', str(executable)], cwd=ROOT, check=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)
    report = {'original_request_checks': 1, 'original_player_clock_callbacks': comparisons,
              'original_release_checks': 1, 'world_palette_hip_publications': comparisons,
              'sanitizer': 'actual resources and shared ownership PASS',
              'scope': 'original request/init/clock/release; channel sampler and face worker separate'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Original cinematic player PASS:', json.dumps(report))


if __name__ == '__main__': main()
