/* em_examine.c — the use-scan EXAMINE interaction (decode ledger +
 * flagged deviations in em_examine.h).
 *
 * Engine sources, all static .s reads (2026-06-11 s66):
 *   - the behavior shape: AREA02 overlay 0x824FA0 (the office examine —
 *     archetype 3, desc D_002758E0 {20,10}, script prime/pump
 *     func_001BA1A0/func_001BA1F0, re-arm on completion), AREA11
 *     0x827B10 (the switch refusal — D_00810841 unlock-bit gate,
 *     300-frame cooldown), AREA06 0x824340, AREA07 0x823DA0.
 *   - the script shape: 0x40-byte records, op07 sub2 enter / op00
 *     camera cue / op02 wait / op0C message (line word + pre-delay) /
 *     op0D sub5 chase cue / op07 sub4|STOP exit.
 *   - the message: the mode-2 machine (em_hud.c radio presenter) —
 *     GLOBAL lines ride em_hud_radio; AREA-bank chains (the per-area
 *     line-record tables D_00264DD0[area+1], 8-byte records
 *     {u16 dur, s16 voice_cue, u8 flag, u8 wait}) are presented here
 *     with the same decoded presentation (tall gray, centered, y 388).
 */
#include "game/em_examine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"        /* EM_PAD_CROSS — the use-button mask */
#include "game/em_hud.h"     /* em_hud_radio (GLOBAL lines) + text draw */

#define EX_PI 3.14159265358979f

typedef struct {
    int   dur;                          /* text frames (record u16 dur) */
    int   gap;                          /* blank frames (the next —
                                         * empty — bank line's record) */
    char  text[EM_EXAMINE_TEXT_MAX];
} ExRec;

typedef struct {
    int   used;
    float pos[3];
    float yaw;                          /* placement yaw (unused by the
                                         * model-0 facing test; kept for
                                         * the manifest round-trip) */
    float dist, dy;                     /* use-scan desc */
    int   gline;                        /* GLOBAL bank line, -1 = chain */
    int   delay;                        /* op0C pre-delay frames */
    int   cooldown;                     /* re-arm cooldown (AREA11 +0x2A) */
    int   has_cam;
    float cam[3];                       /* op00 camera-cue eye */
    int   n_rec;
    ExRec rec[EM_EXAMINE_RECS];
    int   cool_left;                    /* live cooldown counter */
} Examine;

/* The running sequence (one at a time — the engine's single script
 * interpreter block per behavior; the scan can't arm a second object
 * while scripted mode pauses input). */
typedef struct {
    int   slot;        /* -1 = idle */
    int   phase;       /* 0 pre-delay, 1 message, 2 terminal frame */
    int   wait;        /* frames left in the current phase step */
    int   rec;         /* chain record cursor */
    int   in_gap;      /* chain: presenting the blank gap record */
    int   gstarted;    /* gline handed to em_hud_radio */
} ExSeq;

static struct {
    Examine e[EM_EXAMINE_MAX];
    int     n;
    ExSeq   seq;
} s = { .seq = { .slot = -1 } };

int em_examine_add(const float pos[3], float yaw, float dist, float dy,
                   int gline, int delay, int cooldown, const float cam[3])
{
    if (s.n >= EM_EXAMINE_MAX) {
        printf("examine: table full (%d)\n", EM_EXAMINE_MAX);
        return -1;
    }
    Examine *e = &s.e[s.n];
    memset(e, 0, sizeof *e);
    e->used  = 1;
    memcpy(e->pos, pos, sizeof e->pos);
    e->yaw   = yaw;
    e->dist  = dist > 0.0f ? dist : EM_EXAMINE_RADIUS;
    e->dy    = dy > 0.0f ? dy : EM_EXAMINE_DY;
    e->gline = gline;
    e->delay = delay;
    e->cooldown = cooldown;
    if (cam) {
        e->has_cam = 1;
        memcpy(e->cam, cam, sizeof e->cam);
    }
    return s.n++;
}

