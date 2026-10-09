#ifndef EM_SFX_BANK_H
#define EM_SFX_BANK_H

#include <stdint.h>

/* ---- EMSR v2: the per-area SFX registry (tools/export_sfx_registry.py) ----
 * Every entry is one sound id resolved through the original 001FB9F0 path
 * for one scope: area (-1,-1) = the global record table's group-1 banks,
 * which are area-independent; any other (area, sub) = an area-tabled id or
 * a record bound to that area's region banks. Each audible entry is the
 * trigger script decoded into timed operations (001152D8 event loop); the
 * native driver below runs them tick by tick. docs/SFX_PITCH.md has the
 * byte layout, docs/SFX_SEQUENCER.md the driver translation. */
enum {
    EM_SFX_STATE_AUDIBLE = 1,     /* every operation runs natively         */
    EM_SFX_STATE_ABSENT = 2,      /* original 001FB9F0/00119EA0 return -1  */
    EM_SFX_STATE_UNSUPPORTED = 3, /* needs driver features not reproduced  */
    EM_SFX_OP_MAX = 32,
    EM_SFX_LADDER_MAX = 0x240
};

/* Script operations (001152D8 dispatch). */
enum {
    EM_SFX_OP_KEY_ON = 1,     /* A0 note vel>0 prog: 00115850              */
    EM_SFX_OP_KEY_OFF = 2,    /* A0 note 0 prog: 001176E0                  */
    EM_SFX_OP_PORTAMENTO = 3, /* B0 41 len depth prog note: 00118078       */
    EM_SFX_OP_END = 4         /* FF 2F 00: 00117C28                        */
};

typedef struct {
    uint16_t tick;        /* track tick the event is dispatched on        */
    uint8_t kind, note, prog;
    uint8_t flags;        /* tone flags: 0x01 sustained, 0x80 effect send */
    uint16_t sample;      /* registry sample index                        */
    uint16_t pitch;       /* key-on SPU2 pitch word, 4096 = 48000 Hz      */
    uint16_t pan;         /* D_00242630 word: left byte << 8 | right byte */
    uint32_t scalar;      /* 001179E0 six-way product >> 27               */
    uint16_t adsr1, adsr2;/* SPU2 envelope registers (command 3)          */
    uint8_t center;       /* tone +2  (voice +0x40)                       */
    int8_t fine;          /* tone +3  (voice +0x36)                       */
    uint8_t range;        /* tone +0xD (voice +0x3A)                      */
    uint8_t alloc;        /* tone +0  (voice +0x20, 00117428 pass 1 key)  */
    uint8_t priority;     /* tone +1  (voice +0x1E)                       */
    uint8_t length, depth;/* portamento controller bytes 2 and 3          */
} EmSfxOp;

typedef struct {
    uint32_t id;
    int16_t area, sub;
    uint8_t state, count;
    uint16_t reason;      /* UNSUPPORTED reason code (docs/SFX_PITCH.md) */
    uint16_t bank;        /* the 001FB9F0 record's group << 8 | bank index:
                           * the handle is D_00281D50[group * 20 + bank]
                           * (em_sfx resolves it at the start)            */
    EmSfxOp ops[EM_SFX_OP_MAX];
} EmSfxEntry;

typedef struct {
    uint32_t frames;      /* key-on pass, decoded at the 48 kHz base clock */
    uint32_t loop_start;  /* repeat frame, or UINT32_MAX for a one-shot    */
    int16_t *pcm;         /* frames, then (frames - loop_start) body frames
                           * decoded with the history carried over the loop */
} EmSfxSample;

typedef struct {
    unsigned entry_count, sample_count, ladder_count;
    EmSfxEntry *entries;
    EmSfxSample *samples;
    uint16_t *ladder;     /* D_00241D70 (00117918's pitch ladder)          */
} EmSfxRegistry;

/* Transactional: on any validation failure the old registry is kept and
 * zero is returned. Game thread (the driver that reads it runs there). */
