/* weapon_fire_test.c — OS-free unit test for em_weapon's fire sub-state
 * machine, manual-reload gate, laser hide window and flashlight toggle
 * (2026-06-11 weapon-fidelity + weapon-visual passes; engine evidence in
 * em_weapon.h):
 *
 *   1. SEMI CADENCE at the ENGINE INTERVAL — func_0017A8B0 stores
 *      +0x2F4 = the aim-ladder clip length (25 for the SPR4) on every
 *      trigger press, so semi spaces shots exactly 13 ticks apart
 *      (counter +2/frame vs 25); mashing every 2 frames cannot beat it,
 *      a single press fires once, presses inside the first ~7 ticks of
 *      the cadence are DROPPED (the +0x2A queue samples only from
 *      counter >= interval - 8), and presses spaced past the cadence
 *      fire 1:1.
 *   2. LASER HIDE WINDOW — player +0x2F2: visible while waiting, hidden
 *      from the shot tick until the cadence expiry, one-tick blink
 *      between chained rounds.
 *   3. L3 MANUAL RELOAD — func_0017B300(.,2) top-up gate: a FULL mag
 *      ignores L3; mag < 30 with reserve > mag reloads; reserve == mag
 *      does not; the engine's quirky fill compare is replicated. The
 *      reload anim must go through the HOLD-type request (the
 *      2026-06-11 stagger fix — no idle interlude at the clip end).
 *   4. DRY-MAG AUTO RELOAD at the cadence EXPIRY (mode 1) — no second
 *      trigger press required.
 *   5. FLASHLIGHT — SQUARE while aiming toggles the PERSISTENT
 *      preference ON (sound 0x179) and OFF (silent); NO timer/auto-off;
 *      the flag survives a holster + re-draw.
 *   6. AUTO parity — the 0x1E fire state writes +0x2F4 = 12.0: a
 *      31-frame hold = 6 rounds at the 6-frame in-burst cadence.
 *   7. INPUT-EVENT-API MASH — the same mash driven through the REAL
 *      pad model (em_input_handle_event -> em_input_pad -> the
 *      em_frame edge computation): the keyboard path cannot out-shoot
 *      the sub-state machine either.
 *
 * Links em_weapon.c + em_input.c; every other module the weapon talks
 * to is stubbed below (clip-less anim stubs -> the flagged fallback
 * windows and the 25-frame true-length semi interval), so the test runs
 * headless on any host: `make test-weapon`.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "em_input.h"
#include "em_model.h"
#include "em_platform.h"
#include "game/em_weapon.h"
#include "game/em_frame.h"
#include "game/em_game.h"
#include "game/em_sfx.h"
#include "game/em_enemy.h"

/* ---- stubs (the modules em_weapon.c references) ----------------------- */

/* anim mailbox recorder: the reload-stagger regression check needs to
 * know WHICH path (request vs hold) carried each clip id. */
static unsigned last_req_clip, last_hold_clip;
int em_game_anim_request(unsigned clip_id, float rate)
{ (void)rate; last_req_clip = clip_id; return 1; }
int em_game_anim_hold(unsigned clip_id, float rate)
{ (void)rate; last_hold_clip = clip_id; return 1; }
int em_game_anim_hold_restart(unsigned clip_id, float rate)
{ (void)clip_id; (void)rate; return 1; }
int em_game_anim_frames(unsigned clip_id)
{ (void)clip_id; return 0; }            /* clip-less: fallback windows */
unsigned em_game_anim_active(void) { return 0; }
int      em_game_anim_frame(void)  { return -1; }

/* Shared SDK stream (func_00122BB8, em_random.c): same arithmetic, local
 * state so the test stays self-contained. */
uint32_t em_random_next(void)
{
    static uint32_t state = 0x45;
    state = state * UINT32_C(0x41C64E6D) + UINT32_C(0x3039);
    return state & UINT32_C(0x7FFFFFFF);
}

static int sfx_count[0x800];
void em_sfx_play(unsigned id) { if (id < 0x800) sfx_count[id]++; }
void em_sfx_play_at(unsigned id, const float pos[3], float radius)
{ (void)pos; (void)radius; if (id < 0x800) sfx_count[id]++; }

