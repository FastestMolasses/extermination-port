/* em_examine.c — the use-scan EXAMINE interaction (decode ledger +
 * flagged deviations in em_examine.h).
 *
 * Engine sources, all static .s reads (2026-06-11 s66). AUDIT NOTE
 * (2026-07): every address in this list except func_001BA1A0 /
 * func_001BA1F0 is AREA OVERLAY code (0x823500+), which the
 * decompilation has NOT recovered — so the behavior shape, the op
 * numbering and the presentation metrics below are one session's
 * disassembly reading, NOT source-derived. Only the two interpreter
 * entry points, plus func_00183EF0 / func_00184BA0 for the scan, can be
 * re-checked against recovered C. See the PROVENANCE block at the top
 * of em_examine.h.
 *   - the behavior shape: AREA02 overlay 0x824FA0 (the office examine —
 *     archetype 3, desc D_002758E0 {20,10}, script prime/pump
 *     func_001BA1A0/func_001BA1F0, re-arm on completion), AREA11
 *     0x827B10 (the switch refusal — D_00810841 unlock-bit gate,
 *     sound-delay counter), AREA06 0x824340, AREA07 0x823DA0.
 *   - the script shape: 0x40-byte records, op07 sub2 enter / op00
 *     camera cue / op02 wait / op0C message (line word + pre-delay) /
 *     op0D sub5 chase cue / op07 sub4|STOP exit.
 *   - the message: the mode-2 machine (em_hud.c radio presenter) —
 *     GLOBAL lines ride em_hud_radio; AREA-bank chains (the per-area
 *     line-record tables D_00264DD0[area+1], 8-byte records
 *     {u16 dur, s16 voice_cue, u8 flag, u8 wait}) are presented here
 *     with the same OBSERVED presentation (tall gray, centered, y 388 —
 *     screen-space metrics read off a capture, not from recovered C).
 */
#include "game/em_examine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"        /* EM_PAD_CROSS — the use-button mask */
#include "game/em_hud.h"     /* em_hud_radio (GLOBAL lines) + text draw */
#include "game/em_game.h"    /* contract-A: terminal power + elevator */
#include "game/em_pickup.h"  /* the single-winner use-scan arbitration */

#define EX_PI 3.14159265358979f

/* FACE pre-roll phase id (ExSeq.phase): runs BEFORE pre-delay (phase 0).
 * Distinct value so the existing 0/1/2 message/terminal switch is
 * untouched. */
#define EX_PHASE_FACE 3

/* Watchdog cap on the FACE pivot. At the standing turn-in-place rate
 * (22.5 deg/frame) a full +-180 deg pivot completes in <= 8 frames; this
 * cap (a generous margin) guarantees the pivot can never soft-lock the
 * sequence if the target is somehow unreachable. The pivot normally
 * SNAPS well within it. */
#define EX_FACE_MAX_FRAMES 16

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
    int   cooldown;                     /* optional legacy manifest delay */
    int   has_cam;
    float cam[3];                       /* op00 camera-cue eye */
    int   has_face;                     /* op04 FACE pre-roll present:
                                         * the scripted heading-target was
                                         * exported (manifest `face <yaw>`).
                                         * Without it the sequence runs as
                                         * before (no pivot). */
    float face_yaw;                     /* op04 scripted target YAW
                                         * (record+0x24 — snow internal
                                         * terminal = -1.3037 rad). The
                                         * player pivots to this at the
                                         * standing turn-in-place rate
                                         * BEFORE the message. */
    int   face_walk;                    /* op01 walk-to duration in frames
                                         * (record+0x0C). 0 = no walk (the
                                         * snow terminals: the use-scan
                                         * already places the player within
                                         * dist 5). Reserved for nonzero-
                                         * duration examines elsewhere
                                         * (office/drawbridge) — currently
                                         * always 0 for the shipped scenes,
                                         * the straight-line position LERP
                                         * is FLAGGED-unimplemented. */
    int   n_rec;
    ExRec rec[EM_EXAMINE_RECS];
    int   cool_left;                    /* live cooldown counter */
    int   is_terminal;                  /* AREA-11 INTERNAL elevator
                                         * control terminal: the
                                         * power-gated RIDE (record 19,
                                         * ov 0x00827B10, on the platform).
                                         * Only CHECKS power; runs anim
                                         * 0x47 + the descent. */
} Examine;

/* The running sequence (one at a time — the engine's single script
 * interpreter block per behavior; the scan can't arm a second object
 * while scripted mode pauses input). */