int em_sfx_registry_load(EmSfxRegistry *registry, const char *path);
void em_sfx_registry_free(EmSfxRegistry *registry);
/* Exact (id, area, sub) match; NULL when absent from the registry. */
const EmSfxEntry *em_sfx_registry_find(const EmSfxRegistry *registry,
                                       unsigned id, int area, int sub);

/* 0011A218 + 001179E0: the two SPU volume words for one voice (with the
 * output mode D_0027F778 below: mono gives both the larger magnitude).
 * Requests outside [-0x1000, 0x1000] leave the track at the 00119EA0
 * defaults 0x1000/0x1000, as 0011A218 refuses the pair. */
/* D_0027F778, the SDK's output-mode word: 00119870(mode) stores it (one
 * storage; 0 at the boot = stereo, 1 = mono). */
void em_sfx_output_mode_00119870(int16_t mode);
int16_t em_sfx_output_mode(void);
void em_sfx_volume_words(uint32_t scalar, uint16_t pan, int32_t request_left,
                         int32_t request_right, uint16_t words[2]);
/* Fixed-mode SPU2 volume word as a gain: 15-bit two's complement,
 * gain = value / 0x4000, range -0x4000..0x3FFF (bit 14 is the sign, so
 * 0x4000 itself is -1.0). */
float em_sfx_volume_gain(uint16_t word);
/* 001FBF50's float_to_int(4096 * gain): truncation toward zero. */
int32_t em_sfx_request_word(float gain);
/* Output frame of a sequencer tick at the NTSC field rate 60000/1001. */
uint64_t em_sfx_tick_frame(unsigned tick, unsigned device_rate);
/* 00117918 over the registry ladder; -1 when an index leaves the table. */
int32_t em_sfx_ladder(const EmSfxRegistry *registry, int center, int note,
                      int fine, int bend, int range);

/* ---- SPU2 ADSR (hardware model, docs/SFX_SEQUENCER.md) -----------------
 * Documented register semantics: attack (linear/pseudo-exponential rise
 * to 0x7FFF), exponential decay to (N+1)*0x800, sustain in either
 * direction, release (linear/exponential) to zero; rate r = shift*4+step,
 * a step every 1 << max(0, shift-11) samples of max(0, 11-shift)-scaled
 * size, x4 wait above 0x6000 on an exponential rise, level-scaled steps on
 * an exponential fall. The wait saturates at 0x8000 samples (the 15-bit
 * counter the captured sustain feedback shows). NOT compared with SPU2
 * output: no capture exists. */
enum { EM_SFX_ENV_OFF = 0, EM_SFX_ENV_ATTACK, EM_SFX_ENV_DECAY,
       EM_SFX_ENV_SUSTAIN, EM_SFX_ENV_RELEASE };
typedef struct {
    uint16_t adsr1, adsr2;
    uint8_t phase;
    int32_t level;        /* ENVX, 0..0x7FFF                               */
    uint32_t counter;
} EmSfxEnvelope;
void em_sfx_envelope_key_on(EmSfxEnvelope *envelope, uint16_t adsr1,
                            uint16_t adsr2);
void em_sfx_envelope_key_off(EmSfxEnvelope *envelope);
/* One 48 kHz SPU sample. */
void em_sfx_envelope_step(EmSfxEnvelope *envelope);

/* ---- The sound driver (00119EA0 tracks, D_0027CCC0 voices) -------------
 * A translation of the parts of 001152D8 the registry scripts reach:
 * 00118EC0 reaper, the track event loop over EmSfxOp (00115850 key-on with
 * 00117428 allocation, 001176E0 key-off, 00118078 portamento, 00117C28
 * end), 00116598's per-voice pitch/volume re-send and the 00116598 tail,
 * and the NON/EON/KON/KOFF flush. Each 001157F0 command is applied to a
 * 48-voice SPU2 model (registers, ADSR, looping source cursor) and passed
 * to an optional sink so tests can compare it with original execution.
 * Not thread-safe: em_sfx.c owns one instance on the game thread (the
 * sequencer thread's tick runs at each field, em_sfx_field). */
