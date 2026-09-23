/* em_player_damage.c — player damage, death and vitals.
 *
 * The player's damage pipeline: the lock gate, the infection and health
 * appliers, the flinch/death/struggle entries, the per-frame hurt and vitals
 * ticks. Split out of em_game.c, which had grown to hold the entire gameplay
 * frame. Behaviour is unchanged by the move — only the file boundary is new.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_player_damage.h"

#include "game/em_game_internal.h"
#include "game/em_pose_math.h"
#include "game/em_random.h"

/* Is the player in a hit reaction / dying / at the game-over screen?
 * (the movement + input + menu lock; also the producer-side immunity
 * gate together with pd_iframes — engine: every producer requires the
 * event byte == 1, which holds from reaction start until the +0x20E
 * window expires after the flinch). */
int player_damage_locked(void)
{
    return g.pd_state != 0 || g.go_state != 0;
}

/* func_0021C270 — apply pending INFECTION damage. */
static void player_apply_infection(void)
{
    if (g.pd_pend_inf == 0.0f) return;
    g.status.infection += g.pd_pend_inf;
    g.pd_pend_inf = 0.0f;
    g.pd_inf_hit  = 1;                     /* +0x1F1 = 1: voice select */
    if (g.status.infection >= 100.0f) {
        g.status.infection = 100.0f;
        if (g.status.health > PD_INFECTED_MAX)
            g.status.health = PD_INFECTED_MAX;
        if (!g.pd_infected) {
            g.pd_infected = 1;             /* +0x234 / 0x8104E4 latch */
            /* the display max swap to 60 — C14: the same +0x234 flag
             * drives the HUD's "/60" max (em_hud.h) */
            g.status.health_max = PD_INFECTED_MAX;
            em_sfx_play_at(PD_SFX_INFECTED, g.pos, 300.0f); /* engine
                                              * play_sound radius 300 */
        }
    }
}

/* func_0021C350 — apply pending HEALTH damage. */
static void player_apply_health(void)
{
    if (g.pd_pend_hp == 0.0f) return;
    g.status.health -= g.pd_pend_hp;
    g.pd_pend_hp = 0.0f;
    g.pd_inf_hit = 0;                      /* +0x1F1 = 0 */
    if (g.status.health <= PD_LOW_HEALTH)
        g.pd_low = 1;                      /* +0x235 |= 1 */
    if (g.status.health <= 0.0f) {
        g.status.health = 0.0f;
        /* event byte = 2 — the death branch runs in the processor */
    }
}

/* func_001B1470 — wrap to (-pi, pi]; -pi itself becomes +pi. The adds
 * and subtracts use the EE single-precision model (em_pose_math.h). */
static float flinch_wrap(float angle)
{
    while (angle > 3.1415927f)
        angle = pose_sub(angle, 6.2831855f);
    while (angle <= -3.1415927f)
        angle = pose_add(angle, 6.2831855f);
    return angle;
}

/* Player +0x70/+0x78, the hit vector 0021D1A0 reads. 001AF5C0 initializes
 * it to (0, 1); the original damage producers (e.g. 0012FC10 writes the
 * normalized player-minus-attacker XZ) overwrite it before posting a hit.
 * The port's producer (em_enemy_player_hit_take) passes only the hit code,
 * so no producer writes it yet and the side test below sees this spawn
 * value on every hit. */
static const float kFlinchHitVector[2] = { 0.0f, 1.0f };

/* func_0021D1A0 — unarmed flinch side test. The hit vector's heading
 * 001B1470(atan2(-z, x) + pi/2) minus body yaw +0xC4, wrapped again;
 * returns 1 when its magnitude exceeds pi/2 (c.le.s false), else 0. */
static unsigned player_flinch_side(const float hit[2], float yaw)
{
    float heading = flinch_wrap(pose_add(1.5707964f, atan2f(-hit[1], hit[0])));
    float diff = flinch_wrap(pose_sub(heading, yaw));
    return fabsf(diff) <= 1.5707964f ? 0u : 1u;
}

