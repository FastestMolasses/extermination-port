/* em_weapon.c — SPR4 firing loop (see em_weapon.h for the engine mapping).
 *
 * Module-level singleton like em_input/em_door: the original game has one
 * player and one gun actor (player +0x20 / D_008102D0), all weapon state
 * is global/static on the PS2 too (the s18 inventory block) — no heap.
 */
#include "game/em_weapon.h"

#include <math.h>
#include <string.h>

#include "em_input.h"

/* --- Engine constants (FINDINGS "WEAPON SYSTEM") ----------------------- */
#define WPN_MAG_MAX     30      /* func_0017B300: mag = min(30, reserve)   */
#define WPN_RANGE       260.0f  /* func_001861C0 untargeted endpoint, and
                                 * the acquisition max distance            */
#define WPN_INTERVAL    12      /* +0x2F4 fire interval (12.0 default)     */
#define WPN_COUNT_STEP  2       /* +0x276 fire counter gains 2/frame
                                 * -> one shot every 6 frames              */
#define WPN_BURST_LEN   3       /* burst family 20..23: +0x28 slti 3       */
#define WPN_BURST_PAUSE 8       /* contract: burst pause = 8 ticks         */
#define WPN_RAY_MASK    0x7u    /* func_001861C0: set mask 7 (hulls +
                                 * cells + grid)                           */
#define WPN_RAY_ID      0x20    /* func_001861C0: query id 0x20            */

/* --- Port placeholders (flagged; engine values are anim-clip lengths
 *     that are not exported yet) ---------------------------------------- */
#define WPN_DRAW_FRAMES    15   /* anim 0x110 length stand-in (0.25 s)     */
#define WPN_HOLSTER_FRAMES 15   /* anim 0x111 length stand-in              */
#define WPN_RELOAD_FRAMES  40   /* anim 0x33 length stand-in (0.67 s)      */
#define WPN_MUZZLE_HEIGHT  12.0f /* chest height; the engine derives the
                                  * muzzle from the hand-bone matrix
                                  * (gun +0xA0 = M(0x810550)*(-3,y,0)) —
                                  * no hand bone wired natively yet        */
#define WPN_FLASH_FRAMES   3    /* muzzle-flash overlay lifetime           */
#define WPN_PULSE_HIT      6    /* crosshair pulse frames on a ray hit     */
#define WPN_PULSE_MISS     3    /* ... and on a miss                       */

static struct {
    uint8_t state;       /* EM_WPN_* (player major-state byte +0x06)      */
    uint8_t fire_mode;   /* D_00810C61: 0 semi / 1 burst-3 / 2 full-auto  */
    uint8_t mag;         /* D_00810C62                                    */
    int16_t reserve;     /* D_00810CB4 — TOTAL pool including the mag     */

    int     timer;       /* DRAW/RELOAD/HOLSTER frames remaining          */
    int     counter;     /* +0x276 fire counter (+2/frame, shot >= 12)    */
    int     pending;     /* +0x2A queued-shot flag (semi press latched
                          * while the cadence counter is still cooling)   */
    int     burst;       /* +0x28 burst counter, shots left in the burst  */
    int     burst_pause; /* ticks until the next burst may start          */

    int     fire_event;  /* gun actor +0x2E: posted by the player SM,
                          * consumed (ray + FX) on the NEXT update — the
                          * contract's mandatory one-frame latency        */

    int     flash;       /* muzzle-flash overlay frames remaining         */
    int     pulse;       /* crosshair pulse frames remaining              */
    int     pulse_hit;   /* the pulse being shown is a hit (vs miss)      */
    int     last_hit;    /* last resolved shot: 1/0; -1 = none yet        */

    int     shots;       /* introspection: rounds fired since reset       */
    int     reloads;     /* introspection: reloads since reset            */
} w;

void em_weapon_reset(uint8_t mag, int16_t reserve)
{
    memset(&w, 0, sizeof w);
    w.state    = EM_WPN_HOLSTERED;
    w.mag      = mag;
    w.reserve  = reserve;
    w.last_hit = -1;
}

/* func_0017B300(_, mode) — MATCHED 100% in the decomp repo. mode 0 = only
 * if the mag is empty; mode 1 = unconditional; else top-up (only if
 * mag < 30 AND reserve > mag). All modes: mag = min(30, reserve), the
 * reserve is NOT subtracted (it is the total pool — each shot already
 * decremented both). Returns 0 = reloaded, 1 = nothing to do. */
static int weapon_reload(int mode)
{
    if (mode == 0) {
        if (w.mag != 0 || w.reserve <= 0) return 1;
    } else if (mode != 1) {
        if (w.mag >= WPN_MAG_MAX || w.reserve <= w.mag) return 1;
    }
    w.mag = (uint8_t)(w.reserve < WPN_MAG_MAX ? w.reserve : WPN_MAG_MAX);
    return 0;
}

