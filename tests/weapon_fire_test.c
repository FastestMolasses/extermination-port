/* weapon_fire_test.c — OS-free unit test for em_weapon's fire sub-state
 * machine, manual-reload gate and flashlight toggle (the 2026-06-11
 * weapon-fidelity pass; engine evidence in em_weapon.h):
 *
 *   1. SEMI CADENCE — the engine fire sub-state HOLDS through the
 *      6-frame cadence (func_00170A60 states 0/0xA/0xB): mashing the
 *      trigger every 2 frames still yields exactly one shot per 6
 *      frames; a single press yields exactly one shot; presses spaced
 *      past the cadence fire 1:1.
 *   2. L3 MANUAL RELOAD — func_0017B300(.,2) top-up gate: a FULL mag
 *      ignores L3; mag < 30 with reserve > mag reloads; reserve == mag
 *      does not; the engine's quirky fill compare (reserve vs the
 *      rounds NEEDED) is replicated.
 *   3. DRY-MAG AUTO RELOAD at the cadence EXPIRY (mode 1) — no second
 *      trigger press required.
 *   4. FLASHLIGHT — SQUARE while aiming toggles the PERSISTENT
 *      preference ON (sound 0x179) and OFF (silent); NO timer and NO
 *      auto-off (the engine's D_00810D3C flag — the 300-frame burst
 *      belongs to the separate, unhooked shoulder-light system, whose
 *      introspection timer must stay 0); the flag survives a holster +
 *      re-draw.
 *   5. AUTO parity — a 31-frame hold = 6 rounds (one per 6 frames).
 *
 * Links ONLY em_weapon.c; every other module the weapon talks to is
 * stubbed below (clip-less anim stubs -> the flagged fallback windows),
 * so the test runs headless on any host: `make test-weapon`.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "em_input.h"
#include "game/em_weapon.h"
#include "game/em_frame.h"
#include "game/em_game.h"
#include "game/em_sfx.h"
#include "game/em_enemy.h"

/* ---- stubs (the modules em_weapon.c references) ----------------------- */

int em_game_anim_request(unsigned clip_id, float rate)
{ (void)clip_id; (void)rate; return 1; }
int em_game_anim_hold(unsigned clip_id, float rate)
{ (void)clip_id; (void)rate; return 1; }
int em_game_anim_hold_restart(unsigned clip_id, float rate)
{ (void)clip_id; (void)rate; return 1; }
int em_game_anim_frames(unsigned clip_id)
{ (void)clip_id; return 0; }            /* clip-less: fallback windows */

static int sfx_count[0x800];
void em_sfx_play(unsigned id) { if (id < 0x800) sfx_count[id]++; }

int em_enemy_acquire(const float from[3], float yaw, float max_dist,
                     float cone_cos, float aim_out[3])
{ (void)from; (void)yaw; (void)max_dist; (void)cone_cos; (void)aim_out;
  return -1; }
int em_enemy_ray_test(const float from[3], const float to[3],
                      float hit_out[3])
{ (void)from; (void)to; (void)hit_out; return -1; }
void em_enemy_damage(int i, int16_t code) { (void)i; (void)code; }

int em_gfx_last_skinned_bone(EmGfx *gfx, uint32_t bone, float out16[16])
{ (void)gfx; (void)bone; (void)out16; return 0; }
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
void em_gfx_beam_dot_tex(EmGfx *gfx, int slot, const float p[3],
                         float size, const float c[4])
{ (void)gfx; (void)slot; (void)p; (void)size; (void)c; }
void em_gfx_spot_light(EmGfx *gfx, const float pos[3], const float dir[3],
                       const float rgb[3], float range,
                       float cos_inner, float cos_outer)
{ (void)gfx; (void)pos; (void)dir; (void)rgb; (void)range;
  (void)cos_inner; (void)cos_outer; }

int em_collision_segment_query(const EmCollision *c, const float from[3],
                               const float to[3], unsigned mask, int id,
                               EmCollHit *hit)
{ (void)c; (void)from; (void)to; (void)mask; (void)id; (void)hit;
  return 0; }

