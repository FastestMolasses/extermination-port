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
#include "game/em_enemy.h"
#include "game/em_game.h"
#include "game/em_sfx.h"

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
#define WPN_HIT_DMG     5       /* func_001B41F0(victim, ..., 5, 0): the
                                 * bullet's damage written into the
                                 * victim's +0x36 mailbox (crawler HP 1
                                 * = one-shot kill)                        */
#define WPN_OVERSHOOT   5.0f    /* targeted rays overshoot the aim point
                                 * by 5 units (func_001861C0 step 1)       */
#define WPN_AIM_CONE    0.9848f /* cos ~10 deg — acquisition facing cone.
                                 * PORT STAND-IN for the engine's SCREEN-
                                 * space cone (func_00199220: |x|<=66+50s,
                                 * |y|<=45+45s on the GS canvas) until a
                                 * projection-space acquisition lands      */

/* --- LASER SIGHT (em_weapon.h header block; s23 disasm) ---------------- */
#define WPN_LASER_SEGS  32      /* func_001E2BA0: the beam is 32 GS LINE
                                 * segments muzzle -> endpoint             */
#define WPN_LASER_PHASE 0.025f  /* per-segment flicker-phase step factor:
                                 * f21 = (0.1 * len) / 4.0 radians         */
#define WPN_DOT_SIZE    3.0f    /* func_001854E0/760: endpoint dot sprite
                                 * 3.0 units (locked-on uses 5.0)          */
#define WPN_LASER_WIDTH 0.12f   /* PORT VALUE: the engine beam is a GS
                                 * LINE prim = 1 screen pixel at 512x448;
                                 * ~0.12 world units reads as ~1 px at the
                                 * aim camera's typical 25-35 u depth
                                 * (1 px ~= z / 240 at zoom s = 480)       */
/* func_00185760 beam base color: unlocked (0.7, 0, 0, 1). With a locked
 * target (D_008106E0 nonzero, aim option 1) the engine switches to
 * (1.0, 0.6, 0.2, 1) and a 5.0-unit warm dot — pending native lock-on. */
static const float kLaserColor[4] = { 0.7f, 0.0f, 0.0f, 1.0f };

/* --- PLAYER ANIMS (wired 2026-06-10 s24 — FINDINGS "ANIM ID MAPPING":
 *     the anim id IS the container index in the player clip library
 *     chunk28/f01_id3c, so these ids are the EMDL clip-table ids the
 *     re-exported player.emdl carries; requests go through em_game's
 *     anim mailbox, the native +0x1F2/+0x20C commit path) ------------- */
#define WPN_ANIM_DRAW    0x110  /* draw, 25 fr — property-table rate 1.4 */
#define WPN_ANIM_HOLSTER 0x111  /* holster, 25 fr, rate 1.0              */
#define WPN_ANIM_RELOAD  0x33   /* reload, 57 fr, rate 1.0               */
#define WPN_ANIM_AIM     0x112  /* SPR4 sub-0 aim-pose ladder BASE (the
                                 * level-pitch step of D_00248B70[0] ->
                                 * 0x112..0x11A); HELD while in the AIM
                                 * state via em_game_anim_hold — the
                                 * pitch-step blend (+0x278) is not
                                 * translated yet                        */
#define WPN_DRAW_RATE    1.4f   /* D_00248C90[0x110].rate_scale          */
/* FIRE RECOIL (decoded s25 — FINDINGS "FIRE ANIM MECHANISM"; corrects
 * the s23 guess that 0x31/0x32/0x34/0x35 name fire CLIPS): the engine
 * has NO separate fire clip. Those four values are the armed-stance
 * ACTION CODES at player +0x1F0 (stance +0x05 0x1D->0x31, 0x1E->0x32,
 * 0x1F->0x34, 0x20->0x35 — set by func_0016F600 on stance entry, NOT
 * by the semi/burst/auto fire families, which only READ the code to
 * pick the fire sound: 0x164 for 0x31/0x34, 0x165 for 0x32/0x35).
 * Containers 49/50/52/53 in the clip library are unrelated neighbors
 * (49/50 are 110/79-frame root-travel locomotion clips — verified, not
 * recoil snaps). The recoil: while the code is 0x31/0x34 the per-bone
 * publisher (bone_matrix_publish -> anim_clip_arbiter, f13 = (float)
 * lh(+0x276)) re-seeds the COMMITTED aim-ladder clip every frame with
 * sample time = the fire counter — 0 at every shot, +2/frame. The
 * snap is baked into the FRONT frames of the aim-pose clip (0x112:
 * 4.0 deg/frame over frames 0..4 decaying to a 0.9 deg/frame static
 * tail), so each shot replays the clip from frame 0 at 2 frames/tick
 * and settles back into the clamped hold. The property-table rate
 * (D_00248C90[0x112].rate_scale = 1.0, extracted) does not drive this
 * path — the counter step does. */
#define WPN_FIRE_RATE    2.0f   /* +0x276 counter step: the recoil
                                 * playhead gains 2 clip-frames/tick    */

/* --- Fallback state windows (flagged; used ONLY when the loaded player
 *     EMDL lacks the clip — anim_ticks() prefers the honest clip
 *     length from em_game_anim_frames) --------------------------------- */
#define WPN_DRAW_FRAMES    15   /* anim 0x110 length stand-in (0.25 s)     */
#define WPN_HOLSTER_FRAMES 15   /* anim 0x111 length stand-in              */
#define WPN_RELOAD_FRAMES  40   /* anim 0x33 length stand-in (0.67 s)      */
#define WPN_RELOAD_MAG_TICK 30  /* MAG-ACTION sound 0x168 offset into the
                                 * reload window: ~0.5 s after the reload
                                 * start (s29 live capture) = 30 ticks at
                                 * 60 Hz; clamped inside a shorter window  */