enum { EM_SFX_TRACKS = 48, EM_SFX_VOICES = 48 };

typedef void (*EmSfxCommandSink)(void *context, int command, int voice,
                                 uint32_t a, uint32_t b);

typedef struct {
    const EmSfxEntry *entry;
    uint8_t allocated;    /* +0x32 */
    uint8_t running;      /* +0x34 */
    uint8_t ended;        /* +0x3E */
    uint8_t effect;       /* +0x42 */
    uint8_t pitch_dirty;  /* +0x50 */
    uint8_t volume_dirty; /* +0x52 */
    uint16_t pitch_scale; /* +0x44 */
    uint16_t porta_note;  /* +0x58 */
    uint16_t next, tick;  /* op cursor / ticks run                         */
    uint16_t handle;      /* +0x24: 00119EA0's bank handle (D_0027C6C0 slot),
                           * which 00115850 copies into a voice's +0x22   */
    int32_t left, right;  /* +0x48 / +0x4C request words                   */
} EmSfxTrack;

typedef struct {
    /* D_0027CCC0 record fields the translated paths read or write */
    uint16_t state, note, owner, release, serial, sustain, kind, age;
    uint16_t priority, alloc, bank, bend, range, prog, center, porta;
    uint16_t base, depth, remaining, length, pan;
    int16_t fine;
    uint32_t scalar;
    /* SPU2 voice model */
    uint16_t pitch, volume[2], adsr1, adsr2;
    const EmSfxSample *sample, *next_sample;
    uint8_t on;
    EmSfxEnvelope envelope;
    uint64_t phase;       /* source cursor, units of 1/(4096*rate) frames  */
    uint64_t frames;      /* output frames since key-on                    */
    uint64_t steps;       /* envelope steps since key-on                   */
} EmSfxVoice;

typedef struct {
    const EmSfxRegistry *registry;
    EmSfxTrack tracks[EM_SFX_TRACKS];
    EmSfxVoice voices[EM_SFX_VOICES];
    uint32_t cursor;      /* D_0027F740+0x30 allocation cursor             */
    uint32_t serial;      /* D_0027F740+0x34 key-on serial                 */
    uint64_t effect, effect_sent, noise, noise_sent, key_on, key_off;
    unsigned no_voice;    /* key-ons 00117428 refused (-1)                 */
    /* D_002817C0 source for the 00118EC0 reaper: NULL = the SPU2 model's
     * ENVX at the tick; em_sfx passes the IOP's reply (the status its
     * driver's last tick wrote before the previous exchange), tests may
     * substitute recorded values. */
    const int32_t *feedback;
    /* 1: the commands reach the SPU2 model only through
     * em_sfx_driver_apply (em_sfx's IOP side); 0: at once (the tests). */
    int deferred;
} EmSfxDriver;

/* Fresh driver: voices free (+0x06/+0x22/+0x24/+0x26 = 0xFFFF, +0x4E =
 * 0x78). stream_voices marks the voices the BGM stream path registers as
 * kind 3 (voices 0..3 in every AREA11 capture), which 00117428 never takes. */
void em_sfx_driver_init(EmSfxDriver *driver, const EmSfxRegistry *registry,
                        uint64_t stream_voices);
/* 00119EA0(handle, ...) + 0011A270(0x1000) + 0011A218: the lowest free
 * track, or -1. `handle` is the bank handle 001FB9F0 reads from D_00281D50
 * (track +0x24, a key-on's voice +0x22). */
int em_sfx_driver_start(EmSfxDriver *driver, const EmSfxEntry *entry,
                        unsigned handle, int32_t left, int32_t right);
/* The same on a given track; -1 when that track is not free. */
int em_sfx_driver_start_at(EmSfxDriver *driver, int track,
                           const EmSfxEntry *entry, unsigned handle,
                           int32_t left, int32_t right);
/* 0011A218: stores the pair (and marks a volume re-send) when both lie in
 * [-0x1000, 0x1000]; otherwise nothing changes. */
