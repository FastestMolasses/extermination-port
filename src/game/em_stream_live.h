/* The live music and voice streams (WP-8b): the one owner of the original
 * stream lanes (em_stream_lanes_original, docs/STREAM_LANES.md) and of the
 * IOP side they talk to (em_iop_stream, docs/IOP_STREAM.md).
 *
 * What runs where (the original's positions):
 *   001AAE40 start-up   em_stream_live_boot: the SNDN2DRV.IRX bring-up's
 *                       buffers (sub_cdrom0_IRX_SNDN2DRV_IRX_1's tail), the
 *                       driver's start-up block (command 0x1E),
 *                       sub_O_STREAM_MUSIC_DAT_1, then 001F9820
 *   area loads          em_stream_live_001FB370: the sound-bank upload
 *                       (em_sound_bank) for the module loader's 001FF590
 *   every field         em_stream_live_field: D_00810E90 += 1 (the vblank
 *                       handler), then the IOP's field (001152D8's RPC 0x64
 *                       exchange and the driver ticks inside the field)
 *   step H (001FB100)   em_stream_live_step_h: the whole 001FB100 (001F9CF0,
 *                       the D_0028215B / D_0081011C output-mode commit, the
 *                       D_00281B70 copy, 001FC6E0's delayed cues) unless
 *                       D_00821058 == 1
 *   everywhere else     the original entry points below, called by the
 *                       scene bindings, the message service, the scripts
 *   audio thread        em_stream_live_mix (summed by em_bgm's callback)
 *
 * Storage: the lanes' state (D_00281FD0.., D_00282154..8C, the ring
 * D_00281CF0 with D_00275B30/34, D_00275B2C) and D_00810E90 live here once.
 * The lanes' view of bytes owned elsewhere (D_008106C8, D_00810D38,
 * D_00810700, D_008104E4, D_008106F4, D_008106F5) is loaded from the
 * canonical scene state before every entry and the bytes the lanes write
 * (D_008106F4, D_008106F5, D_00810D38) are stored back after it.
 *
 * Fail-stop: a missing export, a latched lane or IOP fault, or a call
 * before the boot latches a fault here; every entry then returns -1 and the
 * first fault is reported once on stderr. */
#ifndef EM_STREAM_LIVE_H
#define EM_STREAM_LIVE_H

#include <stdint.h>
#include <stdio.h>

#include "game/em_iop_stream.h"
#include "game/em_sound_bank.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 001AAE40's start-up. `path` is assets/streams/streams.emst
 * (tools/export_streams.py). 0, or -1 (fault latched, message printed). */
int em_stream_live_boot(const char *path);
/* After em_bgm_shutdown (the device no longer mixes). */
void em_stream_live_shutdown(void);
int em_stream_live_failed(void);

/* One NTSC field (the top of every em_frame_step, movie frames included). */
int em_stream_live_field(void);
/* Main-loop step H (001FB100, em_slg_001FB100; the caller skips it while
 * D_00821058 == 1, as 001FB100 does). */
int em_stream_live_step_h(void);
/* 001FBC50's tail: D_00281F30's ten records back to {0, -1} (the boot, and
 * every 001FBC50 the scene bindings run). */
void em_stream_live_001FBC50_cues(void);
/* D_00281F30, 001FC6E0's ten delayed cues {delay, cue, a2, a3} (0xA0
 * bytes, the one storage step H's 001FB100 reads): their writer 001FC580
 * (the boxes' and drums' break cue, em_area11_boxes) stores through this
 * view. NULL before the boot. */
int32_t (*em_stream_live_d281F30(void))[4];

/* The original entry points (arguments are the original's; 0, or -1 on a
 * fault). */
int em_stream_live_001FA790(int32_t lane, int32_t cue);
int em_stream_live_001FAAC0(int32_t lane);
int em_stream_live_001FAB50(void);
int em_stream_live_001FABB0(void);
int em_stream_live_001FAD70(int32_t lane, int32_t fade, int32_t release);
int em_stream_live_001FAE70(int32_t a0);
int em_stream_live_001FD470(int32_t mask);
int em_stream_live_00119828(int32_t a0, int32_t a1, int32_t a2);
/* 001FA5A0(cue): the voice ring push the message service's 001FD580 /
 * 001FD6A0 make (em_message_voice_ring_push over D_00281CF0 / D_00275B30). */
int em_stream_live_001FA5A0(int32_t cue);

/* The sound-bank upload 001FB370 (em_sound_bank) over this IOP backend:
 * bind once after the boot with D_00264890 (the loader's pack), then the
 * module loader's area streamer calls it through its bank hook
 * (docs/IOP_STREAM.md "The sound-bank transfer"). 0, or -1 (fault). */