/* FIRE-CHAIN TAIL (s29 live capture — scheduled like the reload's 0x168):
 * the wall impact 0x189 lands ~2 frames after the fire sound. The fire
 * event already spends one frame in the gun mailbox (shot at T, ray
 * resolved at T+1), so the resolve arms ONE more tick and the sound
 * plays at T+2 — the engine's observed latency by construction. */
#define WPN_IMPACT_SFX_TICKS 1  /* resolve(T+1) + 1 tick -> 0x189 at T+2  */
#define WPN_CASING_TICKS    42  /* shell casing 0x16A hits the floor
                                 * ~0.7 s = 42 ticks after EACH shot      */
#define WPN_CASING_SLOTS     8  /* pending casings: the 6-tick full-auto
                                 * cadence keeps ceil(42/6) = 7 in flight */
#define WPN_MUZZLE_HEIGHT  12.0f /* chest height; the engine derives the
                                  * muzzle from the hand-bone matrix
                                  * (gun +0xA0 = M(0x810550)*(-3,y,0)) —
                                  * no hand bone wired natively yet        */
#define WPN_FLASH_FRAMES   3    /* muzzle-flash overlay lifetime           */
#define WPN_PULSE_HIT      6    /* crosshair pulse frames on a ray hit     */
#define WPN_PULSE_MISS     3    /* ... and on a miss                       */

/* --- KNIFE / MELEE constants (em_weapon.h "KNIFE / MELEE"; decoded
 *     2026-06-10 s36 — FINDINGS "KNIFE/MELEE DECODED". All table values
 *     are the boot-ELF row idx 0; idx 1 is the alternate-context row
 *     (+0x236), untranslated) ------------------------------------------ */
#define MELEE_ANIM_L1     0x10B  /* light hit 1, 50 fr (D_00248690[0][0]) */
#define MELEE_ANIM_L2     0x10C  /* light hit 2, 25 fr (D_00248690[0][1]) */
#define MELEE_ANIM_L3     0x10D  /* light hit 3, 20 fr (D_00248690[0][2]) */
#define MELEE_ANIM_HEAVY  0x10E  /* heavy stab, 20 fr  (D_002754A8[0])    */
#define MELEE_ANIM_RECOV  0x10F  /* hit-confirm recover, 25 fr (state
                                  * 0x51 idx-0 arm; blend 4.0)            */
/* Impact gates T (the +0x3C c.le.s thresholds; impact_tick =
 * max(3, len - T) — the down-count reading, see the header's TIMING
 * NOTE; 3 covers the request->commit mailbox latency + blend-in). */
#define MELEE_GATE_L1     24.0f  /* D_002486A0[0]                         */
#define MELEE_GATE_L2     26.0f  /* D_002486A8[0]                         */
#define MELEE_GATE_L3     41.0f  /* D_002486B0[0]                         */
#define MELEE_GATE_HEAVY  43.0f  /* D_00248700[0]                         */
/* Chain windows: the buffered (+0x2E) next hit starts when the clip
 * time passes len - 19 (D_002486D0/D4 idx 0, both 19.0). */
#define MELEE_CHAIN_TAIL  19.0f
#define MELEE_DMG_L1      3      /* func_001735C0 state-1 +0x36 write     */
#define MELEE_DMG_L2      3      /* state-2 write                         */
#define MELEE_DMG_L3      5      /* state-3 write                         */
#define MELEE_DMG_HEAVY   15     /* func_00173E60 (0xF)                   */
#define MELEE_RECOV_PAUSE 4      /* engine state 0x50: +0x28 = 4 ticks    */
#define MELEE_RECOV_RATE  1.0f   /* property-table rate (1.0; the 4.0 the
                                  * engine passes the arbiter is BLEND)   */
/* Fallback clip lengths (flagged; used only when the player EMDL lacks
 * the clip — the values ARE the disc clip lengths, baked as stand-ins). */
#define MELEE_L1_FRAMES    50
#define MELEE_L2_FRAMES    25
#define MELEE_L3_FRAMES    20
#define MELEE_HEAVY_FRAMES 20
#define MELEE_RECOV_FRAMES 25
/* RANGE — PORT STAND-IN (the engine has no player-side knife range: the
 * damage goes to the melee-target link's +0x36 mailbox and the TARGET-
 * side polls do the range work, e.g. func_00219870's func_0019AA80
 * probe). 12.0 = the engine's documented hands-reach (the use-scan
 * dist^2 <= 144, func_0019A910 mode 6, s17); the cone is a port value. */
#define EM_MELEE_REACH    12.0f
#define MELEE_CONE        0.5f   /* cos 60 deg frontal arc                */

static struct {
    uint8_t state;       /* EM_WPN_* (player major-state byte +0x06)      */
    uint8_t fire_mode;   /* D_00810C61: 0 semi / 1 burst-3 / 2 full-auto  */
    uint8_t mag;         /* D_00810C62                                    */
    int16_t reserve;     /* D_00810CB4 — TOTAL pool including the mag     */

    int     timer;       /* DRAW/RELOAD/HOLSTER frames remaining          */
    int     mag_sfx;     /* RELOAD ticks until the mag-action sound 0x168
                          * (0 = none pending; cancelled by a stance drop) */
    int     counter;     /* +0x276 fire counter (+2/frame, shot >= 12)    */
    int     pending;     /* +0x2A queued-shot flag (semi press latched
                          * while the cadence counter is still cooling)   */
    int     burst;       /* +0x28 burst counter, shots left in the burst  */
    int     burst_pause; /* ticks until the next burst may start          */

