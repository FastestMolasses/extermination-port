/* The live music and voice streams (WP-8b): the one owner of the original
 * stream lanes (em_stream_lanes_original, docs/STREAM_LANES.md) and of the
 * IOP side they talk to (em_iop_stream, docs/IOP_STREAM.md).
 *
 * What runs where (the original's positions):
 *   001AAE40 start-up   em_stream_live_boot: the SNDN2DRV.IRX bring-up's
 *                       buffers (sub_cdrom0_IRX_SNDN2DRV_IRX_1's tail),
 *                       sub_O_STREAM_MUSIC_DAT_1, then 001F9820
 *   every field         em_stream_live_field: D_00810E90 += 1 (the vblank
 *                       handler), then the IOP's field (001152D8's RPC 0x64
 *                       exchange and the driver ticks inside the field)
 *   step H (001FB100)   em_stream_live_step_h: 001F9CF0 unless D_00821058 == 1
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
/* Main-loop step H (001FB100's 001F9CF0; the caller skips it while
 * D_00821058 == 1, as 001FB100 does). */
int em_stream_live_step_h(void);

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

/* D_00282154 + lane (lb): a lane's active byte; 0 before the boot. */
int8_t em_stream_live_active(int lane);
/* D_00282157 (lb): 001FA0D0's read phase; 0 before the boot. */
uint8_t em_stream_live_read_phase(void);
/* D_00282178 + 4 * lane: the lane's cue; 0 before the boot. */
int32_t em_stream_live_cue(int lane);

/* Audio thread: sum the rendered stream frames (em_iop_stream_mix). */
void em_stream_live_mix(float *out, int frames, int device_rate);

#ifdef __cplusplus
}
#endif

#endif /* EM_STREAM_LIVE_H */