int em_enemy_acquire(const float from[3], float yaw, float max_dist,
                     float cone_cos, float aim_out[3])
{ (void)from; (void)yaw; (void)max_dist; (void)cone_cos; (void)aim_out;
  return -1; }                  /* the melee reach resolver — unused here */

/* STUB ENEMY WORLD for the round-robin section (8.): two stationary
 * targets in front of the muzzle-fallback ray (origin y 12, +Z). The
 * acquisition chain consumes em_enemy_count/_targetable/_aim_point/
 * _ray_test exactly like the real module; em_enemy_damage records the
 * per-shot victim sequence. stub_on = 0 keeps every earlier section in
 * the empty world. */
#define STUB_N 2
static int   stub_on;
static int   stub_alive[STUB_N];
static float stub_pos[STUB_N][3];
static int   stub_seq[32];
static int   stub_seq_n;

int em_enemy_count(void) { return stub_on ? STUB_N : 0; }
int em_enemy_targetable(int i)
{ return stub_on && i >= 0 && i < STUB_N && stub_alive[i]; }
void em_enemy_aim_point(int i, float out[3])
{ memcpy(out, stub_pos[i], sizeof stub_pos[i]); }

int em_enemy_ray_test(const float from[3], const float to[3],
                      float hit_out[3])
{
    /* segment vs 3.0-radius spheres at the stub aim points — the same
     * shape as the real em_enemy_ray_test (nearest entry wins). */
    if (!stub_on) return -1;
    int   best   = -1;
    float best_t = 2.0f;
    float d[3]   = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    float dd     = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (dd < 1e-9f) return -1;
    for (int i = 0; i < STUB_N; i++) {
        if (!stub_alive[i]) continue;
        float m[3] = { from[0] - stub_pos[i][0], from[1] - stub_pos[i][1],
                       from[2] - stub_pos[i][2] };
        float b    = m[0] * d[0] + m[1] * d[1] + m[2] * d[2];
        float c    = m[0] * m[0] + m[1] * m[1] + m[2] * m[2] - 9.0f;
        float disc = b * b - dd * c;
        if (disc < 0.0f) continue;
        float t = (-b - sqrtf(disc)) / dd;
        if (c <= 0.0f) t = 0.0f;
        if (t < 0.0f || t > 1.0f || t >= best_t) continue;
        best   = i;
        best_t = t;
    }
    if (best >= 0 && hit_out)
        for (int k = 0; k < 3; k++)
            hit_out[k] = from[k] + d[k] * best_t;
    return best;
}

void em_enemy_damage(int i, int16_t code)
{
    (void)code;
    if (stub_seq_n < (int)(sizeof stub_seq / sizeof stub_seq[0]))
        stub_seq[stub_seq_n++] = i;
}

int em_gfx_last_skinned_bone(EmGfx *gfx, uint32_t bone, float out16[16])
{ (void)gfx; (void)bone; (void)out16; return 0; }

/* Camera publish for the screen-cone test: an engine-shaped projection
 * from the muzzle height looking down +Z — clip.x = 1.5*x (= the
 * s/EM_GS_HALF_W = 480/320 row), clip.y = (480/224)*(y - 12),
 * clip.w = z. Both stub targets land well inside the |sx| <= 66 /
 * |sy| <= 45 GS-center box. */
int em_gfx_last_viewproj(EmGfx *gfx, float out16[16])
{
    (void)gfx;
    if (!stub_on) return 0;     /* headless default: no camera         */
    memset(out16, 0, 16 * sizeof(float));
    out16[0]  = 1.5f;                    /* clip.x = 1.5 * x           */
    out16[5]  = 480.0f / 224.0f;         /* clip.y = 2.1429 * y ...    */
    out16[13] = -12.0f * 480.0f / 224.0f; /* ... centered at y = 12    */
    out16[11] = 1.0f;                    /* clip.w = z                 */
    return 1;
}
void em_gfx_beam(EmGfx *gfx, const float a[3], const float b[3],
                 float width, const float ca[4], const float cb[4])
{ (void)gfx; (void)a; (void)b; (void)width; (void)ca; (void)cb; }
void em_gfx_beam_dot(EmGfx *gfx, const float p[3], float size,
                     const float c[4])
{ (void)gfx; (void)p; (void)size; (void)c; }
int em_gfx_beam_texture_set(EmGfx *gfx, int slot, const uint8_t *rgba,
                            uint32_t w, uint32_t h)
{ (void)gfx; (void)slot; (void)rgba; (void)w; (void)h; return 0; }
void em_gfx_beam_tex(EmGfx *gfx, int slot, const float a[3],
                     const float b[3], float width, const float c[4])
{ (void)gfx; (void)slot; (void)a; (void)b; (void)width; (void)c; }
void em_gfx_beam_tex_roll(EmGfx *gfx, int slot, const float a[3],
                          const float b[3], float width, float roll,
                          const float c[4])
{ (void)gfx; (void)slot; (void)a; (void)b; (void)width; (void)roll;
  (void)c; }