    int     fire_event;  /* gun actor +0x2E: posted by the player SM,
                          * consumed (ray + FX) on the NEXT update — the
                          * contract's mandatory one-frame latency        */

    int     impact_sfx;  /* ticks until the wall-impact sound 0x189
                          * (0 = none pending; armed by a WALL ray hit)   */
    int     casing[WPN_CASING_SLOTS];
                         /* per-shot countdowns to the shell-casing
                          * sound 0x16A (0 = slot free); the casing is
                          * already in the air, so these keep ticking
                          * through reloads/holsters/stance drops        */

    int     flash;       /* muzzle-flash overlay frames remaining         */
    int     pulse;       /* crosshair pulse frames remaining              */
    int     pulse_hit;   /* the pulse being shown is a hit (vs miss)      */
    int     last_hit;    /* last resolved shot: 1/0; -1 = none yet        */

    int     shots;       /* introspection: rounds fired since reset       */
    int     reloads;     /* introspection: reloads since reset            */

    /* LASER SIGHT — the gun-side per-frame raycast result (the engine
     * stores the clipped endpoint + flags in the gun actor's +0x1F0
     * block; func_001854E0/760 refresh it every aim frame). */
    int      laser_on;     /* gun +0x210 "laser active" flag              */
    float    laser_a[3];   /* beam start = muzzle (gun +0x1F0 vec)        */
    float    laser_b[3];   /* clipped endpoint (gun +0x200 vec)           */
    uint32_t rng;          /* flicker LCG (engine: func_00122BB8 rand)    */
} w;

/* KNIFE / MELEE state (engine player modes 0x21/0x22 — see the header
 * block; the engine keeps this in the player struct, the port in the
 * same module-level singleton style as the rifle). */
static struct {
    int state;       /* EM_MELEE_* (the engine's +0x05 mode + +0x06
                      * major collapsed: IDLE / SWING / RECOVER)        */
    int heavy;       /* 1 = mode 0x22 (heavy stab), 0 = mode 0x21      */
    int combo;       /* light combo hit 1..3 (engine major +0x06)      */
    int tick;        /* ticks into the current swing / recover window  */
    int len;         /* current swing window, ticks (= clip frames)    */
    int impact;      /* impact tick (max(3, len - T))                  */
    int chain_at;    /* chain-window tick (len - 19); 0 = no chain     */
    int buffered;    /* +0x2E: FIRE pressed during the swing           */
    int confirm;     /* target +0x0A read-back: a victim was struck    */
    int recov_pause; /* engine 0x50/0x51 countdown before anim 0x10F   */
    int swings;      /* introspection: attacks started since reset     */
    int hits;        /* introspection: impact-tick victims since reset */
    int sub_toggle;  /* D_00810D3C mirror (SQUARE while armed, att. 0) */
} m;

/* The flicker random source — engine func_00122BB8 is the C-library
 * rand(); a freestanding LCG keeps the port deterministic per run. */
static uint32_t wpn_rand(void)
{
    w.rng = w.rng * 1103515245u + 12345u;
    return (w.rng >> 16) & 0x7FFF;
}

void em_weapon_reset(uint8_t mag, int16_t reserve)
{
    memset(&w, 0, sizeof w);
    memset(&m, 0, sizeof m);
    w.state    = EM_WPN_HOLSTERED;
    w.mag      = mag;
    w.reserve  = reserve;
    w.last_hit = -1;
}

/* HONEST state windows from the committed clip lengths: the scripted
 * clip plays at `rate` frames/tick and em_game's commit clears it when
 * the clip time passes (frames - 1) + 1, i.e. after ceil(frames / rate)
 * ticks — the state timer matches that exactly, so the state machine
 * and the visible anim end together (one frame of request->commit
 * latency aside, the engine's own mailbox latency). A player EMDL
 * without the clip falls back to the flagged stand-in window. */
static int anim_ticks(unsigned clip_id, float rate, int fallback)
{
    int fc = em_game_anim_frames(clip_id);
    if (fc <= 0) return fallback;
    return (int)ceilf((float)fc / rate);
}

int em_weapon_draw_ticks(void)
{
    return anim_ticks(WPN_ANIM_DRAW, WPN_DRAW_RATE, WPN_DRAW_FRAMES);
}

int em_weapon_reload_ticks(void)
{
    return anim_ticks(WPN_ANIM_RELOAD, 1.0f, WPN_RELOAD_FRAMES);
}