int em_stream_live_bind_sound_bank(const int32_t base[5]);
int em_stream_live_001FB370(uint32_t address, const uint8_t *bytes, uint32_t size, uint32_t *result);
/* The bound instance (tick log, tests), NULL before the binding. */
const EmSoundBank *em_stream_live_sound_bank(void);
/* The IOP the lanes run on (tests), NULL before the boot. */
const EmIopStream *em_stream_live_iop(void);
/* A screen-module loader read (em_module_loader's 00112440) on the same
 * disc: em_iop_stream_loader_read (the PS2 disc-drive timing switch only;
 * nothing at host speed). No-op before the boot. */
void em_stream_live_loader_read(uint32_t lsn, uint32_t sectors, int measured);

/* D_00282154 + lane (lb): a lane's active byte; 0 before the boot. */
int8_t em_stream_live_active(int lane);
/* D_00282157 (lb): 001FA0D0's read phase; 0 before the boot. */
uint8_t em_stream_live_read_phase(void);
/* Canonical committed D_0028215B, read-only. NULL before boot or on a fault;
 * callers must not replace an unavailable mode with an assumed stereo bit. */
const uint8_t *em_stream_live_output_mode(void);
/* D_00282178 + 4 * lane: the lane's cue; 0 before the boot. */
int32_t em_stream_live_cue(int lane);
/* The tick log's "stream" row (tools/test_level_smoke.py
 * check_director_beat): D_00810E90, D_00282157 (read phase), D_00282158
 * (read lane), D_00282154..56 (active) and each lane's +0x03 (load).
 * 0 before the boot. */
int em_stream_live_log(uint32_t out[9]);
/* The EE sound state at the main-loop top (chain step AUDIO): sampled at
 * every field right after the vblank's sound work (em_sfx_field, then the
 * IOP exchange), so it is the state the previous frame and the vblank's
 * 001152D8 tick left, the point at which the decomp's audio captures
 * (build/s87/audio, docs/CAPTURES_AUDIO.md) record their per-frame state.
 * The level smoke's tick log writes it as "snd" (tools/level_smoke_audio.py
 * compares it with the captures). */
enum { EM_SOUND_VOICE_FIELDS = 17 };
typedef struct {
    int valid;                 /* 0 before the boot / the SFX registry      */
    uint64_t ticks;            /* 001152D8 ticks run (em_sfx_field)          */
    uint32_t d810E90;          /* the vblank count                          */
    int8_t active[3];          /* D_00282154..56                            */
    int8_t read_phase, read_lane; /* D_00282157 / 58                        */
    uint8_t mono;              /* D_0028215B                                */
    int32_t cue[3];            /* D_00282178[3]                             */
    int32_t music_clip;        /* D_00275B2C                                */
    int32_t ring[16];          /* D_00281CF0                                */
    int8_t ring_head, ring_tail; /* D_00275B30 / D_00275B34                 */
    int32_t b70[48], c30[48];  /* D_00281B70 / D_00281C30                   */
    int32_t f30[10][4];        /* D_00281F30                                */
    uint32_t cursor, serial;   /* D_0027F740 + 0x30 / + 0x34                */
    /* D_0027CCC0[v] at +0x00, 0x02, 0x06, 0x08, 0x0A, 0x0C, 0x1A, 0x1C,
     * 0x1E, 0x20, 0x44, 0x4E, 0x60, 0x62, 0x64, 0x3E and 0x22 (the fields
     * the driver translation keeps; tools/test_area11_sfx_reference.py
     * VOICE_FIELDS, then the bank handle). */
    uint16_t voice[48][EM_SOUND_VOICE_FIELDS];
    int32_t envx[48];          /* D_002817C0: the reaper's ENVX feedback     */
    /* Diagnostics (the captures do not hold D_0027E0C0): each allocated
     * track's sound id (its registry entry), -1 for a free track. */
    int32_t track_id[48];
} EmSoundSample;
/* The last field's sample (valid 0 before the first). */
const EmSoundSample *em_stream_live_sound_sample(void);
/* The drive's counters (em_iop_stream_drive_stats) and whether the PS2
 * disc-drive timing switch was on at the boot (*ps2_timing; em_settings);
 * 0 before the boot. */
int em_stream_live_drive_stats(EmIopDriveStats *out, int *ps2_timing);
/* One "stream drive: <mode>: ..." line with the mode and the counters (the
 * level smoke and newgame-control print it; tools/test_level_smoke.py and
 * tools/test_rand_order.py read the mode from it). Nothing before the boot. */
void em_stream_live_drive_report(FILE *out);

/* Audio thread: sum the rendered stream frames (em_iop_stream_mix). */
void em_stream_live_mix(float *out, int frames, int device_rate);

#ifdef __cplusplus
}
#endif

#endif /* EM_STREAM_LIVE_H */