typedef struct {
    int   slot;        /* -1 = idle */
    int   phase;       /* 3 FACE pre-roll, 0 pre-delay, 1 message,
                        * 2 terminal frame */
    int   face_left;   /* FACE phase: frames left before the max-frame cap
                        * forces the pivot to complete (a watchdog so a bad
                        * target can never soft-lock the sequence) */
    int   wait;        /* frames left in the current phase step */
    int   rec;         /* chain record cursor */
    int   in_gap;      /* chain: presenting the blank gap record */
    int   gstarted;    /* gline handed to em_hud_radio */
    int   no_message;  /* terminal powered/insert run: suppress the
                        * refusal line (the engine's powered script
                        * 0x82A750 is a different, line-less script) */
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

int em_examine_set_terminal(int slot)
{
    if (slot < 0 || slot >= s.n) return -1;
    s.e[slot].is_terminal = 1;
    return 0;
}

int em_examine_set_face(int slot, float face_yaw, int walk_frames)
{
    if (slot < 0 || slot >= s.n) return -1;
    Examine *e = &s.e[slot];
    e->has_face  = 1;
    e->face_yaw  = face_yaw;
    e->face_walk = walk_frames > 0 ? walk_frames : 0;
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

/* The use scan (func_00184BA0 walk + func_00183EF0 archetype geometry):
 * CROSS edge, XZ disc, asymmetric dy window, pi/2 facing with the 7-u
 * auto pass; nearest passer wins.
 *
 * CORRECTED (audit): the facing half-angle is pi/2, not pi/4. Read in
 * decomp Extermination/src/func_00183EF0.c, `case 3: case 4:` — the
 * archetype-3 branch ends in `if (fabs(ang) <= 1.5707964f) { ... return
 * 1; } return 0;` and RETURNS there; the pi/4 test (0.7853982f) at the
 * tail of that function is only reached by archetypes 0/1/2, which
 * `break` out of the switch. The pi/4 here was inherited from the DOOR
 * use-scan (FINDINGS "side test within pi/2, then a pi/4 facing"), a
 * different two-stage test. Consequence of the old value: examines
 * silently refused to arm for approach angles between 45 and 90 degrees
 * that the engine accepts. */
static int examine_scan(const float pp[3], float pyaw, float *out_dist)
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
            /* func_00183EF0 case 3/4: bd > 7 -> ang = wrap(player_yaw -
             * atan2(bx, bz)); accepted while |ang| <= 1.5707964f. */
            float fd = ex_norm_ang(atan2f(dx, dz) - pyaw);
            if (fabsf(fd) > EM_EXAMINE_FACE_HALF) continue;
        }
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    /* the engine compares the PLANAR distance (spad 0x70003B98), not the
     * square — hand it back for the cross-module single-winner rule */
    if (out_dist) *out_dist = best >= 0 ? sqrtf(best_d2) : 0.0f;
    return best;
}

static void seq_start(int slot)
{
    Examine *e = &s.e[slot];
    s.seq.slot       = slot;
    s.seq.phase      = 0;
    s.seq.wait       = e->delay;        /* op0C pre-delay / op02 wait */
    s.seq.rec        = 0;
    s.seq.in_gap     = 0;
    s.seq.gstarted   = 0;
    s.seq.no_message = 0;
    s.seq.face_left  = 0;

    /* AREA-11 INTERNAL elevator control terminal (record 19, ov
     * 0x00827B10, on the platform): the CORRECTED two-terminal flow
     * (INVESTIGATION_area11_elevator.md "CORRECTED FLOW"). This terminal
     * NO LONGER inserts the battery or sets power — it ONLY checks power
     * and runs the ride. In the original the power bit D_00810841[11]
     * bit 7 is set by the panel program's callback 001580C0 (panel
     * 00159210), bound only in the not-yet-live interaction host (WP-4).
     * The former "battery_terminal" insert path was removed: it was
     * attributed to 008237E0, which is Roger's controller (story byte
     * D_008107D8 dispatch), with no battery or power behavior. */
    if (e->is_terminal) {
        if (em_game_terminal_powered()) {
            /* POWERED path — the engine's script 0x82A750: play the
             * lever-throw (anim 0x47) ON THE PLAYER with input/movement
             * locked for its duration, THEN opcode-9 INSTALL + run the
             * elevator descent (ov 0x00828050). em_game_elevator_start
             * is idempotent (a repeat after the ride is a no-op), so
             * re-using a powered terminal just re-locks for the clip.
             * Suppress the refusal line — the powered script carries no
             * "no power" message. */
            em_game_player_interact_anim(0x47);
            em_game_elevator_start();
            s.seq.no_message = 1;
            printf("examine: slot %d — INTERNAL TERMINAL powered "
                   "(anim 0x47 + lock, elevator descending)\n", slot);
            return;
        }
        /* The legacy message adapter presents original global line1A.
         * Original owner+2A is a sound counter, not a refusal cooldown. */
        printf("examine: slot %d — INTERNAL TERMINAL UNPOWERED refusal "
               "(no power)\n", slot);
    }

    /* op04 FACE pre-roll (INVESTIGATION_examine_walk_face.md §3): on the
     * message/refusal path (the early-return powered clip path plays its
     * OWN facing clip and never reaches here), if the examine
     * ships a scripted face-yaw, pivot the player to it BEFORE the message.
     * op01 walk-to is duration-0 for the snow terminals (the use-scan
     * already places the player within dist 5), so the visible pre-roll
     * is purely the FACE turn — the player stands and rotates in place to
     * the scripted yaw at 22.5 deg/frame, then the line shows. A nonzero
     * walk-to LERP (face_walk) is FLAGGED-unimplemented (no shipped scene
     * sets it; office/drawbridge would). */
    if (e->has_face) {
        s.seq.phase     = EX_PHASE_FACE;
        s.seq.face_left = EX_FACE_MAX_FRAMES;
    }

    printf("examine: armed slot %d at (%.1f, %.1f, %.1f) — %s%s\n", slot,
           e->pos[0], e->pos[1], e->pos[2],
           e->gline >= 0 ? "global line" : "area chain",
           e->has_face ? " (+ FACE pre-roll)" : "");
}