void em_gfx_beam_dot_tex(EmGfx *gfx, int slot, const float p[3],
                         float size, const float c[4])
{ (void)gfx; (void)slot; (void)p; (void)size; (void)c; }
void em_gfx_beam_tri_tex(EmGfx *gfx, int slot, const float p[9],
                         const float uv[6], const float c[4])
{ (void)gfx; (void)slot; (void)p; (void)uv; (void)c; }
void em_gfx_spot_light(EmGfx *gfx, const float pos[3], const float dir[3],
                       const float rgb[3], float range,
                       float cos_inner, float cos_outer)
{ (void)gfx; (void)pos; (void)dir; (void)rgb; (void)range;
  (void)cos_inner; (void)cos_outer; }

int em_model_load(EmModel *m, const char *path)
{ (void)path; memset(m, 0, sizeof *m); return 1; }   /* asset-less host */
void em_model_free(EmModel *m) { (void)m; }

int em_collision_segment_query(const EmCollision *c, const float from[3],
                               const float to[3], unsigned mask, int id,
                               EmCollHit *hit)
{ (void)c; (void)from; (void)to; (void)mask; (void)id; (void)hit;
  return 0; }

/* ---- driver ------------------------------------------------------------ */

/* ENGINE TIMINGS under the clip-less stubs (true-length fallbacks):
 * semi interval 25 -> expiry 12 ticks after the shot tick, chained shot
 * the 13th; the +0x2A queue samples from counter >= 17 (tick 8+). */
#define SEMI_SPACING 13

static int      fails;
static uint16_t held_now;

static void check(int cond, const char *what)
{
    if (cond) return;
    fails++;
    printf("weapon fire test: CHECK FAILED — %s (state %d, mag %u, "
           "reserve %d, shots %d, reloads %d)\n", what, em_weapon_state(),
           em_weapon_mag(), em_weapon_reserve(), em_weapon_shots(),
           em_weapon_reloads());
}

/* One frame: buttons `down` newly pressed this frame (edges computed
 * against the running held set), `up` newly released. */
static void frame(uint16_t down, uint16_t up)
{
    static const float pos[3] = { 0.0f, 0.0f, 0.0f };
    EmFrameInput in;
    memset(&in, 0, sizeof in);
    in.lx = in.ly = in.rx = in.ry = 0x80;
    in.pressed  = (uint16_t)(down & ~held_now);
    in.released = (uint16_t)(up & held_now);
    held_now    = (uint16_t)((held_now | down) & ~up);
    in.held     = held_now;
    em_weapon_update(NULL, pos, 0.0f, &in);
}

static void frames(int n) { while (n-- > 0) frame(0, 0); }

/* Draw the rifle and settle into AIM (R1 held from here on). */
static void draw_to_aim(void)
{
    held_now = 0;
    frame(EM_PAD_R1, 0);
    int guard = 64;
    while (em_weapon_state() != EM_WPN_AIM && guard-- > 0) frame(0, 0);
    check(em_weapon_state() == EM_WPN_AIM, "reached AIM after the draw");
}

