/* The blocking movie 00203350 on the game's field clock (em_movie_field.h).
 * The comments name the original function each step stands for and say what
 * it does; docs/GS_EXACT.md 10.4 has the evidence. */
#include "game/em_movie_field.h"

#include <string.h>

int em_movie_field_begin(EmMovieField *m, const EmMovieFieldConfig *cfg)
{
    if (!m || !cfg) return -1;
    memset(m, 0, sizeof *m);
    /* The gate needs a full ring (00203B80) before the feed loop's end:
     * a movie with fewer pictures than the ring and the held ones would
     * wait on the drive, which host speed has no model of. */
    if (cfg->pictures < cfg->held + EM_MOVIE_FIELD_RING || cfg->feed_end == 0) return -1;
    m->cfg = *cfg;
    /* 00203350 entered on the arming step and its first 001009C8 returned
     * on that step's vblank; the second waits for the next one. */
    m->phase = EM_MOVIE_FIELD_WAIT_SYNC2;
    m->shown = -1;
    return 0;
}

void em_movie_field_vblank(EmMovieField *m, int field)
{
    m->vblank++;
    m->sent = 0;
    /* The sound's commands queued in the last period (00206520's key-on,
     * 00206320's stop) reach the IOP with this field's RPC 0x64 exchange. */
    if (m->sound == 3) m->sound = 1;      /* key-on queued */
    else if (m->sound == 4) m->sound = 2; /* stop queued */
    m->sound_field = m->sound == 1;
    if (m->sound_field) m->sound_fields++;

    /* 00206030, the vblank handler, while the gate D_00275C80 is open. */
    if (m->gate) {
        if (m->ring_count == 0) {
            m->no_picture++;                      /* D_007A55F8 */
        } else if (field == 0 && m->head_state == 2) {
            m->shown = (int32_t)(m->decoded - m->ring_count);
            m->head_state = 1;                    /* the field-0 half */
            m->sent = 1;
            m->sends[0]++;
        } else if (field != 0 && m->head_state == 1) {
            m->head_state = 0;                    /* the field-1 half */
            m->sent = 2;
            m->sends[1]++;
            /* 00206170 at the transfer's end (host speed: this vblank):
             * D_00275C84 set, 00206010 frees the head slot. */
            m->ring_count--;
            if (m->ring_count) m->head_state = 2;
        }
    }

    /* The 001009C8 waits that return on this vblank. */
    switch (m->phase) {
    case EM_MOVIE_FIELD_WAIT_SYNC2:
        /* 00205050, 00205740, 00113C38 and 002034C0 at host speed: the
         * decoder thread starts and the feed loop runs in this period. */
        m->phase = EM_MOVIE_FIELD_PLAY;
        m->feeding = 1;
        break;
    case EM_MOVIE_FIELD_WAIT_GATE:
        /* 002061B0(1) loops while 001009C8 returns 1: a field-0 vblank
         * returns it; then the gate opens (after this vblank's handler). */
        if (field == 0) {
            m->phase = EM_MOVIE_FIELD_PLAY;
            m->gate = 1;
            m->gate_vblank = m->vblank;
            m->sound = 3;                         /* 00206520 */
        }
        break;
    case EM_MOVIE_FIELD_WAIT_SYNC3:
        m->phase = EM_MOVIE_FIELD_WAIT_SYNC4;
        break;
    case EM_MOVIE_FIELD_WAIT_SYNC4:
        m->phase = EM_MOVIE_FIELD_DONE;           /* 001F9BF0, D_00821058 = 0 */
        break;
    default:
        break;
    }
}

void em_movie_field_decode(EmMovieField *m)
{
    /* The decoder thread (sub_sceMpegGetPicture_decode_error): it tests
     * the skip state at the top of each picture, then waits for a free
     * slot and decodes; the last `held` pictures come out only after the
     * feed loop has ended (the bytes 00206BF0 pushes). */
    if (m->phase == EM_MOVIE_FIELD_WAIT_SYNC2 || m->decoder_stopped) return;
    while (m->ring_count < EM_MOVIE_FIELD_RING) {
        if (m->decoded >= m->cfg.pictures - m->cfg.held && m->feeding) return;
        if (m->ring_count == 0) m->head_state = 2;
        m->ring_count++;
        m->decoded++;
        if (m->decoded == m->cfg.pictures || m->skip) {
            m->decoder_stopped = 1;
            return;
        }
    }
}

int em_movie_field_skip_ready(const EmMovieField *m)
{
    return m->phase == EM_MOVIE_FIELD_PLAY && m->feeding && !m->skip &&
           m->decoded >= EM_MOVIE_FIELD_SKIP_COUNT;
}

int em_movie_field_main(EmMovieField *m, int skip)
{
    switch (m->phase) {
    case EM_MOVIE_FIELD_PLAY:
        break;
    case EM_MOVIE_FIELD_DONE:
        return 0;
    default:
        return 1;                                 /* a 001009C8 wait */
    }
    if (m->feeding) {
        /* 002036E0's pass: the pad's skip, 00206B70. */
        if (skip && em_movie_field_skip_ready(m)) m->skip = 1;
        if (!m->gate) {
            /* The ring is full and the sound's first 0x6000 bytes are on
             * the IOP (host speed): 002061B0(1) waits from the next vblank. */
            if (m->ring_count == EM_MOVIE_FIELD_RING) m->phase = EM_MOVIE_FIELD_WAIT_GATE;
            return 1;
        }
        if (m->vblank - m->gate_vblank >= m->cfg.feed_end) {
            /* The byte budget is spent: the loop ends, 00206BF0 pushes the
             * stream's last bytes and the decoder outputs the held pictures. */
            m->feeding = 0;
            em_movie_field_decode(m);
        }
    }
    /* 00206D10: the decoder done and the ring empty set state 3, which
     * ends the feed loop or the drain loop in this period; 00206200 closes
     * the gate and 00206320 queues the sound's stop. */
    if (m->decoder_stopped && m->ring_count == 0) {
        m->ended = 1;
        m->end_vblank = m->vblank;
        m->feeding = 0;
        m->gate = 0;
        if (m->sound == 1 || m->sound == 3) m->sound = 4;
        m->phase = EM_MOVIE_FIELD_WAIT_SYNC3;
    }
    return 1;
}