/* state 2 sub 0 phase 0 — FLINCH entry (func_0021D800 case 0): voice,
 * then ONE func_00122BB8 draw picks the clip family. Armed (+0x236)
 * wins over the infected latch (+0x234); only the unarmed, uninfected
 * arm consults 0021D1A0. Family bit 1 -> 0x56 / 0x1E|0x1F, bit 0 ->
 * 0x57 / 0x20|0x21 (side 1 takes the lower id). */
static void player_enter_flinch(void)
{
    int      armed = em_weapon_state() != EM_WPN_HOLSTERED;  /* +0x236 */
    unsigned clip;
    em_sfx_play_at(g.pd_inf_hit ? PD_SFX_HURT_INF : PD_SFX_HURT,
                   g.pos, 300.0f);            /* player-attached, r=300 */
    unsigned fam   = em_random_next() & 1u;   /* func_00122BB8() & 1 */
    if (armed) {
        clip = fam ? PD_CLIP_FLINCH_ARM_A : PD_CLIP_FLINCH_ARM_B;
    } else if (!g.pd_infected) {
        static int warned;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "player flinch: hit vector +0x70/+0x78 is not "
                    "supplied by the port's damage producers; 0021D1A0 "
                    "uses the 001AF5C0 spawn value (0,1)\n");
        }
        unsigned side = player_flinch_side(kFlinchHitVector, g.yaw);
        clip = fam ? (side ? PD_CLIP_FLINCH_A0 : PD_CLIP_FLINCH_A1)
                   : (side ? PD_CLIP_FLINCH_B0 : PD_CLIP_FLINCH_B1);
    } else {
        clip = PD_CLIP_FLINCH_INF;
    }
    /* 0021D600(+0x1F1 in {1,3,4}) suppresses a second voice, else
     * 0x146 (family 1) / 0x147 (family 0), r=300. The port's +0x1F1
     * (pd_inf_hit) carries only the 0 (health) / 1 (infection) values. */
    if (g.pd_inf_hit != 1)
        em_sfx_play_at(fam ? 0x146u : 0x147u, g.pos, 300.0f);
    if (!em_game_anim_request(clip, 1.0f))
        clip = 0;                  /* clip-less asset: timed fallback */
    g.pd_state = 2;
    g.pd_sub   = 0;
    g.pd_phase = clip ? 5 : 2;     /* 5 = wait commit; 2 = clip-less
                                    * timed fallback */
    g.pd_hold  = 0;
    g.pd_clip  = clip;
    /* rumble func_001B61C0(0, 0xC0, 5, 1) — no force-feedback backend */
}

/* state 2 sub 1/3 phase 0 — DEATH entry (func_0021E240 /
 * func_0021E830). The +0x1F0 = 0x40/0x3F write is only the category
 * marker — the sub handler requests the REAL clip itself. */
static void player_enter_death(void)
{
    int      armed = em_weapon_state() != EM_WPN_HOLSTERED;  /* +0x236 */
    unsigned clip  = g.pd_infected ? PD_CLIP_DEATH_INF
                   : armed         ? PD_CLIP_DEATH_ARM
                                   : PD_CLIP_DEATH;
    em_sfx_play_at(PD_SFX_DEATH_VOICE, g.pos, 300.0f); /* 0x146, r=300 */
    em_sfx_play_at(PD_SFX_DEATH_BODY,  g.pos, 300.0f); /* 0x151, r=300 */
    if (!em_game_anim_hold(clip, 1.0f))
        clip = 0;                  /* clip-less asset: straight to hold */
    g.pd_state    = 2;
    g.pd_sub      = g.pd_infected ? 3 : 1;
    g.pd_phase    = clip ? 5 : 2;  /* 5 = wait commit */
    g.pd_hold     = PD_CORPSE_HOLD;
    g.pd_clip     = clip;
    g.pd_cue_fall = 0;
    g.pd_cue_thud = 0;
    /* gore effect 0x80000051 (infected) — no effect system: flagged */
    printf("player death: clip 0x%X (%s)%s\n", clip,
           g.pd_infected ? "infected" : armed ? "armed" : "unarmed",
           clip ? "" : " — EMDL carries no death clip (timed fallback)");
}