int em_examine_text(int slot, int dur, int gap, const char *text)
{
    if (slot < 0 || slot >= s.n || !text) return -1;
    Examine *e = &s.e[slot];
    if (e->n_rec >= EM_EXAMINE_RECS) {
        printf("examine: chain full on slot %d\n", slot);
        return -1;
    }
    ExRec *r = &e->rec[e->n_rec++];
    r->dur = dur > 0 ? dur : 148;       /* the table's common duration */
    r->gap = gap > 0 ? gap : 0;
    /* decode the manifest's two-char "\n" escapes */
    size_t o = 0;
    for (const char *p = text; *p && o + 1 < sizeof r->text; p++) {
        if (p[0] == '\\' && p[1] == 'n') {
            r->text[o++] = '\n';
            p++;
        } else {
            r->text[o++] = *p;
        }
    }
    r->text[o] = '\0';
    return 0;
}

void em_examine_reset(void)
{
    memset(&s, 0, sizeof s);
    s.seq.slot = -1;
}

int em_examine_count(void) { return s.n; }
int em_examine_active(void) { return s.seq.slot; }

int em_examine_pos(int i, float out_pos[3])
{
    if (i < 0 || i >= s.n) return 0;
    memcpy(out_pos, s.e[i].pos, 3 * sizeof(float));
    return 1;
}

int em_examine_input_locked(void)
{
    return s.seq.slot >= 0;
}

int em_examine_camera(float out_eye[3], float out_tgt[3])
{
    if (s.seq.slot < 0) return 0;
    const Examine *e = &s.e[s.seq.slot];
    if (!e->has_cam) return 0;
    memcpy(out_eye, e->cam, 3 * sizeof(float));
    /* the cue frames the examined object (the op00 second vector's
     * spline semantics are undecoded — em_examine.h FLAG): target =
     * the placement, slightly up */
    out_tgt[0] = e->pos[0];
    out_tgt[1] = e->pos[1] + 1.0f;
    out_tgt[2] = e->pos[2];
    return 1;
}

/* func_001B1470 — wrap to (-pi, pi]. */
static float ex_norm_ang(float a)
{
    while (a >  EX_PI) a -= 2.0f * EX_PI;
    while (a < -EX_PI) a += 2.0f * EX_PI;
    return a;
}

/* The use scan (func_00184BA0 walk + func_00183EF0 archetype geometry,
 * em_pickup.c shape): CROSS edge, XZ ring, dy window, pi/4 facing with
 * the 7-u auto pass; nearest passer wins. */