void em_sfx_driver_request(EmSfxDriver *driver, int track, int32_t left,
                           int32_t right);
/* 0011A070(track | hard << 15): frees an allocated SFX track and keys off
 * its voices; hard also zeroes their ADSR words (command 3) and state. */
void em_sfx_driver_stop(EmSfxDriver *driver, int track, int hard,
                        EmSfxCommandSink sink, void *context);
/* 00117428(tone[0], tone[1], bank) over the driver's voice table and
 * allocation cursor: -1 when every voice is busy (a kind-2 voice is never
 * taken over; see docs/SFX_SEQUENCER.md). */
int em_sfx_driver_allocate(EmSfxDriver *driver, unsigned alloc,
                           unsigned priority, unsigned bank);
/* 00119890(1, track): 2 while allocated as an SFX track, else 0. */
int em_sfx_driver_status(const EmSfxDriver *driver, int track);
/* The SPU2 side of one 001157F0 command (the IOP driver running it). */
void em_sfx_driver_apply(EmSfxDriver *driver, int command, int voice, uint32_t a,
                         uint32_t b);
/* One 001152D8 sequencer tick; commands are applied (unless deferred), then
 * passed on. */
void em_sfx_driver_tick(EmSfxDriver *driver, EmSfxCommandSink sink,
                        void *context);
/* Render (or with out == NULL only advance) every live SPU voice by
 * `frames` output frames at device_rate: rational pitch cursor, loop
 * replay, linear interpolation, ADSR at the 48 kHz clock, Q14 volumes.
 * Returns the voice frames produced (summed over voices). */
int em_sfx_driver_render(EmSfxDriver *driver, float *out, unsigned frames,
                         unsigned device_rate);
/* ENVX the reaper sees (the D_002817C0 feedback model). */
int32_t em_sfx_driver_envx(const EmSfxDriver *driver, int voice);
/* 1 while any track is allocated or any SPU voice still sounds. */
int em_sfx_driver_busy(const EmSfxDriver *driver);
/* SPU voices currently sounding. */
int em_sfx_driver_voices(const EmSfxDriver *driver);

/* ---- 001FC3C0 looped positional service / 001FC520 release ------------
 * `requested` is D_00281B70 (the id each service started on a track),
 * `snapshot` is D_00281C30 (001FB100's per-frame copy of it); `handle` is
 * the owner's service word (-1 = none). The callbacks are the original
 * callees: 00119890(1, track), 001FBF50 (0 = out of range; else the two
 * float_to_int request words), 0011A218, 0011A070(track) and
 * 001FB9F0(id, 0x1000, left, right) (reached through 001FBD50). */
typedef struct {
    void *context;
    int (*status)(void *context, int track);
    int (*gains)(void *context, int32_t *left, int32_t *right);
    void (*request)(void *context, int track, int32_t left, int32_t right);
    void (*stop)(void *context, int track);
    int (*start)(void *context, unsigned id, int32_t left, int32_t right);
    /* Optional authoritative store for the original handle word. Called
     * at every original store, including unchanged values; <0 refuses. */
    int (*store)(void *context,int32_t *handle,int32_t value);
} EmSfxLoopOps;
/* One 001FC3C0 call: (frame + ordinal) % 10 is the original cadence over
 * the scratchpad frame counter 0x70003B68 and the owner ordinal
 * 0x70003B8A. Returns the new handle. gains may return -1 for a
 * provider fault; that path performs no following voice/table/handle store. */
int32_t em_sfx_service_step(const EmSfxLoopOps *ops, int32_t requested[EM_SFX_TRACKS],
                         const int32_t snapshot[EM_SFX_TRACKS], int32_t *handle,
                         unsigned id, int32_t frame, int16_t ordinal);
/* 001FC520: stop a live handle (0011A070) and clear it. */
void em_sfx_service_release(const EmSfxLoopOps *ops,
                         int32_t requested[EM_SFX_TRACKS], int32_t *handle);

#endif
