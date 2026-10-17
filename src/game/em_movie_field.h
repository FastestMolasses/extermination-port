/* em_movie_field.h — the blocking movie 00203350 on the game's field clock.
 *
 * The original plays every movie inside one main-loop iteration (step M) and
 * paces it by vblanks alone; the port plays it the same way, one vblank per
 * em_frame_step, with the disc, the IOP, the DMA and the IPU at host speed
 * (CLAUDE.md). What the code waits for, read from the decomp's C
 * (docs/GS_EXACT.md 10.4 has the whole account and the fork probes):
 *
 *   00203350 (byte-matched): 00113280(0), 001FBC50, 001F9B20, a wait on
 *     001192D0, then two 001009C8 waits (sceGsSyncV: the next vblank, its
 *     field), 00205050 (the movie's display environment), 00205740, a wait
 *     on 00113C38, 00203460 until it returns 1; then 00205740, 001CCCC0,
 *     00200970(0), two more 001009C8 waits, 001F9BF0, D_00821058 = 0.
 *   00203460 (byte-matched) -> 002034C0 (NEARMISS): the decoder thread
 *     00206D10 (byte-matched), the frame ring D_007A55A0 with two slots
 *     (00203B20(..., 2), byte-matched), the vblank handler 00206030 (INTC 2)
 *     and the DMA-end handler 00206170 (channel 2).
 *   002036E0 (NEARMISS): the feed loop runs while its byte budget lasts and
 *     the state 00206B80 is not 3; each pass reads the pad (001B57E0) and
 *     with D_00810E70 & (0x70003B90 == 0 ? 0x8F0 : 0x800) and the decoder's
 *     picture count (D_002DF740 +8) >= 11 calls 00206B70 (state 1, the
 *     skip). Once the frame ring is full (00203B80) and the sound's first
 *     0x6000 bytes are on the IOP (00203990 -> 00206500) it calls
 *     002061B0(1) (byte-matched: 001009C8 until a vblank of field 0, then
 *     the gate D_00275C80 = 1) and 00206520 (the sound's key-on commands).
 *     After the loop: 00206BF0 (NEARMISS) pushes 4 bytes after the stream,
 *     then the drain loop runs until 00206CC0 or state 3; 00206200 closes
 *     the gate, 00206320 queues the sound's stop.
 *   00206030 (vblank, byte-matched) while the gate is open: the ring's
 *     head picture (00205F90, an asm body) in state 2 is sent on a field-0
 *     vblank (state 1), in state 1 on a field-1 vblank (state 0, D_00275C84
 *     = 1); no picture counts D_007A55F8. 00206170 (DMA end, byte-matched)
 *     then frees the slot (00206010: D_007A55AC -= 1).
 *   The decoder thread (sub_sceMpegGetPicture_decode_error, NEARMISS):
 *     per picture, state 1 ends it, else it waits for a free slot and
 *     decodes the next picture into it; at the stream's end 00206D10 waits
 *     for D_007A55AC == 0 and sets state 3.
 *   The sound's commands (00206520, 00206320, 001157F0's queue) leave with
 *     the next field's RPC 0x64 exchange, so the sound starts in the field
 *     after the gate's vblank and stops in the field after the play loop's
 *     last vblank.
 *
 * At host speed this gives (every count a vblank, one port step each):
 * the two opening waits; the gate on the first field-0 vblank after the
 * second; one vblank with the head picture still in state 2 (field 1);
 * then each picture on a field-0 and a field-1 vblank, in order, none
 * dropped; the play loop ends in the vblank of the last field-1 send;
 * two closing waits (fields 0 and 1), and the iteration's P reads field 1.
 * Two measured facts per movie complete it (EmMovieFieldConfig): the last
 * `held` pictures come out of the decoder only after the feed loop has
 * ended (the stream's last coded picture and the reference picture before
 * it need the bytes 00206BF0 pushes), and the feed loop ends `feed_end`
 * vblanks after the gate: its last bytes are the trailing sound, which the
 * rings take only as the SPU2 plays it (48 kHz, the same at host speed);
 * the IOP side that reports the play position is not translated, so the
 * field is the fork's measurement (GS_EXACT.md 10.4).
 *
 * Pure state: no platform, no clock. The frontend (em_frontend.c) drives
 * it from em_frame's movie vblank hook and pump; tests/movie_field_test.c.
 */