static int examine_scan(const float pp[3], float pyaw)
{
    int   best = -1;
    float best_d2 = 1e30f;
    for (int i = 0; i < s.n; i++) {
        Examine *e = &s.e[i];
        if (!e->used || e->cool_left > 0) continue;
        float dx = e->pos[0] - pp[0];
        float dz = e->pos[2] - pp[2];
        float d2 = dx * dx + dz * dz;
        if (d2 > e->dist * e->dist) continue;
        float dy = pp[1] - e->pos[1];               /* player above: + */
        if (dy >= 0.0f ? dy > e->dy
                       : -dy > e->dy + EM_EXAMINE_DY_EXTRA) continue;
        if (d2 > EM_EXAMINE_AUTO_RING * EM_EXAMINE_AUTO_RING) {
            float fd = ex_norm_ang(atan2f(dx, dz) - pyaw);
            if (fabsf(fd) > EX_PI * 0.25f) continue;
        }
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

static void seq_start(int slot)
{
    Examine *e = &s.e[slot];
    s.seq.slot     = slot;
    s.seq.phase    = 0;
    s.seq.wait     = e->delay;          /* op0C pre-delay / op02 wait */
    s.seq.rec      = 0;
    s.seq.in_gap   = 0;
    s.seq.gstarted = 0;
    printf("examine: armed slot %d at (%.1f, %.1f, %.1f) — %s\n", slot,
           e->pos[0], e->pos[1], e->pos[2],
           e->gline >= 0 ? "global line" : "area chain");
}

static void seq_finish(void)
{
    Examine *e = &s.e[s.seq.slot];
    e->cool_left = e->cooldown;         /* AREA11 +0x2A (0 elsewhere) */
    printf("examine: slot %d done (re-arm%s)\n", s.seq.slot,
           e->cooldown ? " after cooldown" : "ed");
    s.seq.slot = -1;                    /* +0x0B = 0, sub-state 0 */
}

void em_examine_update(const float player_pos[3], float player_yaw,
                       const EmFrameInput *in, int scan)
{
    /* cooldowns tick regardless (the engine's +0x2A countdown runs in
     * the behavior's per-frame sub-state) */
    for (int i = 0; i < s.n; i++)
        if (s.e[i].cool_left > 0) s.e[i].cool_left--;

    if (s.seq.slot < 0) {
        if (scan && (in->pressed & EM_PAD_CROSS)) {
            int hit = examine_scan(player_pos, player_yaw);
            if (hit >= 0) seq_start(hit);
        }
        return;
    }

    Examine *e = &s.e[s.seq.slot];
    switch (s.seq.phase) {
    case 0:                              /* pre-delay (scripted mode on) */
        if (s.seq.wait-- > 0) return;
        s.seq.phase = 1;
        if (e->gline >= 0) {
            em_hud_radio(e->gline);      /* the mode-2 machine owns it */
            s.seq.gstarted = 1;
        } else if (e->n_rec > 0) {
            s.seq.rec    = 0;
            s.seq.in_gap = 0;
            s.seq.wait   = e->rec[0].dur;
        } else {
            s.seq.phase = 2;             /* no message — terminal only */
            s.seq.wait  = 1;
        }
        return;
    case 1:
        if (e->gline >= 0) {
            /* em_hud's machine runs the timer + the terminal frame */
            if (s.seq.gstarted && !em_hud_radio_active())
                seq_finish();
            return;
        }
        if (--s.seq.wait > 0) return;
        if (!s.seq.in_gap && e->rec[s.seq.rec].gap > 0) {
            s.seq.in_gap = 1;            /* the empty bank line's record */
            s.seq.wait   = e->rec[s.seq.rec].gap;
            return;
        }
        s.seq.in_gap = 0;
        if (++s.seq.rec < e->n_rec) {
            s.seq.wait = e->rec[s.seq.rec].dur;
            return;
        }
        s.seq.phase = 2;                 /* terminal record {0, wait 1} */
        s.seq.wait  = 1;
        return;
    case 2:                              /* one bookkeeping frame */
        if (--s.seq.wait <= 0) seq_finish();
        return;
    }
}

/* The mode-2 presentation for AREA-bank chain text (func_001FD950 —
 * the same constants as em_hud's radio presenter: tall gray, x = 256 -
 * max(first two '\n' segments)/2 on the 512 canvas, y 388, 24-px line
 * steps). GLOBAL lines draw inside em_hud and need nothing here. */
void em_examine_render(EmGfx *gfx)
{
    if (s.seq.slot < 0 || s.seq.phase != 1 || s.seq.in_gap || !gfx)
        return;
    const Examine *e = &s.e[s.seq.slot];
    if (e->gline >= 0 || s.seq.rec >= e->n_rec) return;
    const char *str = e->rec[s.seq.rec].text;
    if (!str[0] || !em_hud_font_ready() || em_hud_visible()) return;

    float wmax = 0.0f;
    const char *p = str;
    for (int i = 0; i < 2; i++) {
        char   seg[EM_EXAMINE_TEXT_MAX];
        size_t n = 0;
        while (p[n] && p[n] != '\n' && n < sizeof seg - 1) {
            seg[n] = p[n];
            n++;
        }
        seg[n] = '\0';
        float w = em_hud_text_width(seg, EM_HUD_TEXT_TALL_GRAY);
        if (w > wmax) wmax = w;
        while (p[n] && p[n] != '\n') n++;
        if (!p[n]) break;
        p += n + 1;
    }

    em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);
    float x = 256.0f - wmax * 0.5f;
    float y = 388.0f;
    p = str;
    while (*p) {
        char   seg[EM_EXAMINE_TEXT_MAX];
        size_t n = 0;
        while (p[n] && p[n] != '\n' && n < sizeof seg - 1) {
            seg[n] = p[n];
            n++;
        }
        seg[n] = '\0';
        em_hud_text(gfx, x, y, seg, EM_HUD_TEXT_TALL_GRAY);
        y += 24.0f;                      /* the engine's '\n' advance */
        while (p[n] && p[n] != '\n') n++;
        if (!p[n]) break;
        p += n + 1;
    }
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
}