/* ---- driver ------------------------------------------------------------ */

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
     * cadence must hold every shot 6 frames apart (11 shots total). */
    em_weapon_reset(30, 120);
    draw_to_aim();
    int last_shot_frame = -1, bad_gap = 0;
    for (int f = 0; f <= 60; f++) {
        int before = em_weapon_shots();
        if ((f & 1) == 0) { frame(EM_PAD_CIRCLE, 0); }
        else              { frame(0, EM_PAD_CIRCLE); }
        if (em_weapon_shots() != before) {
            if (last_shot_frame >= 0 && f - last_shot_frame != 6)
                bad_gap = f - last_shot_frame;
            last_shot_frame = f;
        }
    }
    check(em_weapon_shots() == 11,
          "semi mash: 61 frames of 2-frame presses = 11 shots (6-frame "
          "cadence), not one per press");
    check(bad_gap == 0, "semi mash: every shot exactly 6 frames apart");

    /* 1b. Single press = single shot (no repeat while held). */
    em_weapon_reset(30, 120);
    draw_to_aim();
    frame(EM_PAD_CIRCLE, 0);
    frames(30);
    check(em_weapon_shots() == 1, "single semi press fires exactly once");

    /* 1c. Presses spaced 8 frames fire 1:1 (past the cadence). */
    em_weapon_reset(30, 120);
    draw_to_aim();
    for (int k = 0; k < 4; k++) {
        frame(EM_PAD_CIRCLE, 0);
        frame(0, EM_PAD_CIRCLE);
        frames(6);
    }
    check(em_weapon_shots() == 4, "8-frame-spaced semi presses fire 1:1");

    /* 2a. L3 with a FULL mag: ignored (the mode-2 top-up gate). */
    em_weapon_reset(30, 120);
    draw_to_aim();
    int reloads0 = em_weapon_reloads();
    frame(EM_PAD_L3, 0);
    frame(0, EM_PAD_L3);
    frames(4);
    check(em_weapon_state() == EM_WPN_AIM &&
          em_weapon_reloads() == reloads0 && em_weapon_mag() == 30,
          "L3 with a full mag does NOT reload");

    /* 2b. L3 with a short mag and a live reserve: top-up. */
    frame(EM_PAD_CIRCLE, 0); frame(0, EM_PAD_CIRCLE); frames(6);
    frame(EM_PAD_CIRCLE, 0); frame(0, EM_PAD_CIRCLE); frames(6);
    check(em_weapon_mag() == 28 && em_weapon_reserve() == 118,
          "two semi shots: 28/118");
    frame(EM_PAD_L3, 0);
    frame(0, EM_PAD_L3);
    frames(2);
    check(em_weapon_state() == EM_WPN_RELOAD && em_weapon_mag() == 30 &&
          em_weapon_reserve() == 118 &&
          em_weapon_reloads() == reloads0 + 1,
          "L3 with mag 28 top-ups to 30, reserve untouched");

    /* 2c. reserve == mag: no top-up (the reserve holds nothing extra). */
    em_weapon_reset(20, 20);
    draw_to_aim();
    reloads0 = em_weapon_reloads();
    frame(EM_PAD_L3, 0);
    frame(0, EM_PAD_L3);
    frames(4);
    check(em_weapon_state() == EM_WPN_AIM &&
          em_weapon_reloads() == reloads0 && em_weapon_mag() == 20,
          "L3 with reserve == mag does NOT reload");

    /* 2d. The engine's quirky fill compare (reserve vs rounds NEEDED):
     * mag 25 / reserve 27 -> 27 >= (30-25) -> mag = 30 (engine-exact;
     * func_0017B300's matched top-up arm). */
    em_weapon_reset(25, 27);
    draw_to_aim();
    frame(EM_PAD_L3, 0);
    frame(0, EM_PAD_L3);
    frames(2);
    check(em_weapon_state() == EM_WPN_RELOAD && em_weapon_mag() == 30,
          "top-up fill quirk: mag 25 / reserve 27 fills to 30");

    /* 3. DRY-MAG AUTO RELOAD at the cadence expiry — no second press. */
    em_weapon_reset(1, 50);
    draw_to_aim();
    reloads0 = em_weapon_reloads();
    frame(EM_PAD_CIRCLE, 0);
    frame(0, EM_PAD_CIRCLE);
    check(em_weapon_shots() >= 1 && em_weapon_mag() == 0,
          "dry run: the only round fired");
    frames(8);      /* expiry lands 5 ticks after the shot */
    check(em_weapon_state() == EM_WPN_RELOAD &&
          em_weapon_reloads() == reloads0 + 1 && em_weapon_mag() == 30,
          "empty mag auto-reloads at the cadence expiry (mode 1), "
          "without another trigger press");

    /* 4. FLASHLIGHT — SQUARE while aiming: a PERSISTENT preference,
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

    /* 5. AUTO parity: 31-frame hold = 6 rounds. */
    em_weapon_reset(30, 120);
    em_weapon_set_fire_mode(EM_WPN_MODE_AUTO);
    draw_to_aim();
    frame(EM_PAD_CIRCLE, 0);
    frames(30);
    frame(0, EM_PAD_CIRCLE);
    frames(10);
    check(em_weapon_shots() == 6, "auto: 31-frame hold = 6 rounds");
    em_weapon_set_fire_mode(EM_WPN_MODE_SEMI);

    printf("weapon fire test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