/* func_0021C440 (generic tail) — the per-frame damage processor:
 * consume the pending amounts, then route to flinch or death. Called
 * once per gameplay frame after the producers ran. */
void player_damage_process(void)
{
    if (g.pd_pend_hp == 0.0f && g.pd_pend_inf == 0.0f) return;
    /* invulnerable: hit-reacting/dead, or inside the post-hit window
     * (engine: producers require event byte == 1) — drop the damage */
    if (player_damage_locked() || g.pd_iframes > 0) {
        g.pd_pend_hp = g.pd_pend_inf = 0.0f;
        return;
    }
    player_apply_health();           /* func_0021C350 */
    player_apply_infection();        /* func_0021C270 */
    if (g.status.health <= 0.0f)
        player_enter_death();
    else
        player_enter_flinch();
}

/* s76 BUG SHAKE-OFF (the latch reaction — func_002208C0 / state-2
 * sub-state 0xD): a bug bite LATCHES the bug onto the player (em_enemy
 * sub 5); while any cling, the player DRAINS, and when free he SHAKES
 * them off (clip 54), which detaches them all. The engine's 0x35->0x36
 * ->0x35 wrap is reduced to the recognisable 0x36 shake. */
static void player_enter_struggle(void)
{
    em_game_anim_hold(PD_CLIP_CLING, 1.0f);   /* 0x2C cling-idle pose */
    em_sfx_play_at(PD_SFX_SHAKE, g.pos, 300.0f);
    g.pd_state    = 2;
    g.pd_sub      = 5;               /* STRUGGLE (mash CROSS)            */
    g.pd_phase    = 1;              /* skip the wait-commit gate         */
    g.pd_hold     = 0;
    g.pd_clip     = PD_CLIP_CLING;
    g.struggle_n  = 0;
    g.status.health -= PD_LATCH_HIT;          /* 10 HP on connect (live) */
    if (g.status.health < 0.0f) g.status.health = 0.0f;
}

/* LIVE-VERIFIED 2026-06-12 (PCSX2): a clinging bug is shaken off by
 * MASHING CROSS, not automatically; while it clings the player loses
 * HEALTH and (the real killer) gains INFECTION fast; winning the mash
 * race throws the bug off and KILLS it, losing it -> infected death.
 * Rates/threshold are flagged PORT constants (need a live read —
 * FINDINGS "BUG LATCH / SHAKE-OFF — LIVE-VERIFIED"). */
void player_struggle_tick(void)
{
    int latched = em_enemy_latched_count();
    if (latched <= 0)
        return;
    /* DRAIN + INFECT while any bug clings (direct — the reaction locks
     * the normal damage pipeline). Infection maxing or health 0 = death. */
    if (g.go_state == 0 && g.pd_sub != 1 && g.pd_sub != 3) {
        g.status.infection += PD_LATCH_INFECT * (float)latched;
        if (g.status.infection > 100.0f) g.status.infection = 100.0f;
        g.status.health    -= PD_LATCH_DRAIN  * (float)latched;
        if (g.status.infection >= 100.0f && !g.pd_infected) {
            g.pd_infected       = 1;            /* infected latch (cap 60) */
            g.status.health_max = PD_INFECTED_MAX;
            em_sfx_play_at(PD_SFX_INFECTED, g.pos, 300.0f);
        }
        if (g.status.health <= 0.0f) {
            g.status.health = 0.0f;
            g.pd_state = 0;
            g.pd_phase = 0;
            em_enemy_shake_off();              /* the bugs go with you   */
            player_enter_death();
            return;
        }
    }
    /* enter the struggle the moment a bug first clings (engine: the
     * contact raises +0x0F=2 when the player is free / not i-framed). */
    if (g.pd_state == 0 && g.go_state == 0 && g.pd_iframes == 0)
        player_enter_struggle();
}

/* The state-2 per-frame tick (the port slice of func_0021D800 phase 1
 * / func_0021E240 / func_0021D2E0): watch the committed reaction clip,
 * fire the death-sequence sound cues, run the corpse hold, then the
 * fade-out into the game-over stand-in. Runs INSTEAD of player_move
 * (state 2 has no free-move spine). */