int em_weapon_holster_ticks(void)
{
    return anim_ticks(WPN_ANIM_HOLSTER, 1.0f, WPN_HOLSTER_FRAMES);
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

/* HOLSTER entry (engine major state 0x65): anim 0x111 + sound 0x163 —
 * every transition into the holster ramp goes through here, exactly the
 * single state-entry the engine plays the sound and anim on. */
static void weapon_enter_holster(void)
{
    em_sfx_play(EM_SFX_WPN_HANDLE);     /* 0x163, state-0x65 entry (the
                                         * shared weapon-handling foley) */
    em_game_anim_request(WPN_ANIM_HOLSTER, 1.0f);   /* anim 0x111 */
    w.state   = EM_WPN_HOLSTER;
    w.timer   = em_weapon_holster_ticks();
    w.mag_sfx = 0;      /* stance drop mid-reload: no mag action played */
}

/* RELOAD entry (engine major state 3): anim 0x33 gates firing for the
 * clip's length; the ammo move (func_0017B300) already happened at the
 * call site. Both entries (dry-mag auto-reload and the manual top-up)
 * go through here. */
static void weapon_enter_reload(void)
{
    /* RELOAD SOUNDS (live-pinned s29): 0x163 at the reload START (the
     * shared weapon-handling foley — same id the holster plays), then
     * 0x168 at the MAG ACTION ~0.5 s in (the distinctive reload sound),
     * scheduled below and ticked by the RELOAD state. */
    em_sfx_play(EM_SFX_WPN_HANDLE);
    em_game_anim_request(WPN_ANIM_RELOAD, 1.0f);    /* anim 0x33 */
    w.reloads++;
    w.pending = 0;
    w.burst   = 0;
    w.state   = EM_WPN_RELOAD;
    w.timer   = em_weapon_reload_ticks();
    w.mag_sfx = WPN_RELOAD_MAG_TICK < w.timer ? WPN_RELOAD_MAG_TICK
                                              : w.timer - 1;
    if (w.mag_sfx < 1) w.mag_sfx = 1;
}

/* AIM entry (engine major state 2, and the post-reload re-entry): HOLD
 * the aim pose — the per-sub-weapon stance-table clip (sub 0 -> 0x112,
 * D_00248B88[0]), which the engine's armed tops keep re-selecting every
 * frame through the arbiter. em_game_anim_hold clamps the clip at its
 * last frame and keeps it committed until the next request replaces it
 * (the holster/reload clips) — the native equivalent of that persistent
 * re-selection. */
static void weapon_enter_aim(void)
{
    /* The hold plays the clip's front settle once at the counter rate
     * (the engine zeroes +0x276 on stance entry, so the pose clip's
     * recoil/settle frames run at 2/tick there too) and clamps. */
    em_game_anim_hold(WPN_ANIM_AIM, WPN_FIRE_RATE); /* aim pose 0x112 */
    w.state   = EM_WPN_AIM;
    w.counter = WPN_INTERVAL;       /* first shot is immediate */
}

/* One trigger-accepted SHOT attempt (the common per-shot block of the
 * fire sub-machine, states 0xB/0x15/0x1F). Dry mag -> auto-reload
 * func_0017B300(.,0); reserve also empty -> dry click (engine sound
 * 0x169). A live round: mag-- AND reserve-- (the TOTAL-pool consume
 * path), post the fire event to the gun (+0x2E = 1), reset the fire
 * counter (+0x276 = 0 — which IS the recoil restart, see WPN_FIRE_RATE)
 * and play the stance fire sound (the block reads the +0x1F0 stance
 * code: 0x164 for 0x31/0x34, 0x165 for 0x32/0x35; the port's single
 * stance is the 0x1D family -> code 0x31 -> 0x164). */
static void weapon_shot(void)
{
    if (w.mag == 0) {
        w.pending = 0;
        w.burst   = 0;
        if (weapon_reload(0) == 0) {
            /* anim 0x33 gates firing; the mag is already refilled. */
            weapon_enter_reload();
        } else {
            em_sfx_play(EM_SFX_WPN_DRY);  /* 0x169 — reserve also empty */
        }
        return;
    }
    w.mag--;
    w.reserve--;          /* the engine's TOTAL-pool rule: BOTH, per shot */
    w.shots++;
    w.fire_event = 1;     /* gun +0x2E — resolved next update             */
    w.counter    = 0;
    /* RECOIL: the counter reset rewinds the held aim clip to frame 0 —
     * the engine's publisher samples the committed ladder clip at
     * frame = +0x276 every tick (FIRE RECOIL block above). The replay
     * runs 25 frames @ 2/tick = 12.5 ticks; at the 6-tick full-auto
     * cadence the next shot restarts it from frame 12, so sustained
     * fire only ever shows the clip's front half — engine-identical
     * overlap by construction (the counter never passes the interval). */
    em_game_anim_hold_restart(WPN_ANIM_AIM, WPN_FIRE_RATE);
    em_sfx_play(EM_SFX_WPN_FIRE);         /* 0x164, the per-shot block    */
    /* SHELL CASING (s29): every shot ejects a casing that hits the
     * floor ~0.7 s later — arm a 42-tick countdown to 0x16A (a free
     * slot always exists: 8 slots vs 7 in flight at the auto cadence;
     * a saturated table drops the oldest-pending overlap harmlessly). */
    for (int k = 0; k < WPN_CASING_SLOTS; k++) {
        if (w.casing[k] == 0) {
            w.casing[k] = WPN_CASING_TICKS;
            break;
        }
    }
}

/* Muzzle point + fire direction from the player placement. The engine
 * derives both from the hand-bone matrix every gun tick (func_00188630:
 * muzzle +0xA0 = M(0x810550)*(-3, tbl.y, 0), dir +0xC0 = normalized
 * second-point delta); with no hand bone wired natively the port uses
 * chest height above the feet along the facing yaw. */
static void weapon_muzzle_ray(const float pos[3], float yaw,
                              float muzzle[3], float dir[3])
{
    muzzle[0] = pos[0];
    muzzle[1] = pos[1] + WPN_MUZZLE_HEIGHT;
    muzzle[2] = pos[2];
    dir[0]    = sinf(yaw);
    dir[1]    = 0.0f;
    dir[2]    = cosf(yaw);
}

/* LASER SIGHT raycast — the per-aim-frame half of func_001854E0/760:
 * the SAME segment query as the bullet (mode 7, mask 0x20) from the
 * muzzle along the fire direction, range 260; the laser clips at the
 * hit point and keeps drawing to the full 260-unit endpoint on a miss.
 * The engine's query reports the hit ACTOR in the scratchpad result
 * (*0x700031D4 — it even tags the victim's +0x0A "laser on me" byte);
 * the port's split runs em_enemy_ray_test beside the world query and
 * the NEAREST of enemy-vs-world clips the beam, exactly like the
 * bullet's victim test. */
static void laser_update(const EmCollision *coll, const float pos[3],
                         float yaw)
{
    float muzzle[3], dir[3];
    weapon_muzzle_ray(pos, yaw, muzzle, dir);

    float end[3] = { muzzle[0] + dir[0] * WPN_RANGE,
                     muzzle[1] + dir[1] * WPN_RANGE,
                     muzzle[2] + dir[2] * WPN_RANGE };

    EmCollHit h;
    if (coll && coll->poly_count &&
        em_collision_segment_query(coll, muzzle, end, WPN_RAY_MASK,
                                   WPN_RAY_ID, &h)) {
        end[0] = h.point[0];
        end[1] = h.point[1];
        end[2] = h.point[2];
    }
    float epoint[3];
    if (em_enemy_ray_test(muzzle, end, epoint) >= 0) {
        /* enemy inside the (already world-clipped) segment: it is the
         * nearer hit by construction — clip the laser on the victim */
        end[0] = epoint[0];
        end[1] = epoint[1];
        end[2] = epoint[2];
    }
    memcpy(w.laser_a, muzzle, sizeof w.laser_a);
    memcpy(w.laser_b, end,    sizeof w.laser_b);
}

/* The gun-side fire-event consumption — func_001861C0, the BULLET.
 *
 * 1. ENDPOINT: with an acquired target the ray aims at its AIM POINT,
 *    overshot by 5 units (the engine cycles 3 screen-cone targets from
 *    func_00199220; the port's em_enemy_acquire is a distance + facing-
 *    cone stand-in — see WPN_AIM_CONE). No target: muzzle + dir*260.
 * 2. One world segment query (mask 7, id 0x20).
 * 3. VICTIM TEST before crediting the world hit: the segment against
 *    every live enemy's hit sphere (em_enemy_ray_test); the NEAREST of
 *    enemy-vs-world wins. An enemy hit applies damage through the
 *    victim's +0x36 mailbox (func_001B41F0's contract: code 5) — the
 *    enemy's own behavior consumes it next tick. Design note: the
 *    world-geometry API (em_collision) stays untouched; actor hits go
 *    through em_enemy's own ray test, the native split of the engine's
 *    "hit actor pointer in the scratchpad result block" (*0x700031D4).
 *
 * Tracer / impact-marker / shell-eject / rumble / gore FX are pending;
 * the overlay flash + crosshair pulse stand in (em_weapon.h). */
static void weapon_resolve_fire(const EmCollision *coll,
                                const float pos[3], float yaw)
{
    float muzzle[3], dir[3];
    float end[3];
    float aim[3];
    weapon_muzzle_ray(pos, yaw, muzzle, dir);

    if (em_enemy_acquire(muzzle, yaw, WPN_RANGE, WPN_AIM_CONE, aim) >= 0) {
        /* targeted shot: endpoint = aim point + 5-unit overshoot */
        float dx = aim[0] - muzzle[0];
        float dy = aim[1] - muzzle[1];
        float dz = aim[2] - muzzle[2];
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dl > 1e-4f) {
            dir[0] = dx / dl;
            dir[1] = dy / dl;
            dir[2] = dz / dl;
        }
        end[0] = aim[0] + dir[0] * WPN_OVERSHOOT;
        end[1] = aim[1] + dir[1] * WPN_OVERSHOOT;
        end[2] = aim[2] + dir[2] * WPN_OVERSHOOT;
    } else {
        end[0] = muzzle[0] + dir[0] * WPN_RANGE;
        end[1] = muzzle[1] + dir[1] * WPN_RANGE;
        end[2] = muzzle[2] + dir[2] * WPN_RANGE;
    }

    int       hit = 0;
    int       world_hit = 0;
    EmCollHit h;
    if (coll && coll->poly_count)
        world_hit = em_collision_segment_query(coll, muzzle, end,
                                               WPN_RAY_MASK, WPN_RAY_ID,
                                               &h) != 0;

    float epoint[3];
    int   victim = em_enemy_ray_test(muzzle, end, epoint);
    if (victim >= 0 && world_hit) {
        /* nearest hit wins (the engine clamps the segment per set) */
        float wd2 = 0.0f, ed2 = 0.0f;
        for (int k = 0; k < 3; k++) {
            float dw = h.point[k] - muzzle[k];
            float de = epoint[k] - muzzle[k];
            wd2 += dw * dw;
            ed2 += de * de;
        }
        if (wd2 < ed2)
            victim = -1;           /* the wall is in front of the enemy */
    }
    if (victim >= 0) {
        /* ENEMY hit: no impact sound here — the flinch/death path owns
         * the victim's audio (the +0x36 mailbox consumer / 0x7D8). */
        em_enemy_damage(victim, WPN_HIT_DMG);  /* victim +0x36 mailbox */
        hit = 1;
    } else {
        hit = world_hit;
        /* WALL hit: arm the impact/ricochet 0x189 one tick out — with
         * the fire event's own one-frame latency the sound lands fire
         * +2 frames, the s29-observed chain (WPN_IMPACT_SFX_TICKS).
         * SURFACE VARIANTS: unpinned — 0x189 for every wall (the
         * em_sfx.h flag on the 0x188/0x18A/0x18B family). */
        if (world_hit)
            w.impact_sfx = WPN_IMPACT_SFX_TICKS;
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

    /* The trigger is CIRCLE — the engine's default-config fire button
     * (config slot spad 0x3B78 = 0x0020 = CIRCLE, live-pinned s29;
     * em_input.h "ENGINE DEFAULT BUTTON CONFIG"). */
    switch (w.fire_mode) {
        case EM_WPN_MODE_SEMI:
            if (in->pressed & EM_PAD_CIRCLE) w.pending = 1;
            if (w.pending && w.counter >= WPN_INTERVAL) {
                w.pending = 0;
                weapon_shot();
            }
            break;
        case EM_WPN_MODE_BURST:
            if ((in->pressed & EM_PAD_CIRCLE) && w.burst == 0 &&
                w.burst_pause == 0)
                w.burst = WPN_BURST_LEN;
            if (w.burst > 0 && w.counter >= WPN_INTERVAL) {
                w.burst--;
                if (w.burst == 0) w.burst_pause = WPN_BURST_PAUSE;
                weapon_shot();
            }
            break;
        case EM_WPN_MODE_AUTO:
            if ((in->held & EM_PAD_CIRCLE) && w.counter >= WPN_INTERVAL)
                weapon_shot();
            break;
        default:
            break;
    }

    /* Manual reload — the engine's raw L3 pad bit (NOT config-mapped),
     * func_0017B300(.,2) top-up; keyboard key 2 (em_input.h). */
    if (w.state == EM_WPN_AIM && (in->pressed & EM_PAD_L3)) {
        if (weapon_reload(2) == 0)
            weapon_enter_reload();
    }
}

/* --- KNIFE / MELEE (engine modes 0x21/0x22 — em_weapon.h header) ------- */

/* Per-attack parameters: anim id, fallback length, impact gate T,
 * damage, sound. attack = 1..3 light combo hits, 0 = the heavy stab. */
static void melee_attack_params(int attack, unsigned *anim, int *fallback,
                                float *gate, int *dmg, unsigned *snd)
{
    switch (attack) {
        case 1:  *anim = MELEE_ANIM_L1;    *fallback = MELEE_L1_FRAMES;
                 *gate = MELEE_GATE_L1;    *dmg = MELEE_DMG_L1;
                 *snd  = EM_SFX_MELEE_HIT1; break;
        case 2:  *anim = MELEE_ANIM_L2;    *fallback = MELEE_L2_FRAMES;
                 *gate = MELEE_GATE_L2;    *dmg = MELEE_DMG_L2;
                 *snd  = EM_SFX_MELEE_HIT2; break;
        case 3:  *anim = MELEE_ANIM_L3;    *fallback = MELEE_L3_FRAMES;
                 *gate = MELEE_GATE_L3;    *dmg = MELEE_DMG_L3;
                 *snd  = EM_SFX_MELEE_HIT3; break;
        default: *anim = MELEE_ANIM_HEAVY; *fallback = MELEE_HEAVY_FRAMES;
                 *gate = MELEE_GATE_HEAVY; *dmg = MELEE_DMG_HEAVY;
                 *snd  = EM_SFX_MELEE_HIT3; break; /* heavy shares 0x17F */
    }
}

/* Start one attack (the engine's mode-0x21 state-N / mode-0x22 entry:
 * anim through the clip arbiter at the property rate 1.0, +0x2E combo
 * buffer cleared, the per-attack window timers armed). attack = 1..3
 * light, 0 = heavy. */
static void melee_start_attack(int attack)
{
    unsigned anim, snd;
    int      fallback, dmg;
    float    gate;
    melee_attack_params(attack, &anim, &fallback, &gate, &dmg, &snd);

    em_game_anim_request(anim, 1.0f);   /* rate 1.0 (D_00248C90 rows)  */
    m.state    = EM_MELEE_SWING;
    m.heavy    = attack == 0;
    m.combo    = attack;
    m.tick     = 0;
    m.len      = anim_ticks(anim, 1.0f, fallback);
    m.impact   = (int)((float)m.len - gate);
    if (m.impact < 3) m.impact = 3;     /* mailbox latency + blend-in  */
    /* Chain window (light hits 1/2 only): opens at len - 19
     * (D_002486D0/D4); hit 3 and the heavy end the string. */
    m.chain_at = (attack == 1 || attack == 2)
                     ? m.len - (int)MELEE_CHAIN_TAIL : 0;
    if (m.chain_at && m.chain_at <= m.impact) m.chain_at = m.impact + 1;
    m.buffered = 0;
    m.confirm  = 0;
    m.swings++;
}

/* The impact gate: swing sound + damage-mailbox write (engine: both
 * fire together, unconditionally — the sound is NOT hit-gated; the
 * mailbox lands on the melee-target link and the TARGET-side polls do
 * the range work). The port resolves the victim here with the reach
 * stand-in (header note) and reads the hit back immediately — the
 * native +0x0A confirm, consumed next tick exactly like the engine. */
static void melee_impact(const float pos[3], float yaw)
{
    unsigned anim, snd;
    int      fallback, dmg;
    float    gate, aim[3];
    melee_attack_params(m.heavy ? 0 : m.combo, &anim, &fallback, &gate,
                        &dmg, &snd);
    (void)anim; (void)fallback; (void)gate;  /* window params (start) */

    em_sfx_play(snd);                   /* 0x17D/0x17E/0x17F, vol 300  */
    int victim = em_enemy_acquire(pos, yaw, EM_MELEE_REACH, MELEE_CONE,
                                  aim);
    if (victim >= 0) {
        em_enemy_damage(victim, (int16_t)dmg);  /* victim +0x36        */
        m.confirm = 1;                  /* target +0x0A read-back      */
        m.hits++;
    }
}

/* The melee per-frame machine — func_001735C0 (light combo) +
 * func_00173E60 (heavy) collapsed to the port shape; engine states in
 * the comments. Runs only while the rifle is HOLSTERED (the engine
 * dispatches modes 0x21/0x22 from the unarmed action codes 0..7). */
static void melee_update(const float pos[3], float yaw,
                         const EmFrameInput *in)
{
    switch (m.state) {
        case EM_MELEE_IDLE:
            if (w.state != EM_WPN_HOLSTERED) return;
            /* Engine dispatch order (func_001607D0 action 0..7): the
             * draw masks first, then FIRE -> mode 0x21, SQUARE ->
             * mode 0x22. The R1 draw is handled by the rifle machine
             * before this runs (em_weapon_update orders it so). */
            if (in->pressed & EM_PAD_CIRCLE)
                melee_start_attack(1);          /* mode 0x21, code 0x36 */
            else if (in->pressed & EM_PAD_SQUARE)
                melee_start_attack(0);          /* mode 0x22, code 0x37 */
            break;

        case EM_MELEE_SWING:
            m.tick++;
            /* +0x2E combo buffer: a FIRE press during the swing (the
             * engine samples it from the damage phase on; the phases
             * open within a tick or two of the start). */
            if (!m.heavy && (in->pressed & EM_PAD_CIRCLE))
                m.buffered = 1;
            if (m.tick == m.impact) {
                melee_impact(pos, yaw);         /* sound + mailbox      */
                break;
            }
            if (m.tick > m.impact && m.confirm) {
                /* Hit confirmed (the engine reads the target's +0x0A
                 * the tick after the write): EARLY EXIT to the recover
                 * states — a landed hit SKIPS the combo chain. */
                m.state       = EM_MELEE_RECOVER;
                m.recov_pause = MELEE_RECOV_PAUSE;  /* state 0x50/0x51 */
                break;
            }
            if (m.chain_at && m.tick >= m.chain_at && m.buffered) {
                /* WHIFF chain: the buffered next hit starts at the
                 * chain window (blend 1.0 through the arbiter). */
                melee_start_attack(m.combo + 1);
                break;
            }
            if (m.tick >= m.len) {
                /* Clip end on a whiff: the 0x63/0x64 exit ramp — NO
                 * recover anim; locomotion resumes by itself. */
                m.state = EM_MELEE_IDLE;
                m.heavy = 0;
                m.combo = 0;
            }
            break;

        case EM_MELEE_RECOVER:
            /* Engine 0x50/0x51: 4-tick pause, then the recover anim
             * 0x10F (idx-0 arm; blend 4.0 — the port plays it at the
             * property rate), then 0x52 waits the clip out -> exit. */
            if (m.recov_pause > 0) {
                if (--m.recov_pause == 0) {
                    em_game_anim_request(MELEE_ANIM_RECOV,
                                         MELEE_RECOV_RATE);
                    m.tick = 0;
                    m.len  = anim_ticks(MELEE_ANIM_RECOV, MELEE_RECOV_RATE,
                                        MELEE_RECOV_FRAMES);
                }
                break;
            }
            if (++m.tick >= m.len) {
                m.state = EM_MELEE_IDLE;
                m.heavy = 0;
                m.combo = 0;
            }
            break;

        default:
            m.state = EM_MELEE_IDLE;
            break;
    }
}

void em_weapon_update(const EmCollision *coll, const float player_pos[3],
                      float player_yaw, const EmFrameInput *in)
{
    if (!in || !player_pos) return;

    /* Scheduled fire-chain tail (ticked BEFORE the fire-event resolve,
     * so an impact armed by this frame's resolve waits a full tick and
     * 0x189 lands exactly fire +2 frames — the s29 chain): */
    if (w.impact_sfx > 0 && --w.impact_sfx == 0)
        em_sfx_play(EM_SFX_WPN_IMPACT);     /* 0x189 wall impact      */
    for (int k = 0; k < WPN_CASING_SLOTS; k++) {
        if (w.casing[k] > 0 && --w.casing[k] == 0)
            em_sfx_play(EM_SFX_WPN_CASING); /* 0x16A casing, shot+42  */
    }

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
            if (draw_held && m.state == EM_MELEE_IDLE) {
                /* major 0 ENTER: reload-if-empty, anim 0x110 at the
                 * property-table rate 1.4, sound 0x162 @vol150
                 * (func_0016F530). The ENTER reload is covered by the
                 * draw anim — no RELOAD state, but it counts as a
                 * reload. */
                if (weapon_reload(0) == 0) w.reloads++;
                em_sfx_play(EM_SFX_WPN_DRAW);
                em_game_anim_request(WPN_ANIM_DRAW, WPN_DRAW_RATE);
                w.state = EM_WPN_DRAW;
                w.timer = em_weapon_draw_ticks();
            }
            break;
        case EM_WPN_DRAW:
            if (!draw_held) {
                weapon_enter_holster();     /* 0x65: anim 0x111, 0x163 */
            } else if (--w.timer <= 0) {
                weapon_enter_aim();         /* major 2: hold pose 0x112 */
                w.pending = 0;
                w.burst   = 0;
            }
            break;
        case EM_WPN_AIM:
            if (!draw_held) {
                weapon_enter_holster();
                w.pending = 0;
                w.burst   = 0;
            } else {
                weapon_fire_logic(in);
                /* SQUARE while armed = the SUB-WEAPON action
                 * (func_0017A970). Attachment 0 (the only one
                 * translated): toggle the D_00810D3C mirror, sound
                 * 0x179 on toggle-ON only — exactly the s29 live
                 * observation (no ammo use, no state change). What
                 * the flag arms is an open item (em_weapon.h). */
                if (in->pressed & EM_PAD_SQUARE) {
                    m.sub_toggle ^= 1;
                    if (m.sub_toggle)
                        em_sfx_play(EM_SFX_SUB_TOGGLE);
                }
            }
            break;
        case EM_WPN_RELOAD:
            /* anim 0x33 wait (major 3); the ammo move already happened.
             * MAG ACTION (s29): 0x168 fires ~0.5 s into the window. */
            if (w.mag_sfx > 0 && --w.mag_sfx == 0)
                em_sfx_play(EM_SFX_WPN_MAG);
            if (--w.timer <= 0)
                weapon_enter_aim();         /* re-hold the aim pose */
            if (!draw_held)                 /* stance drop mid-reload */
                weapon_enter_holster();
            break;
        case EM_WPN_HOLSTER:
            if (--w.timer <= 0)
                w.state = EM_WPN_HOLSTERED;
            break;
        default:
            w.state = EM_WPN_HOLSTERED;
            break;
    }

    /* KNIFE / MELEE — the unarmed attack machine (engine modes
     * 0x21/0x22, dispatched from the unarmed action codes AFTER the
     * draw masks — the rifle switch above ran first, so a same-frame
     * R1 draw correctly wins over a melee press). */
    melee_update(player_pos, player_yaw, in);

    /* LASER SIGHT refresh — the engine's drawers gate on the armed-
     * stance CODE (player +0x1F0 in {0x31, 0x34} with phase +0x1F1 ==
     * 1), which is held for the ENTIRE aim including the fire/recoil
     * ticks (FINDINGS "LASER SIGHT DECODED": the laser runs the whole
     * time the player aims) — i.e. the AIM/FIRE loop, but not the
     * draw, reload (code 0x33) or holster clips. */
    w.laser_on = (w.state == EM_WPN_AIM);
    if (w.laser_on)
        laser_update(coll, player_pos, player_yaw);
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

    /* LASER SIGHT — the translated func_00185760 pass (em_weapon.h).
     * Beam: 32 segments muzzle -> clipped endpoint, per-vertex color =
     * base * max(sin(phase), 0) with a random phase start each frame
     * and a 0.025*len step per segment (func_001E2BA0: phase0 =
     * rand/2^31 * 2pi, f21 = 0.1*len/4; negative sine clamps to 0 in
     * the engine's float->color conversion — the dashed shimmer).
     * func_001E2BA0 zero-initializes the previous-vertex color, so the
     * first segment fades up from black. Dot: 3.0-unit additive glow
     * at the endpoint, R = (0x50 + rand5)/0x80 of the GS 0x80 = 1.0
     * scale, G = B = 0 (func_00185760's unlocked arm). */
    if (w.laser_on) {
        float d[3] = { w.laser_b[0] - w.laser_a[0],
                       w.laser_b[1] - w.laser_a[1],
                       w.laser_b[2] - w.laser_a[2] };
        float len   = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float phase = (float)wpn_rand() / 32768.0f * 6.2831853f;
        float dph   = WPN_LASER_PHASE * len;
        float pa[3] = { w.laser_a[0], w.laser_a[1], w.laser_a[2] };
        float ca[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        for (int i = 1; i <= WPN_LASER_SEGS; i++) {
            float t     = (float)i / (float)WPN_LASER_SEGS;
            float pb[3] = { w.laser_a[0] + d[0] * t,
                            w.laser_a[1] + d[1] * t,
                            w.laser_a[2] + d[2] * t };
            float s  = sinf(phase + dph * (float)i);
            float in = s > 0.0f ? s : 0.0f;
            float cb[4] = { kLaserColor[0] * in, kLaserColor[1] * in,
                            kLaserColor[2] * in, kLaserColor[3] };
            em_gfx_beam(gfx, pa, pb, WPN_LASER_WIDTH, ca, cb);
            memcpy(pa, pb, sizeof pa);
            memcpy(ca, cb, sizeof ca);
        }
        float dr = (float)(0x50 + (wpn_rand() & 0x1F)) / 128.0f;
        float dot[4] = { dr, 0.0f, 0.0f, 1.0f };
        em_gfx_beam_dot(gfx, w.laser_b, WPN_DOT_SIZE, dot);
    }

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

/* Armed-stance query for the camera's aim-state target-height offset
 * (em_weapon.h "AIM CAMERA HOOKUP"): 1 across DRAW/AIM/RELOAD — the
 * engine's weapon modes 0x1D..0x20 — not while holstering out. */
int em_weapon_is_aiming(void)
{
    return w.state == EM_WPN_DRAW || w.state == EM_WPN_AIM ||
           w.state == EM_WPN_RELOAD;
}

void em_weapon_set_fire_mode(uint8_t mode)
{
    if (mode <= EM_WPN_MODE_AUTO) w.fire_mode = mode;
}

/* --- KNIFE / MELEE accessors (em_weapon.h) ------------------------------ */

/* Movement plant for em_game's player_move: the engine's melee modes
 * 0x21/0x22 replace the locomotion modes outright (the heavy yaw steer
 * func_00173DD0 is untranslated — flagged in the header). */
int em_weapon_is_melee(void)      { return m.state != EM_MELEE_IDLE; }

int em_weapon_melee_state(void)   { return m.state; }
int em_weapon_melee_combo(void)   { return m.heavy ? 0 : m.combo; }
int em_weapon_melee_heavy(void)   { return m.heavy; }
int em_weapon_melee_swings(void)  { return m.swings; }
int em_weapon_melee_hits(void)    { return m.hits; }
int em_weapon_sub_toggle(void)    { return m.sub_toggle; }