int main(void)
{
    /* 1a. SEMI MASH: press CIRCLE every 2nd frame for 61 frames — the
     * ladder-clip cadence must hold every shot exactly SEMI_SPACING
     * frames apart: shots at 0/13/26/39/52 = 5 total (the old flat-12
     * interval yielded 11 — the user-reported double rate). */
    em_weapon_reset(30, 120);
    draw_to_aim();
    int last_shot_frame = -1, bad_gap = 0;
    for (int f = 0; f <= 60; f++) {
        int before = em_weapon_shots();
        if ((f & 1) == 0) { frame(EM_PAD_CIRCLE, 0); }
        else              { frame(0, EM_PAD_CIRCLE); }
        if (em_weapon_shots() != before) {
            if (last_shot_frame >= 0 &&
                f - last_shot_frame != SEMI_SPACING)
                bad_gap = f - last_shot_frame;
            last_shot_frame = f;
        }
    }
    check(em_weapon_shots() == 5,
          "semi mash: 61 frames of 2-frame presses = 5 shots (the "
          "25-frame ladder cadence), not one per press");
    check(bad_gap == 0, "semi mash: every shot exactly 13 frames apart");

    /* 1b. Single press = single shot (no repeat while held). */
    em_weapon_reset(30, 120);
    draw_to_aim();
    frame(EM_PAD_CIRCLE, 0);
    frames(40);
    check(em_weapon_shots() == 1, "single semi press fires exactly once");

    /* 1c. A press EARLY in the cadence (before counter >= interval-8,
     * i.e. inside ~7 ticks of the shot) is DROPPED — the engine's
     * queue-sampling window. */
    em_weapon_reset(30, 120);
    draw_to_aim();
    frame(EM_PAD_CIRCLE, 0);            /* shot at tick 0 */
    frame(0, EM_PAD_CIRCLE);
    frames(4);                          /* ticks 1..5 */
    frame(EM_PAD_CIRCLE, 0);            /* press at tick 6: counter 14 < 17 */
    frame(0, EM_PAD_CIRCLE);
    frames(30);
    check(em_weapon_shots() == 1,
          "press in the first ~7 cadence ticks is dropped (queue window "
          "opens at counter >= interval - 8)");

    /* 1d. Presses spaced past the cadence fire 1:1. */
    em_weapon_reset(30, 120);
    draw_to_aim();
    for (int k = 0; k < 4; k++) {
        frame(EM_PAD_CIRCLE, 0);
        frame(0, EM_PAD_CIRCLE);
        frames(SEMI_SPACING);           /* past the 13-tick spacing */
    }
    check(em_weapon_shots() == 4,
          "presses spaced past the cadence fire 1:1");

    /* 2. LASER HIDE WINDOW (+0x2F2): visible while waiting, hidden from
     * the shot tick to the cadence expiry, blink between chained
     * rounds. */
    em_weapon_reset(30, 120);
    draw_to_aim();
    check(em_weapon_laser_visible() == 1, "laser visible while waiting");
    frame(EM_PAD_CIRCLE, 0);            /* shot tick: hidden */
    check(em_weapon_laser_visible() == 0,
          "laser hidden on the shot tick (+0x2F2 cleared)");
    frame(0, EM_PAD_CIRCLE);
    int hidden_all = 1;
    for (int k = 0; k < 11; k++) {      /* cadence ticks 1..11 */
        frame(0, 0);
        if (em_weapon_laser_visible()) hidden_all = 0;
    }
    check(hidden_all, "laser stays hidden through the cadence");
    frames(2);                          /* expiry tick 12 + WAIT tick 13 */
    check(em_weapon_laser_visible() == 1,
          "laser back after the cadence expiry");
    /* chained round: a RE-PRESS inside the queue window (ticks 8..12)
     * arms +0x2A; the expiry tick blinks the laser visible for one
     * frame and the queued shot hides it again the next tick. */
    frame(EM_PAD_CIRCLE, 0);            /* shot (tick 0), hidden */
    frame(0, EM_PAD_CIRCLE);            /* tick 1: release */
    frames(7);                          /* ticks 2..8 */
    frame(EM_PAD_CIRCLE, 0);            /* tick 9: press edge in window */
    frame(0, EM_PAD_CIRCLE);            /* tick 10 */
    int blink = 0, rehidden = 0;
    for (int k = 0; k < 4; k++) {       /* ticks 11..14 (expiry = 12) */
        frame(0, 0);
        if (em_weapon_laser_visible()) blink = 1;
        else if (blink)                rehidden = 1;
    }
    check(blink && rehidden,
          "chained round: one-tick laser blink at the expiry, hidden "
          "again on the queued shot");

    /* 3a. L3 with a FULL mag: ignored (the mode-2 top-up gate). */
    em_weapon_reset(30, 120);
    draw_to_aim();
    int reloads0 = em_weapon_reloads();
    frame(EM_PAD_L3, 0);
    frame(0, EM_PAD_L3);
    frames(4);
    check(em_weapon_state() == EM_WPN_AIM &&
          em_weapon_reloads() == reloads0 && em_weapon_mag() == 30,
          "L3 with a full mag does NOT reload");

    /* 3b. L3 with a short mag and a live reserve: top-up — and the
     * reload anim goes through the HOLD path (stagger fix). */
    frame(EM_PAD_CIRCLE, 0); frame(0, EM_PAD_CIRCLE);
    frames(SEMI_SPACING);
    frame(EM_PAD_CIRCLE, 0); frame(0, EM_PAD_CIRCLE);
    frames(SEMI_SPACING + 2);           /* back in WAIT (L3 honored) */
    check(em_weapon_mag() == 28 && em_weapon_reserve() == 118,
          "two semi shots: 28/118");
    last_hold_clip = 0;
    frame(EM_PAD_L3, 0);
    frame(0, EM_PAD_L3);
    frames(2);
    check(em_weapon_state() == EM_WPN_RELOAD && em_weapon_mag() == 30 &&
          em_weapon_reserve() == 118 &&
          em_weapon_reloads() == reloads0 + 1,
          "L3 with mag 28 top-ups to 30, reserve untouched");
    check(last_hold_clip == 0x11B,
          "reload anim 0x11B (283 — the TRUE reload clip, the "
          "D_00248B98 table pick; NOT the 0x33 action code) requested "
          "as a HELD clip (no idle pop at the clip end)");

    /* 3c. reserve == mag: no top-up (the reserve holds nothing extra). */
    em_weapon_reset(20, 20);
    draw_to_aim();
    reloads0 = em_weapon_reloads();
    frame(EM_PAD_L3, 0);
    frame(0, EM_PAD_L3);
    frames(4);
    check(em_weapon_state() == EM_WPN_AIM &&
          em_weapon_reloads() == reloads0 && em_weapon_mag() == 20,
          "L3 with reserve == mag does NOT reload");

    /* 3d. The engine's quirky fill compare (reserve vs rounds NEEDED):
     * mag 25 / reserve 27 -> 27 >= (30-25) -> mag = 30 (engine-exact;
     * func_0017B300's matched top-up arm). */
    em_weapon_reset(25, 27);
    draw_to_aim();
    frame(EM_PAD_L3, 0);
    frame(0, EM_PAD_L3);
    frames(2);
    check(em_weapon_state() == EM_WPN_RELOAD && em_weapon_mag() == 30,
          "top-up fill quirk: mag 25 / reserve 27 fills to 30");

    /* 4. DRY-MAG AUTO RELOAD at the cadence expiry — no second press. */
    em_weapon_reset(1, 50);
    draw_to_aim();
    reloads0 = em_weapon_reloads();
    frame(EM_PAD_CIRCLE, 0);
    frame(0, EM_PAD_CIRCLE);
    check(em_weapon_shots() >= 1 && em_weapon_mag() == 0,
          "dry run: the only round fired");
    frames(SEMI_SPACING + 2);   /* expiry lands 12 ticks after the shot */
    check(em_weapon_state() == EM_WPN_RELOAD &&
          em_weapon_reloads() == reloads0 + 1 && em_weapon_mag() == 30,
          "empty mag auto-reloads at the cadence expiry (mode 1), "
          "without another trigger press");

    /* 5. FLASHLIGHT — SQUARE while aiming: a PERSISTENT preference,
     * no timer, no auto-off (the corrected D_00810D3C model). */
    em_weapon_reset(30, 120);
    draw_to_aim();
    int on_sfx0  = sfx_count[0x179];
    int off_sfx0 = sfx_count[0x15D];
    frame(EM_PAD_SQUARE, 0);
    frame(0, EM_PAD_SQUARE);
    check(em_weapon_flashlight() == 1 &&
          em_weapon_flashlight_timer() == 0 &&
          sfx_count[0x179] == on_sfx0 + 1,
          "SQUARE while aiming toggles the flashlight ON (0x179; the "
          "shoulder-burst timer stays unarmed)");
    frames(400);    /* well past the old 300-frame burst */
    check(em_weapon_flashlight() == 1 &&
          sfx_count[0x15D] == off_sfx0,
          "NO auto-off: still on after 400 frames, no 0x15D switch "
          "sound (the light never runs out)");
    /* the preference survives a holster + re-draw */
    frame(0, EM_PAD_R1);                     /* drop the stance        */
    frames(em_weapon_holster_ticks() + 2);
    check(em_weapon_state() == EM_WPN_HOLSTERED &&
          em_weapon_flashlight() == 1,
          "holstered: the preference flag persists");
    draw_to_aim();                           /* re-draw (R1 held)      */
    check(em_weapon_flashlight() == 1,
          "re-drawn: the light preference is replayed, no re-toggle");
    /* manual OFF is silent */
    int on_sfx1 = sfx_count[0x179];
    frame(EM_PAD_SQUARE, 0); frame(0, EM_PAD_SQUARE);
    check(em_weapon_flashlight() == 0 &&
          sfx_count[0x179] == on_sfx1 &&
          sfx_count[0x15D] == off_sfx0,
          "manual toggle-OFF is silent");
    frame(EM_PAD_SQUARE, 0); frame(0, EM_PAD_SQUARE);
    check(em_weapon_flashlight() == 1 &&
          sfx_count[0x179] == on_sfx1 + 1,
          "third toggle: ON again with 0x179");

    /* 6. AUTO parity: the 0x1E store (+0x2F4 = 12.0) keeps the 6-frame
     * in-burst cadence — a 31-frame hold = 6 rounds. */
    em_weapon_reset(30, 120);
    em_weapon_set_fire_mode(EM_WPN_MODE_AUTO);
    draw_to_aim();
    frame(EM_PAD_CIRCLE, 0);
    frames(30);
    frame(0, EM_PAD_CIRCLE);
    frames(10);
    check(em_weapon_shots() == 6, "auto: 31-frame hold = 6 rounds");
    em_weapon_set_fire_mode(EM_WPN_MODE_SEMI);

    /* 7. INPUT-EVENT-API MASH: the same 2-frame mash driven through the
     * REAL pad model — KEY_DOWN/KEY_UP 'l' (CIRCLE) and a held 'e'
     * (R1) through em_input_handle_event, sampled per frame with
     * em_input_pad and edge-computed exactly like em_frame's step C.
     * Proves the keyboard event path cannot out-shoot the sub-state
     * machine (the user-reported runtime rapid fire). */
    em_weapon_reset(30, 120);
    em_input_init();
    {
        static const float pos[3] = { 0.0f, 0.0f, 0.0f };
        uint16_t prev = 0;
        int      shots0 = em_weapon_shots();
        int      last = -1, gap_bad = 0;
        EmEvent  ev;
        memset(&ev, 0, sizeof ev);
        ev.key  = 'e';
        ev.type = EM_EVENT_KEY_DOWN;
        em_input_handle_event(&ev);              /* hold R1 */
        for (int f = 0; f < 100; f++) {
            /* mash 'l': down on even frames, up on odd — from frame 20
             * (after the draw window) */
            if (f >= 20) {
                ev.key  = 'l';
                ev.type = (f & 1) ? EM_EVENT_KEY_UP : EM_EVENT_KEY_DOWN;
                em_input_handle_event(&ev);
            }
            EmPadState pad;
            em_input_pad(&pad);
            EmFrameInput in;
            memset(&in, 0, sizeof in);
            in.lx = in.ly = in.rx = in.ry = 0x80;
            in.held     = pad.buttons;
            in.pressed  = (uint16_t)(pad.buttons & ~prev);
            in.released = (uint16_t)(prev & ~pad.buttons);
            prev        = pad.buttons;
            int before = em_weapon_shots();
            em_weapon_update(NULL, pos, 0.0f, &in);
            if (em_weapon_shots() != before) {
                if (last >= 0 && f - last < SEMI_SPACING)
                    gap_bad = f - last;
                last = f;
            }
        }
        check(em_weapon_shots() - shots0 > 0,
              "event-API mash: the rifle fires at all");
        check(gap_bad == 0,
              "event-API mash: no two shots closer than the 13-tick "
              "ladder cadence through the real pad model");
    }

    /* 8. TARGET ACQUISITION + 2-ENEMY ROUND-ROBIN + LOCK STEER (the
     * 2026-06-11 func_00199220 / func_001861C0 / func_0017AF70
     * translation). Stub world: A (slot index 0) dead ahead at
     * (0, 12, 30) on the muzzle-fallback ray, B (index 1) at
     * (7, 12, 50) — both inside the |sx| <= 66 / |sy| <= 45 GS-center
     * cone through the stub camera, both clearing each other's
     * validation ray. Expected slots: tgt[0] = A (nearest),
     * tgt[1] = B, tgt[2] empty. */
    em_weapon_reset(30, 120);
    em_weapon_render((EmGfx *)&held_now);   /* cache a non-NULL device:
                                             * weapon_viewproj needs it
                                             * (all gfx calls stubbed) */
    stub_on       = 1;
    stub_alive[0] = stub_alive[1] = 1;
    stub_pos[0][0] = 0.0f; stub_pos[0][1] = 12.0f; stub_pos[0][2] = 30.0f;
    stub_pos[1][0] = 7.0f; stub_pos[1][1] = 12.0f; stub_pos[1][2] = 50.0f;
    stub_seq_n     = 0;
    draw_to_aim();
    frame(0, 0);                            /* one settled aim tick    */
    check(em_weapon_lock_target() == 0 &&
          em_weapon_target_slot(1) == 1 &&
          em_weapon_target_slot(2) == -1,
          "acquisition fills the slots nearest-first: E0 = A, E4 = B, "
          "E8 empty");

    /* SEMI round-robin: each press advances +0x2F0 BEFORE the shot
     * (the engine stance-top increment on the press latch), so the
     * shots walk slot 1 (B), slot 2 -> empty -> E0 (A), slot 0 (A),
     * slot 1 (B)... — the decoded func_001861C0 fallback rule. */
    for (int k = 0; k < 4; k++) {
        frame(EM_PAD_CIRCLE, 0);
        frame(0, EM_PAD_CIRCLE);
        frames(SEMI_SPACING);
    }
    check(em_weapon_shots() == 4 && stub_seq_n == 4,
          "round-robin leg: 4 spaced presses = 4 resolved hits");
    check(stub_seq_n >= 4 &&
          stub_seq[0] == 1 && stub_seq[1] == 0 &&
          stub_seq[2] == 0 && stub_seq[3] == 1,
          "manual 3-slot round-robin: victims B, A, A, B (cycle 1 -> "
          "E4, 2 -> empty E8 falls back to E0, 0 -> E0, 1 -> E4)");

    /* LOCK STEER at the decoded rate: kill A -> the lock re-acquires
     * B (atan2(7, 50) = 0.139 rad off the body heading). From centered
     * blends the desired yaw blend is 0.5 + 0.5*0.139/1.0469 = 0.5666:
     * every step moves EXACTLY 0.02 blend units toward it. (The
     * clip-less harness pins the fallback gun dir at +Z, so the
     * angular error never closes — in the game the posed hand matrix
     * follows the blends and the creep converges/snaps; here we
     * assert the RATE, the per-frame 0.02 bound.) */
    stub_alive[0] = 0;
    frame(0, 0);                            /* re-acquire: lock = B    */
    check(em_weapon_lock_target() == 1, "lock re-acquires B once A "
          "is gone");
    {
        static const float org[3] = { 0.0f, 0.0f, 0.0f };
        float p = 0.5f, y = 0.5f;
        check(em_weapon_lock_steer(org, 0.0f, p, y, &p, &y) == 1,
              "lock steer engages with a live lock");
        check(fabsf(y - 0.52f) < 1e-4f && fabsf(p - 0.5f) < 1e-4f,
              "first steer step is exactly 0.02 blend units toward "
              "the lock (yaw only — the target sits at muzzle height)");
        for (int k = 0; k < 3; k++)
            em_weapon_lock_steer(org, 0.0f, p, y, &p, &y);
        check(fabsf(y - 0.58f) < 1e-4f,
              "steer rate holds at 0.02 blend units per call (the "
              "decoded func_0017AF70 step)");
    }

    /* Slot table empties outside AIM (engine: stance entries clear
     * D_008106E0 — the laser's warm color drops with it). */
    frame(0, EM_PAD_R1);
    frames(em_weapon_holster_ticks() + 2);
    check(em_weapon_lock_target() == -1,
          "lock clears when the stance drops");
    stub_on = 0;

    printf("weapon fire test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