void player_hurt_tick(void)
{
    if (g.pd_phase == 5) {
        /* wait for the scripted-anim COMMIT (1-frame latency — the
         * engine requests land one update after the write too) */
        if (em_game_anim_active() == g.pd_clip)
            g.pd_phase = 1;
        return;
    }
    if (g.pd_sub == 0) {
        /* FLINCH: wait for the one-shot clip to play out (sa auto-
         * clears at clip end), then arm the i-frames and exit to
         * state 1 (the engine exits to sub 7 recover — the port's
         * locomotion idle is that pose). */
        int over = g.pd_phase == 2
                 ? ++g.pd_hold >= 30      /* clip-less fallback: ~30 f */
                 : em_game_anim_active() != g.pd_clip;
        if (over) {
            g.pd_state   = 0;
            g.pd_phase   = 0;
            g.pd_hold    = 0;
            g.pd_iframes = PD_IFRAMES;       /* +0x20E = 0x3C */
        }
        return;
    }
    if (g.pd_sub == 5) {
        /* STRUGGLE: mash CROSS to advance the counter (live: each press
         * advances it; no decay). At the threshold the bugs are thrown
         * off and DIE (em_enemy_shake_off). The cling pose (0x2C) holds;
         * each press plays the struggle flourish (0x2E). */
        const EmFrameInput *in = em_frame_input();
        if (in && (in->pressed & EM_PAD_CROSS)) {
            g.struggle_n++;
            em_game_anim_request(PD_CLIP_STRUGGLE, 1.2f);   /* 0x2E */
        } else if (em_game_anim_active() == 0) {
            em_game_anim_hold(PD_CLIP_CLING, 1.0f);          /* re-hold 0x2C */
        }
        if (g.struggle_n >= PD_STRUGGLE_WIN) {
            em_enemy_shake_off();              /* thrown off -> the bugs DIE */
            em_sfx_play_at(PD_SFX_THROWOFF, g.pos, 300.0f);  /* 0x14D */
            em_game_anim_request(PD_CLIP_THROWOFF, 1.0f);    /* 0x24 */
            g.pd_sub   = 6;
            g.pd_phase = 5;
            g.pd_clip  = PD_CLIP_THROWOFF;
            g.pd_hold  = 0;
        } else if (em_enemy_latched_count() == 0) {
            g.pd_state   = 0;                  /* bugs gone (shot off) -> exit */
            g.pd_phase   = 0;
            g.pd_iframes = PD_IFRAMES;
        }
        return;
    }
    if (g.pd_sub == 6) {
        /* THROW-OFF recover: the 0x24 clip plays out, then exit + invuln. */
        int over = g.pd_phase == 2
                 ? ++g.pd_hold >= 30
                 : em_game_anim_active() != g.pd_clip;
        if (over) {
            g.pd_state   = 0;
            g.pd_phase   = 0;
            g.pd_hold    = 0;
            g.pd_iframes = PD_IFRAMES;
        }
        return;
    }
    /* DEATH (sub 1 normal / 3 infected) */
    if (g.pd_phase == 1) {
        /* clip playing: frames-remaining sound cues (func_0021E240
         * phase 1 reads the anim clock +0x3C the same way) */
        int total = em_game_anim_frames(g.pd_clip);
        int cur   = em_game_anim_frame();
        int rem   = (cur >= 0 && total > 0) ? total - 1 - cur : -1;
        if (!g.pd_cue_fall && rem >= 0 && rem <= PD_DEATH_CUE_FALL) {
            g.pd_cue_fall = 1;
            em_sfx_play_at(PD_SFX_DEATH_FALL, g.pos, 300.0f); /* 0x156 */
        }
        if (!g.pd_cue_thud && rem >= 0 && rem <= PD_DEATH_CUE_THUD) {
            g.pd_cue_thud = 1;
            em_sfx_play_at(g.pd_infected ? PD_SFX_DEATH_THUD_I
                                         : PD_SFX_DEATH_THUD,
                           g.pos, 300.0f);
            /* + the heavy ground rumble func_001B61C0(1,0xEE,0x3C,1) */
        }
        if (rem <= 0) {
            /* clip done (held on the last frame — the corpse pose):
             * func_0021D2E0 phase 0. Blood-pool effect 0x80000043
             * under the corpse: skipped (no decal system), flagged. */
            g.pd_phase = 2;
        }
        return;
    }
    if (g.pd_phase == 2) {
        /* corpse hold (+0x28 = 0x78), then the standard fade-out —
         * func_001AEDE0(4,0), the door-transit machine. */
        if (--g.pd_hold <= 0) {
            g.pd_phase = 3;
            em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
            g.go_state = 1;
        }
        return;
    }
    /* phase 3: parked dead in the fade-out. The hand-off at full black
     * is the frame machine's since S11b: B9 was written by the player
     * stage (0015CF90) when health reached 0, and 0x1AE040 state 1 calls
     * 001AD140 once D_0028A9A0 == 2 (after the variant), which moves the
     * game task to the byte-matched 001AD4E0 game-over screen; the world
     * frame (this tick) no longer runs after that. */
}