#ifndef EM_MOVIE_FIELD_H
#define EM_MOVIE_FIELD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_MOVIE_FIELD_RING = 2,        /* 002034C0's 00203B20(D_007A55A0, ..., 2) */
    EM_MOVIE_FIELD_SKIP_COUNT = 11  /* 002036E0: D_002DF740 +8 >= 0xB */
};

typedef struct {
    uint32_t pictures;   /* the movie's pictures, in display order */
    uint32_t held;       /* the last pictures the decoder outputs only after the feed loop */
    uint32_t feed_end;   /* vblanks from the gate's to the one whose period ends the feed loop */
} EmMovieFieldConfig;

typedef enum {
    EM_MOVIE_FIELD_WAIT_SYNC2,  /* 00203350's second opening 001009C8 */
    EM_MOVIE_FIELD_WAIT_GATE,   /* 002061B0(1): a field-0 vblank */
    EM_MOVIE_FIELD_PLAY,        /* the feed loop / the drain loop */
    EM_MOVIE_FIELD_WAIT_SYNC3,  /* the closing 001009C8 waits */
    EM_MOVIE_FIELD_WAIT_SYNC4,
    EM_MOVIE_FIELD_DONE         /* 00203350 returned */
} EmMovieFieldPhase;

typedef struct {
    EmMovieFieldConfig cfg;
    EmMovieFieldPhase phase;
    uint32_t vblank;       /* vblanks since the arming one (0: the first 001009C8 wait's) */
    uint8_t gate;          /* D_00275C80 */
    uint32_t gate_vblank;  /* the vblank 002061B0 returned on */
    /* the frame ring D_007A55A0 */
    uint32_t ring_head;    /* the oldest picture in the ring */
    uint32_t ring_count;   /* D_007A55AC */
    uint8_t head_state;    /* the head slot's word: 2 decoded, 1 field 0 sent, 0 field 1 sent */
    /* the decoder thread */
    uint32_t decoded;      /* D_002DF740 +8: pictures the decoder has output */
    uint8_t decoder_stopped;
    uint8_t skip;          /* 00206B70: the state word +0xA8 = 1 */
    uint8_t feeding;       /* the feed loop runs */
    uint8_t ended;         /* the play loop has ended (state 3) */
    uint32_t end_vblank;   /* its vblank: the last field-1 send's */
    /* what the vblank handler did */
    int32_t shown;         /* the picture DISPFB2 shows (its last send); -1 none */
    uint8_t sent;          /* this vblank: 0 none, 1 a field-0 send, 2 a field-1 send */
    uint32_t no_picture;   /* D_007A55F8: open-gate vblanks with no picture in the ring */
    uint32_t sends[2];     /* field-0 / field-1 sends */
    /* the sound (the SPU2's view, field by field) */
    uint8_t sound;         /* 0 before the key-on, 3 key-on queued, 1 sounding, 4 stop queued, 2 stopped */
    uint8_t sound_field;   /* this vblank's field sounds */
    uint32_t sound_fields; /* fields that sounded */
} EmMovieField;

/* The arming step's M: 00203350 has entered and its first 001009C8 wait
 * returned on that step's vblank. 0, or -1 for a configuration the model
 * cannot play (fewer pictures than the ring and the held ones). */
int em_movie_field_begin(EmMovieField *m, const EmMovieFieldConfig *cfg);
/* One vblank (the next step's top), `field` its GS field: the waits that
 * return on it and the handler 00206030's send. Sets m->sent and
 * m->sound_field for this vblank. */
void em_movie_field_vblank(EmMovieField *m, int field);
/* The period after the vblank, at host speed: the decoder thread first
 * (it takes a freed slot before the feed loop's next pad read), then
 * em_movie_field_skip_ready, then em_movie_field_main with the pad's skip. */
void em_movie_field_decode(EmMovieField *m);
/* The feed loop runs and the decoder's count has reached 11: a skip
 * button held now is honoured (002036E0's test). */
int em_movie_field_skip_ready(const EmMovieField *m);
/* The main thread's period: `skip` the pad's skip (honoured only when
 * em_movie_field_skip_ready). 1 while 00203350 holds the iteration, 0 when
 * it returns in this period (the iteration's N..W run in this step). */
int em_movie_field_main(EmMovieField *m, int skip);

#ifdef __cplusplus
}
#endif

#endif /* EM_MOVIE_FIELD_H */