/* One trigger-accepted SHOT attempt (the common per-shot block of the
 * fire sub-machine, states 0xB/0x15/0x1F). Dry mag -> auto-reload
 * func_0017B300(.,0); reserve also empty -> dry click (engine sound
 * 0x169 — no native SFX hookup yet). A live round: mag-- AND reserve--
 * (the TOTAL-pool consume path), post the fire event to the gun
 * (+0x2E = 1; engine sounds 0x164/0x165, anims 0x31/0x32/0x34/0x35). */
static void weapon_shot(void)
{
    if (w.mag == 0) {
        w.pending = 0;
        w.burst   = 0;
        if (weapon_reload(0) == 0) {
            /* anim 0x33 gates firing; the mag is already refilled */
            w.reloads++;
            w.state = EM_WPN_RELOAD;
            w.timer = WPN_RELOAD_FRAMES;
        }
        /* else: dry click 0x169 — nothing to fire from */
        return;
    }
    w.mag--;
    w.reserve--;          /* the engine's TOTAL-pool rule: BOTH, per shot */
    w.shots++;
    w.fire_event = 1;     /* gun +0x2E — resolved next update             */
    w.counter    = 0;
}

/* The gun-side fire-event consumption — func_001861C0, the BULLET.
 * Hitscan: one segment query, muzzle -> muzzle + dir*260 (no native
 * target acquisition yet, so always the untargeted endpoint). Tracer /
 * impact-marker / shell-eject / rumble are pending; the overlay flash +
 * crosshair pulse stand in (em_weapon.h "VISUAL FEEDBACK"). */
static void weapon_resolve_fire(const EmCollision *coll,
                                const float pos[3], float yaw)
{
    float muzzle[3] = { pos[0], pos[1] + WPN_MUZZLE_HEIGHT, pos[2] };
    float dir[3]    = { sinf(yaw), 0.0f, cosf(yaw) };
    float end[3]    = { muzzle[0] + dir[0] * WPN_RANGE,
                        muzzle[1] + dir[1] * WPN_RANGE,
                        muzzle[2] + dir[2] * WPN_RANGE };
    int hit = 0;
    if (coll && coll->poly_count) {
        EmCollHit h;
        hit = em_collision_segment_query(coll, muzzle, end, WPN_RAY_MASK,
                                         WPN_RAY_ID, &h) != 0;
    }
    w.last_hit  = hit;
    w.flash     = WPN_FLASH_FRAMES;
    w.pulse     = hit ? WPN_PULSE_HIT : WPN_PULSE_MISS;
    w.pulse_hit = hit;
}

/* The AIM/FIRE loop's trigger logic — the fire sub-machine families
 * selected by the fire-mode byte (semi 10/11, burst 20..23, auto 30..32).
 * All families share the cadence: counter += 2/frame, shot needs >= 12. */
static void weapon_fire_logic(const EmFrameInput *in)
{
    if (w.counter < WPN_INTERVAL) w.counter += WPN_COUNT_STEP;
    if (w.burst_pause > 0) w.burst_pause--;

    switch (w.fire_mode) {
        case EM_WPN_MODE_SEMI:
            if (in->pressed & EM_PAD_CROSS) w.pending = 1;
            if (w.pending && w.counter >= WPN_INTERVAL) {
                w.pending = 0;
                weapon_shot();
            }
            break;
        case EM_WPN_MODE_BURST:
            if ((in->pressed & EM_PAD_CROSS) && w.burst == 0 &&
                w.burst_pause == 0)
                w.burst = WPN_BURST_LEN;
            if (w.burst > 0 && w.counter >= WPN_INTERVAL) {
                w.burst--;
                if (w.burst == 0) w.burst_pause = WPN_BURST_PAUSE;
                weapon_shot();
            }
            break;
        case EM_WPN_MODE_AUTO:
            if ((in->held & EM_PAD_CROSS) && w.counter >= WPN_INTERVAL)
                weapon_shot();
            break;
        default:
            break;
    }

    /* Manual reload — engine: L3, func_0017B300(.,2) top-up; port key
     * SQUARE (em_weapon.h "KEY MAPPING" deviation note). */
    if (w.state == EM_WPN_AIM && (in->pressed & EM_PAD_SQUARE)) {
        if (weapon_reload(2) == 0) {
            w.reloads = w.reloads + 1;
            w.pending = 0;
            w.burst   = 0;
            w.state   = EM_WPN_RELOAD;
            w.timer   = WPN_RELOAD_FRAMES;
        }
    }
}