static void seq_finish(void)
{
    Examine *e = &s.e[s.seq.slot];
    /* Optional legacy manifest delay. AREA11's owner+2A is a sound
     * counter and must not be exported as a cooldown here. */
    e->cool_left = s.seq.no_message ? 0 : e->cooldown;
    printf("examine: slot %d done (re-arm%s)\n", s.seq.slot,
           e->cool_left ? " after cooldown" : "ed");
    s.seq.slot = -1;                    /* +0x0B = 0, sub-state 0 */
}

void em_examine_update(const float player_pos[3], float player_yaw,
                       const EmFrameInput *in, int scan)
{
    /* Legacy per-object re-arm delays, where explicitly requested. */
    for (int i = 0; i < s.n; i++)
        if (s.e[i].cool_left > 0) s.e[i].cool_left--;

    if (s.seq.slot < 0) {
        /* Do not arm a new examine while a scripted interact anim (the
         * insert/lever clip) or the elevator ride still owns the player
         * — the press that started the interaction must not also
         * double-trigger another examine on a subsequent frame
         * (contract: em_game_player_interact_busy). */
        if (scan && (in->pressed & EM_PAD_CROSS) &&
            !em_game_player_interact_busy()) {
            float hit_d = 0.0f;
            int   hit   = examine_scan(player_pos, player_yaw, &hit_d);
            if (hit >= 0) {
                /* ONE WINNER PER PRESS (CORRECTED, audit 2026-07-31).
                 * func_00184BA0 (recovered C) walks ONE interactive list
                 * that holds items AND examine objects, keeps the single
                 * smallest planar distance (`if (v < best) { best = v;
                 * winner = obj; }`) and arms only that object
                 * (`winner[0xB] = 4; return 1`). The port scans the two
                 * kinds in separate modules, so a press near both used to
                 * take the item AND start the examine script in the same
                 * frame. em_pickup_update has already run this frame:
                 * yield to its winner when it is nearer, otherwise take
                 * the press back off it. (Ties go to the item — the
                 * engine's strict `<` resolves them by list order, which
                 * the port has no counterpart for.) */
                float item_d = 0.0f;
                if (em_pickup_scan_dist(&item_d) && item_d <= hit_d) {
                    printf("examine: slot %d lost the press to a nearer "
                           "item (%.2f <= %.2f)\n", hit, item_d, hit_d);
                } else {
                    em_pickup_scan_release();
                    seq_start(hit);
                }
            }
        }
        return;
    }

    Examine *e = &s.e[s.seq.slot];
    switch (s.seq.phase) {
    case EX_PHASE_FACE:                  /* op04 FACE pivot (pre-message) */
        /* Turn the locked player toward the scripted yaw at the standing
         * turn-in-place rate (em_game_player_face_step = 22.5 deg/frame,
         * SNAP-when-within). The player is held by em_examine_input_locked
         * (player_move suppresses free locomotion the whole script), so
         * this owns the heading. Advance to pre-delay once facing, or when
         * the watchdog cap forces it (a target that can never be reached
         * must not soft-lock the sequence). The op01 walk-to LERP is
         * duration-0 here (snow) and so skipped — FLAGGED for nonzero. */
        if (em_game_player_face_step(e->face_yaw) || --s.seq.face_left <= 0)
            s.seq.phase = 0;             /* facing -> the existing pre-delay */
        return;
    case 0:                              /* pre-delay (scripted mode on) */
        if (s.seq.wait-- > 0) return;
        s.seq.phase = 1;
        if (s.seq.no_message) {
            /* terminal powered/insert run: no line (the powered script
             * 0x82A750 carries none) — straight to the bookkeeping
             * frame; the camera cue + input lock still framed the cue */
            s.seq.phase = 2;
            s.seq.wait  = 1;
        } else if (e->gline >= 0) {
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