/* Passive per-frame vitals (func_0015D100 infected arm + the spine's
 * kill plane + the +0x20E i-frame countdown).
 * RE-VERIFIED against src/func_0015D100.c (BYTE-MATCHED — authoritative):
 * both arms are gated on func_0021BB00(self) == 0 (entity not busy),
 * and the flag byte +0x234 (the INFECTED latch) picks the arm:
 *   +0x234 != 0  INFECTED: counter +0x2FC++ every frame; at >= 0xF0
 *                (240) reset it and, if health <= 2.0f, set event byte
 *                = 2, pending +0x224 = 2.0f and damage type +0xF = 0x63
 *                (the infected-drain death) WITHOUT subtracting;
 *                otherwise health -= 2.0f, effect 0x80000063, then the
 *                low-health latch +0x235 |= 1 at health <= 35.0f.
 *                (`health <= 2` then death is arithmetically identical
 *                to the port's `health -= 2; death if <= 0`.)
 *   +0x234 == 0  HAZARD-ROOM arm — UNTRANSLATED, but now pinned:
 *                gated on func_001B0070() & 4, D_008106C8 & 0x60 and
 *                D_00810C7E == 0; counter +0x300 at >= 0x168 (360, NOT
 *                240) drains health by 1.0f (not 2.0f), and at
 *                health <= 1.0f forces event 2 + pending 1.0f.
 * The func_0015D000 heartbeat rumble is untranslated (PD block doc).
 * RE-CONFIRMED (audit 2026-07-31): every constant above reads out of
 * src/func_0015D100.c literally — the 0xF0 / 0x168 periods, the 2.0f /
 * 1.0f drains, the `<= 2.0f` / `<= 1.0f` death gates, the +0xF = 0x63
 * type byte and the `<= 35.0f` +0x235 latch. */
void player_vitals_tick(void)
{
    if (g.pd_iframes > 0) g.pd_iframes--;
    if (player_damage_locked()) return;
    /* INFECTED passive drain: health -= 2.0 every 240 frames (the
     * green effect 0x80000063 per tick is skipped — no effect system,
     * flagged). Reaching 0 = the engine's event-2/type-0x63 death ->
     * the infected death sequence. */
    if (g.pd_infected && ++g.pd_drain_t >= PD_DRAIN_PERIOD) {
        g.pd_drain_t = 0;
        g.status.health -= PD_DRAIN_AMOUNT;
        if (g.status.health <= PD_LOW_HEALTH) g.pd_low = 1;
        if (g.status.health <= 0.0f) {
            g.status.health = 0.0f;
            player_enter_death();
            return;
        }
    }
    /* KILL PLANE (spine death check): Y < -200 -> engine state 6
     * (func_0015D460): zero health, fade, park — no anim, no sound. */
    if (g.pos[1] < PD_KILL_PLANE) {
        g.status.health = 0.0f;
        g.pd_state = 2;
        g.pd_sub   = 1;
        g.pd_phase = 3;            /* straight to the fade wait */
        g.pd_clip  = 0;
        em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
        g.go_state = 1;
    }
}