void em_weapon_update(const EmCollision *coll, const float player_pos[3],
                      float player_yaw, const EmFrameInput *in)
{
    if (!in || !player_pos) return;

    /* Gun-side tick FIRST: an event posted last frame resolves now —
     * the contract's one-frame fire-event latency, kept exactly. */
    if (w.fire_event) {
        w.fire_event = 0;
        weapon_resolve_fire(coll, player_pos, player_yaw);
    }
    if (w.flash > 0) w.flash--;
    if (w.pulse > 0) w.pulse--;

    const int draw_held = (in->held & EM_PAD_R1) != 0;

    switch (w.state) {
        case EM_WPN_HOLSTERED:
            if (draw_held) {
                /* major 0 ENTER: reload-if-empty, anim 0x110, sound
                 * 0x162. The ENTER reload is covered by the draw anim —
                 * no RELOAD state, but it counts as a reload. */
                if (weapon_reload(0) == 0) w.reloads++;
                w.state = EM_WPN_DRAW;
                w.timer = WPN_DRAW_FRAMES;
            }
            break;
        case EM_WPN_DRAW:
            if (!draw_held) {
                w.state = EM_WPN_HOLSTER;   /* 0x65: anim 0x111, 0x163 */
                w.timer = WPN_HOLSTER_FRAMES;
            } else if (--w.timer <= 0) {
                w.state   = EM_WPN_AIM;     /* major 2 */
                w.counter = WPN_INTERVAL;   /* first shot is immediate */
                w.pending = 0;
                w.burst   = 0;
            }
            break;
        case EM_WPN_AIM:
            if (!draw_held) {
                w.state = EM_WPN_HOLSTER;
                w.timer = WPN_HOLSTER_FRAMES;
                w.pending = 0;
                w.burst   = 0;
            } else {
                weapon_fire_logic(in);
            }
            break;
        case EM_WPN_RELOAD:
            /* anim 0x33 wait (major 3); the ammo move already happened */
            if (--w.timer <= 0) {
                w.state   = EM_WPN_AIM;
                w.counter = WPN_INTERVAL;
            }
            if (!draw_held) {               /* stance drop mid-reload */
                w.state = EM_WPN_HOLSTER;
                w.timer = WPN_HOLSTER_FRAMES;
            }
            break;
        case EM_WPN_HOLSTER:
            if (--w.timer <= 0)
                w.state = EM_WPN_HOLSTERED;
            break;
        default:
            w.state = EM_WPN_HOLSTERED;
            break;
    }
}

/* --- Placeholder overlay feedback (em_weapon.h "VISUAL FEEDBACK") ------ */

static const float kCrosshair[4]  = { 1.0f, 1.0f, 1.0f, 0.45f };
static const float kPulseHit[4]   = { 1.0f, 1.0f, 1.0f, 0.90f };
static const float kPulseMiss[4]  = { 0.8f, 0.8f, 0.8f, 0.35f };
static const float kFlashOuter[4] = { 1.0f, 0.75f, 0.30f, 0.55f };
static const float kFlashInner[4] = { 1.0f, 1.00f, 0.85f, 0.85f };

static void rect_centered(EmGfx *gfx, float cx, float cy, float w_, float h_,
                          const float rgba[4])
{
    em_gfx_overlay_rect(gfx, cx - w_ * 0.5f, cy - h_ * 0.5f, w_, h_, rgba);
}

void em_weapon_render(EmGfx *gfx)
{
    if (!gfx || w.state == EM_WPN_HOLSTERED) return;

    const float cx = EM_GFX_OVERLAY_W * 0.5f;   /* 320 */
    const float cy = EM_GFX_OVERLAY_H * 0.5f;   /* 224 */

    /* Crosshair while in the armed stance (aim/reload). */
    if (w.state == EM_WPN_AIM || w.state == EM_WPN_RELOAD)
        rect_centered(gfx, cx, cy, 4.0f, 4.0f, kCrosshair);

    /* Hit-marker pulse: bright/large on a ray hit, dim/small on a miss. */
    if (w.pulse > 0)
        rect_centered(gfx, cx, cy,
                      w.pulse_hit ? 10.0f : 6.0f,
                      w.pulse_hit ? 10.0f : 6.0f,
                      w.pulse_hit ? kPulseHit : kPulseMiss);

    /* Muzzle flash: 3-frame two-layer flare at the lower center (where
     * the gun sits in the chase view; the real pass projects the gun
     * actor's muzzle point +0xA0 — pending). */
    if (w.flash > 0) {
        float s = (float)w.flash / (float)WPN_FLASH_FRAMES; /* 1 -> 1/3 */
        rect_centered(gfx, cx, cy + 76.0f, 28.0f * s, 20.0f * s,
                      kFlashOuter);
        rect_centered(gfx, cx, cy + 76.0f, 14.0f * s, 10.0f * s,
                      kFlashInner);
    }
}

/* --- Accessors ---------------------------------------------------------- */

uint8_t em_weapon_mag(void)      { return w.mag; }
int16_t em_weapon_reserve(void)  { return w.reserve; }
uint8_t em_weapon_fire_mode(void){ return w.fire_mode; }
int     em_weapon_state(void)    { return w.state; }
int     em_weapon_shots(void)    { return w.shots; }
int     em_weapon_reloads(void)  { return w.reloads; }
int     em_weapon_last_hit(void) { return w.last_hit; }

void em_weapon_set_fire_mode(uint8_t mode)
{
    if (mode <= EM_WPN_MODE_AUTO) w.fire_mode = mode;
}
