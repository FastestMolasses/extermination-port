/* em_door.c — interactive door actors (see em_door.h for the engine
 * mapping, the FULL s22 transit sequence, and the flagged deviations).
 *
 * State machine (FINDINGS "FIRST INTERACTIVE OBJECTS" + "AREA TRANSITION
 * LIFECYCLE" s22, func_001BC350 RUN sub-states, engine numbering kept):
 *
 *   0 CLOSED   armed (+0x0B != 0, by the use scan or a neighbor panel)
 *              -> LOCK GATE (model 0x15 vs D_00810841 — natively the
 *              manifest `locked` token vs em_door_unlock) -> transit
 *              kickoff (func_001BBE40: SIDE LATCH +0x2E, INPUT LOCK,
 *              walk-to the staging point door + 5*n) -> 3, or kickoff
 *              mode 1 -> 1 when the gate refuses.
 *   1 LOCKED   the LOCKED TRY script D_0024DEC0 (em_door.h "THE LOCKED
 *              SEQUENCE"): locked-look camera cut, player try anim
 *              0x46/0x44, lock-fixture jiggle clip 3/1, rattle 0x3F2 at
 *              the 60-frame mark + the radio/examine message line 6
 *              (em_hud_radio, 118 f), wait message done + clip end
 *              -> 2. No fade, no warp — the door stays shut.
 *   2 LOCKED'  finish script D_0024DBC0 ran (op07 sub4: camera +
 *              control restored at the 1->2 edge) -> re-arm, 0.
 *   3 OPENING  walk-to arrival, then the OPEN script D_0024DE40
 *              (FINDINGS "DOOR SCRIPTS DECODED" s23): player anim
 *              0x45 front / 0x43 back at rate 1.0 (op 0x0A sub 0, via
 *              em_game_anim_request), door sound + clip start (op 0x0B
 *              sub 6; pump func_001BC0E0 advances 1.0/frame), wait 90
 *              front / 70 back frames (op 0x02 STOP) -> 4
 *   4 OPEN     one-frame transition COMMIT (func_001BC240 ->
 *              func_001BC150): arm the 64-frame fade-out
 *              (func_001AEDE0(4,0)); room moves do NOT fade audio -> 5
 *   5 CLOSING  engine sub 5 = transition pending. At fade-out complete
 *              (screen black): post the RE-PLACE (spawn point behind the
 *              door, exit yaw), arm the 64-frame fade-in + the ARRIVAL
 *              WALK-OUT (player state 5/1 — em_door.h step 4), and start
 *              running the clip back (the engine re-arms when the
 *              request byte B8 clears, right after the re-place). The
 *              MENU unlocks when the fade-in completes; MOVEMENT when
 *              the walk-out phases end (~111 frames — the two-lock
 *              split, em_door.h "THE TWO LOCKS"); at clip rest the door
 *              re-arms (+0x0B = 0) -> 0
 *
 * SLIDERS (m17/m09) run the decoded variant brain func_001BB860
 * instead — same CROSS use-arm (the one engine trigger, s58), native
 * slide, scripted walk-through, no fade, no player anim; see the
 * "SLIDER (m17/m09) VARIANT BRAIN" block below and em_door.h.
 *
 * Articulation: the engine evaluates a keyframe clip on the door's bone
 * slots (func_001BC300 -> func_001C68C0). The shipped door EMDLs carry
 * the real disc clips (s30/s32, slot-0x39 bank ids [0,2,1,3]); a
 * single-frame EMDL falls back to the legacy PLACEHOLDER hinge swing
 * below (90 degrees about the placement origin's Y axis — the panel's
 * hinge edge sits at local x = 0). A real baked clip (frame_count > 1)
 * plays at 1.0 frame/tick, exactly the engine rate.
 */
#include "game/em_door.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"   /* EM_PAD_CROSS — the frame input button mask */
#include "em_model.h"
#include "game/em_game.h"   /* scripted player anim (op 0x0A sub 0) */
#include "game/em_hud.h"    /* em_hud_radio — the locked "VO" text */
#include "game/em_sfx.h"

#define DOOR_MAX        EM_DOOR_MAX
#define DOOR_MODEL_MAX  4
#define DOOR_BONE_MAX   16    /* palette slots incl. the EMDL identity slot */
#define DOOR_PI         3.14159265f

/* Use-scan constants — func_00183EF0 CLASS-5 path, fully read 2026-06-11
 * (FINDINGS "DOOR USE SCAN + STAGING MATH"; retires the s17 class-7
 * extrapolation). For door models 3/0x15 (hinged m03 family — the
 * placement origin is the panel's HINGE corner) the scan measures from
 * the DOORWAY CENTER, 5 u from the hinge toward the free/handle edge:
 *
 *     center = door_pos + 5 * (-cos(yaw), 0, +sin(yaw))
 *
 * (other models — m17/m09 sliders whose origin is already the doorway
 * center — use door_pos directly). Conditions, in engine order:
 *     horizontal dist(player, center) <= desc[0] = 10.0   (D_002755F0)
 *     |player_y - door_y|             <= desc[1] =  8.0
 *     side:   |norm(atan2(player - door_pos) - yaw)| <= pi/2 -> front
 *     facing: |norm(player_yaw + (front ? pi : 0) - yaw)| <= pi/4
 * No LOS query and no 2-u auto ring exist in the class-5 path (both were
 * the class-7 prefix) — the old port LOS "doorway pocket" exemption and
 * the auto ring are retired. The manifest radius carries desc[0]. */
#define DOOR_CENTER_OFF   5.0f   /* hinge -> doorway center, m03/m15 only */
#define DOOR_VERT_LIMIT   8.0f   /* desc[1] (D_002755F0[1]) */
#define DOOR_FACING_ANG   (DOOR_PI * 0.25f)   /* pi/4 yaw window */

/* PLACEHOLDER swing timing — flagged: the real clip length is unknown.
 * The engine advances its clip 1.0/frame and the two live-captured
 * transits ran 97 (room move) and 77 (area change) vsyncs of clip; 90
 * frames sits inside that captured window. */
#define DOOR_SWING_FRAMES 90.0f
#define DOOR_SWING_ANGLE  (DOOR_PI * 0.5f)

/* SLIDER (m17/m09) VARIANT BRAIN — func_001BB860, decoded 2026-06-11
 * (closes the s32 "variant lifecycle unread" flag; replaces the m03
 * stand-in for these models):
 *
 *   TRIGGER  the same +0x0B-bit-2 use-arm as the m03 family — the use
 *            scan func_00184BA0 runs ONLY on the USE-button press edge
 *            (every caller gates on D_00810E74 & spad-0x70003B76 =
 *            CROSS, decoded 2026-06-11 s58 — see door_trigger_scan),
 *            so sliders arm on CROSS inside the class-5 window exactly
 *            like hinged doors. The s56 "walk-into, no button" reading
 *            is OVERTURNED.
 *   KICKOFF  the trigger sub func_001BB560: side latch +0x2E from the
 *            door->player bearing vs the door yaw (flags2 8/0x16 invert
 *            it — single-leaf variants mount reversed), player yaw
 *            SNAPPED through the door, player SNAPPED (func_00182F90
 *            instant translate) to the staging point door_pos - 6.0 *
 *            (sin, cos)(snapped yaw) — 6.0, not the m03 family's 5.0;
 *            the port walks the same point via the MOVE-TO, the
 *            established m03 deviation. Lock-gated placements (flags2
 *            0x16/0x17/0x3E + D_00810841 bit clear) queue the LOCKED
 *            script D_0024DA40 instead (camera + the m03 locked VO,
 *            NO motion — sliders have no locked-jiggle clip; not in
 *            the port: no lock bitmask yet, flagged).
 *   OPEN     script D_0024D900: op07 sub0 scripted-mode enter (input
 *            lock, NO fade), op0D sub5 chase-camera cue (not owned by
 *            em_door — port camera keeps chasing, flagged), op17 sub0
 *            positional door sound, op09 = func_001BB400 NATIVE SLIDE
 *            (bone +0x7C keyed translation, 0.2 u/frame until the
 *            panels part 9.0 u — the port EMDL bakes exactly that as
 *            its 46-frame clip, pumped 1.0/frame), then op01 sub8 =
 *            scripted player WALK-THROUGH (func_001B94F0 move-to,
 *            walk clip — the player crosses the open doorway; NO
 *            player door-gesture anim anywhere in the script).
 *   AFTER    brain state 3 = the transition COMMIT (func_001BC150,
 *            dest table — the engine re-places/area-changes), state 4
 *            waits + re-arms. The port: goto sliders fade + scene-
 *            switch at walk-through end (the m03 commit path); plain
 *            sliders stay OPEN — the engine re-closes via room
 *            re-entry state, so the port re-closes by running the
 *            slide clip backwards when the player leaves the scan
 *            radius (the s32-flagged reverse, motion-identical). */
#define SLIDER_POINT_DIST 6.0f  /* func_001BB560 staging: 6.0 * (sin,cos) */
#define SLIDER_LEAVE_PAD  2.0f  /* re-close hysteresis past the radius */

/* Staging/spawn offset along the door normal: the engine stages the
 * player at CENTER +- 5.0 * n on his own side. Decoded exactly from
 * func_001BBE40 (2026-06-11; both s22 live captures reproduce to the
 * digit — (104, -247.2) and (62, -225.5)):
 *
 *     pyaw = norm(door_yaw + (front ? pi : 0))      (the yaw snap)
 *     sx = door_x - 5*cos(door_yaw) - 5*sin(pyaw)
 *     sy = player_y
 *     sz = door_z + 5*sin(door_yaw) - 5*cos(pyaw)
 *
 * = doorway CENTER (the same 5-u hinge->handle lateral term as the use
 * scan) + 5 u out of the door plane on the player's side. NOTE the
 * decomp stub src/func_001BBE40.c annotated its trig externs swapped;
 * func_0011DE90 = cosf, func_0011E2A8 = sinf (pinned by the captures
 * AND by func_00136630's forward-step x += v*E2A8 / z += v*DE90 with
 * the engine's forward = (sin yaw, cos yaw)). The spawn-table records
 * flank the CENTER at ~+-5 with exit yaw (office rec 2/3 = (104, -245)/
 * (104, -259) vs center (104, -252.2)). */
#define DOOR_POINT_DIST   5.0f

/* OPEN-phase script values (FINDINGS "DOOR SCRIPTS DECODED" s23 — the
 * D_0024DE40 open script, records patched by side at kickoff):
 *  - player anim id (op 0x0A sub 0, rate 1.0): 0x45 front / 0x43 back
 *    — the reach-out/walk-through clips, played through the scripted-
 *    anim mailbox (em_game_anim_request; id == library container).
 *  - phase duration (the op 0x02 STOP wait): 90.0 front / 70.0 back
 *    frames, then the script ends and the transition COMMIT runs.
 * SIDE: the s17 front test — bearing(player - door) within pi/2 of the
 * door yaw (i.e. the player stands on the side the door faces, +n). */
#define DOOR_ANIM_OPEN_FRONT  0x45
#define DOOR_ANIM_OPEN_BACK   0x43
#define DOOR_WAIT_FRONT       90.0f
#define DOOR_WAIT_BACK        70.0f

/* LOCKED-TRY script values (em_door.h "THE LOCKED SEQUENCE" — script
 * D_0024DEC0, FINDINGS s23; clip/anim identities re-verified by the
 * 2026-06-11 directory bake):
 *  - player anim id (op 0x0A sub 0, rate 1.0): 0x46 front / 0x44 back
 *    — the try-the-handle-and-fail gestures (200 f, return to rest).
 *  - door clip ENGINE id (op 0x0B sub 0, no sound): 3 front / 1 back
 *    — the lock-fixture jiggle (s30; the EMDLs carry the real clips,
 *    resolved per id through em_model_clip_index).
 *  - the op 0x02 wait before the rattle record: 60 frames (the fixture
 *    motion peaks f60-110 — the sound lands on the shake).
 *  - locked rattle sound id 0x3F2 (op 0x17 sub 0; em_sfx.h).
 *  - locked "VO" = the RADIO/EXAMINE MESSAGE machine (em_hud_radio;
 *    FINDINGS "RADIO-MESSAGE MACHINE DECODED" 2026-06-11): the op09
 *    native func_001BBAE0 starts engine mode 2 on the GLOBAL line
 *    selected by the door LINK's low 6 bits via jtbl_0026E1A0 (sel
 *    0..5 -> lines 6/0/2/8/0xA/4). BOTH shipped lock-gated doors
 *    (office0/drawbridge m15, links 0x0200) carry sel 0 -> line 6
 *    "It's locked and won't open." (118 frames, centered text, no
 *    audio). The native is PUMPED until the message machine reports
 *    done, so the locked script cannot finish before the text clears
 *    — the port blocks its finish edge on em_hud_radio_active() the
 *    same way. The voice-cue field of every global record is -1; the
 *    optional scene.txt `lockedvo <id-hex>` audio slot stays honored
 *    if the registry ever resolves a real cue (none do).
 *  - sliders' locked script D_0024DA40 = camera + wait 40 + the SAME
 *    VO native + exit (s56) — no exported locked slider exists; the
 *    port approximates with the hinged flow minus anim/clip/rattle
 *    (FLAGGED), with the real wait-40 + message-duration timing. */
#define DOOR_ANIM_LOCK_FRONT  0x46
#define DOOR_ANIM_LOCK_BACK   0x44
#define DOOR_CLIP_OPEN_FRONT  2      /* engine clip ids (op 0x0B sub 6) */
#define DOOR_CLIP_OPEN_BACK   0
#define DOOR_CLIP_LOCK_FRONT  3      /* engine clip ids (op 0x0B sub 0) */
#define DOOR_CLIP_LOCK_BACK   1
#define DOOR_LOCK_RATTLE_AT   60.0f  /* the op 0x02 wait before 0x3F2 */
#define DOOR_LOCK_SLIDER_WAIT 40.0f  /* D_0024DA40's op 0x02 wait */
#define DOOR_VO_KEYWORD       "lockedvo"
#define DOOR_RADIO_LINE       6      /* jtbl_0026E1A0 sel 0 (link low 6
                                      * bits = 0 on both shipped locked
                                      * doors) -> GLOBAL examine line 6,
                                      * "It's locked and won't open." */

/* Door SOUND pair — FINDINGS "DOOR SCRIPTS DECODED" s23: the open
 * script's op 0x0B sub 6 record is patched by func_001BBD60 with
 * D_0024DB80[link >> 8][side], a [front_id, back_id] halfword pair
 * table indexed by the placement LINK halfword's high byte (door
 * family ids 0x3FB..0x40E). The port's manifest does not carry the
 * per-door link yet (export_props owns the door lines), so the pair
 * arrives as ONE optional GLOBAL scene.txt line, generated alongside
 * the registry by the decomp repo's tools/gen_sfx_registry.py:
 *
 *     doorsfx <front-id> <back-id>     (e.g. office: doorsfx 0x3FD 0x3FE
 *                                       = D_0024DB80[2], both office
 *                                       doors' links are 0x02xx)
 *
 * em_game's manifest parser skips unknown keywords, so em_door scans
 * scene.txt itself (once, at the first em_door_add). With the line
 * present the open chain plays the side-correct pair id exactly like
 * the engine record, and the legacy close-at-black placeholder play is
 * dropped (the decoded open script D_0024DE40 carries a SINGLE sound
 * record; no close sound is decoded — func_001BBD20 is a possible
 * close path, revisit). Without the line, the previous PLACEHOLDER
 * behavior is preserved bit-for-bit (EM_SFX_DOOR_OPEN/CLOSE, which an
 * unmapped registry turns into silent no-ops). FLAGGED simplification
 * until the manifest door lines grow the link halfword. */
#define DOOR_SFX_KEYWORD  "doorsfx"

/* Fade speed for the transit fades — the captured func_001AEDE0 speed
 * (4 -> 64-frame ramp), see em_frame.h. */
#define DOOR_FADE_SPEED   EM_FADE_SPEED_DOOR

/* ARRIVAL WALK-OUT constants — func_00183250 (player state 5/1), all
 * engine values (em_door.h step 4): phase frame counts (+0x28 timer
 * loads), the locIdx-2 locomotion speed (D_00248870[2], u/tick) and the
 * phase-3 ramp decay step (0x3C3A2E8C). ~111 frames, ~12.8 u total. */
#define WALKOUT_PHASE1_FRAMES 50      /* 0x32: clip only, no translation */
#define WALKOUT_PHASE2_FRAMES 30      /* 0x1E: mover at full ramp        */
#define WALKOUT_PHASE3_FRAMES 30      /* 0x1E: mover while ramp decays   */
#define WALKOUT_SPEED_UPT     0.3f    /* u/tick — engine +0x38 init      */
#define WALKOUT_RAMP_STEP     0.0113636f  /* 0x3C3A2E8C, per frame       */
#define WALKOUT_TICK_HZ       60.0f   /* u/tick -> u/sec for the port    */

typedef struct {
    char       path[512];
    EmModel    model;
    EmGfxMesh *mesh;
    int        used;
    /* closed-pose (frame 0) palette + door-local AABB of the posed mesh */
    float      base[DOOR_BONE_MAX * 16];
    float      lo[3], hi[3];
    int        has_clip;     /* frame_count > 1: real baked clip present */
    int        slider;       /* engine model byte 0x09/0x17: the sliding-
                              * door family — variant brain func_001BB860
                              * (CROSS use-arm, native slide, scripted
                              * walk-through; see the SLIDER block above).
                              * Parsed from the door_mXX filename like
                              * `hinged` (FLAGGED: filename convention). */
    int        hinged;       /* engine model byte 3/0x15 (placement origin
                              * = the hinge corner): use scan + staging
                              * measure from the doorway CENTER, 5 u along
                              * the panel (func_00183EF0 class-5 branch /
                              * func_001BBE40). Parsed from the exporter's
                              * doors/door_mXX.emdl name — the manifest's
                              * model-byte carrier (FLAGGED: filename
                              * convention, not a record field). */
} DoorModel;

typedef struct {
    int      model;          /* index into s.models */
    float    pos[3];         /* placement (actor +0xB0) */
    float    yaw;            /* placement ry (actor +0xC4) */
    float    center[3];      /* doorway CENTER: pos + 5*(-cos,0,+sin)(yaw)
                              * for hinged models, else == pos — the use
                              * scan + staging reference point */
    float    radius;         /* use-scan distance, desc[0] = 10.0 */
    uint8_t  state;          /* actor +0x05 sub-state (engine values) */
    uint8_t  armed;          /* actor +0x0B activation flags (scan: 4) */
    int      slider;         /* model is the m17/m09 slider family —
                              * runs the func_001BB860 variant flow */
    int      sl_phase;       /* slider OPENING sub-phase: 0 = staging
                              * walk, 1 = native slide pump, 2 =
                              * scripted walk-through */
    int      lock_gated;     /* manifest `locked` token — the decoded
                              * D_00810841 gate (em_door.h "THE LOCKED
                              * SEQUENCE") */
    int      unlocked;       /* em_door_unlock ran — the area unlock
                              * bit is set; the gate passes */
    int      lk_look;        /* locked-try script holds the locked-look
                              * camera (arrival .. finish) */
    int      lk_fired;       /* locked rattle + VO records ran (the
                              * one-shot at the 60-frame mark) */
    int      clip_idx;       /* EMDL clip INDEX in play (engine clip id
                              * resolved via em_model_clip_index at
                              * sequence start; 0 = the closed pose) */
    float    clip_t;         /* anim block +0xE clip time, frames */
    int      transit;        /* walk-to MOVE-TO active (func_001BBE40) */
    float    transit_to[3];  /* STAGING point door_pos + 5.0 * n, near side */
    float    transit_yaw;    /* player yaw snapped to the door normal
                              * (travel direction = the exit yaw) */
    int      front;          /* side latch (actor +0x2E): 1 = front (the
                              * +n side the door faces), 0 = back */
    float    open_wait;      /* scripted open-phase length, frames (the
                              * op 0x02 wait: 90 front / 70 back) */
    float    phase_t;        /* open-phase frame counter */
    int      anim_started;   /* open-phase script chain fired (player
                              * anim + door clip + sound, one-shot) */
    float    spawn_pt[3];    /* re-place point door_pos - 5.0 * n, far side
                              * (the spawn-table record's pose) */
    /* manifest goto tail (decoded dest+spawn tables — em_door.h): the
     * commit switches scenes instead of the same-scene re-place. */
    int      has_goto;
    char     goto_dir[64];   /* target scene dir (sibling name or path) */
    float    goto_pos[3];    /* arrival spawn record: pos */
    float    goto_yaw;       /*                       exit yaw */
    int      did_warp;       /* sub 5: re-place already posted this transit */
    float    aabb_lo[3];     /* world AABB of the CLOSED door (hull box) */
    float    aabb_hi[3];
    float    palette[DOOR_BONE_MAX * 16];
} Door;

static struct {
    DoorModel models[DOOR_MODEL_MAX];
    int       n_models;
    Door      doors[DOOR_MAX];
    int       n_doors;
    /* transit-wide state (one transit at a time, like the engine's
     * single B5..B8 request block). THE TWO LOCKS (em_door.h "THE TWO
     * LOCKS" — decoded 2026-06-11, separate engine systems):
     *   lock_move — player movement/actions (the engine's scripted mode
     *               + player state 5/1): kickoff .. walk-out end.
     *   lock_menu — the status-screen toggle (func_001AE7E0's gates:
     *               pending request / fade machine D_0028A9A0 != 0 /
     *               spad 3B8D): kickoff .. fade-in completion. */
    int       lock_move;
    int       lock_menu;
    int       unlock_armed;  /* re-place posted: menu unlock at fade-in end */
    int       warp_pending;  /* one-shot re-place request for em_game */
    float     warp_pos[3];
    float     warp_yaw;
    /* ARRIVAL WALK-OUT (engine player state 5/1, func_00183250 — the
     * em_door.h step-4 decode). Phases tick in em_door_update from the
     * re-place post; em_game player_move consumes the per-frame command
     * via em_door_walkout_active. Survives em_door_scene_clear. */
    int       wo_phase;      /* 0 idle; 1/2/3 = the engine +0x06 phases */
    int       wo_t;          /* frames left in the current phase */
    float     wo_yaw;        /* walk direction = the spawn exit yaw */
    float     wo_ramp;       /* phase-3 speed ramp (+0x38), u/tick */
    /* one-shot scene-switch request (goto doors; warp_pos/yaw carry the
     * arrival spawn) */
    int       goto_pending;
    char      goto_dir[64];
    /* door sound pair (see DOOR_SFX_KEYWORD above) */
    int       sfx_scanned;   /* scene.txt scanned once for doorsfx */
    int       sfx_real;      /* doorsfx line found: engine pair active */
    unsigned  sfx_pair[2];   /* D_0024DB80 pair [0]=front, [1]=back */
    /* optional locked-VO id (see DOOR_VO_KEYWORD — only emitted by the
     * registry generator if a locked line ever resolves to real audio;
     * the shipped locked doors are TEXT-ONLY radio messages) */
    int       vo_real;
    unsigned  vo_id;
    int       rattles;       /* locked-rattle plays (introspection) */
} s;

static void door_build_palette(Door *d);

void em_door_reset(void)
{
    /* Models/meshes survive a reset only through shutdown (boot calls
     * reset exactly once before adding; em_game_shutdown frees). */
    memset(&s, 0, sizeof s);
}

/* ------------------------------------------------------------------ */
/* Loading                                                              */
/* ------------------------------------------------------------------ */

/* One-shot scan of <scene_dir>/scene.txt for the optional global sound
 * lines (see DOOR_SFX_KEYWORD / DOOR_VO_KEYWORD). em_door owns these
 * keywords; em_game's parser skips lines it does not know. */
static void door_sfx_manifest_scan(const char *scene_dir)
{
    if (s.sfx_scanned) return;
    s.sfx_scanned = 1;

    char path[512];
    snprintf(path, sizeof path, "%s/scene.txt", scene_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    unsigned front, back, vo;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (!s.sfx_real &&
            sscanf(line, DOOR_SFX_KEYWORD " %x %x", &front, &back) == 2) {
            s.sfx_pair[0] = front;   /* D_0024DB80 pair[link>>8][0] */
            s.sfx_pair[1] = back;    /*                        [1] */
            s.sfx_real    = 1;
            printf("door sfx: manifest pair front 0x%03X / back 0x%03X "
                   "(D_0024DB80)\n", front, back);
        } else if (!s.vo_real &&
                   sscanf(line, DOOR_VO_KEYWORD " %x", &vo) == 1) {
            s.vo_id   = vo;          /* a real locked-VO audio cue */
            s.vo_real = 1;
            printf("door sfx: manifest locked-VO id 0x%03X\n", vo);
        }
    }
    fclose(f);
}

static int door_model_get(EmGfx *gfx, const char *scene_dir,
                          const char *file)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", scene_dir, file);
    for (int i = 0; i < s.n_models; i++)
        if (strcmp(s.models[i].path, path) == 0)
            return i;
    if (s.n_models >= DOOR_MODEL_MAX) return -1;

    DoorModel *dm = &s.models[s.n_models];
    if (em_model_load(&dm->model, path) != 0) return -1;
    if (dm->model.bone_count > DOOR_BONE_MAX) {
        fprintf(stderr, "door: %s: %u bones > %d\n", path,
                dm->model.bone_count, DOOR_BONE_MAX);
        em_model_free(&dm->model);
        return -1;
    }
    dm->mesh = em_gfx_mesh_create(gfx, dm->model.verts,
                                  dm->model.vert_count, dm->model.indices,
                                  dm->model.index_count,
                                  (const EmGfxTexDesc *)dm->model.texs,
                                  dm->model.tex_count, dm->model.texels,
                                  dm->model.flags);
    if (!dm->mesh) {
        em_model_free(&dm->model);
        return -1;
    }
    snprintf(dm->path, sizeof dm->path, "%s", path);

    /* Closed pose: frame 0 of clip 0 (the exporter's captured pose). */
    em_model_palette_at(&dm->model, 0, 0.0, dm->base);
    dm->has_clip = dm->model.frame_count > 1;

    /* Engine model byte from the exporter's doors/door_mXX.emdl name:
     * func_00183EF0's class-5 branch keys the doorway-center offset on
     * model == 3 || model == 0x15 (the hinged m03 family; their
     * placement origin is the hinge corner). Unparseable names default
     * to hinged=0 (center == pos — correct for the m17/m09 sliders,
     * whose origin is the doorway center). */
    dm->hinged = 0;
    dm->slider = 0;
    const char *m = strstr(file, "_m");
    if (m) {
        unsigned mb = (unsigned)strtoul(m + 2, NULL, 16);
        dm->hinged = (mb == 0x03 || mb == 0x15);
        dm->slider = (mb == 0x09 || mb == 0x17);
    }

    /* Door-local AABB of the POSED closed mesh (palette * position) —
     * the blocking hull (the engine's per-uid collision-record AABB). */
    dm->lo[0] = dm->lo[1] = dm->lo[2] =  1e9f;
    dm->hi[0] = dm->hi[1] = dm->hi[2] = -1e9f;
    for (uint32_t v = 0; v < dm->model.vert_count; v++) {
        const float *vert = dm->model.verts + v * EM_MODEL_VERT_WORDS;
        uint32_t bone_word;
        memcpy(&bone_word, dm->model.verts + v * EM_MODEL_VERT_WORDS + 8,
               sizeof bone_word);
        uint32_t bone = bone_word & EM_MODEL_VERT_BONE_MASK;
        if (bone >= dm->model.bone_count) bone = dm->model.bone_count - 1;
        const float *m = dm->base + bone * 16;
        for (int k = 0; k < 3; k++) {
            float p = m[0 + k] * vert[0] + m[4 + k] * vert[1]
                    + m[8 + k] * vert[2] + m[12 + k];
            if (p < dm->lo[k]) dm->lo[k] = p;
            if (p > dm->hi[k]) dm->hi[k] = p;
        }
    }

    printf("door model: %s — %u verts, %u tris, %u bones, %s, local box "
           "(%.1f, %.1f, %.1f)..(%.1f, %.1f, %.1f)\n", path,
           dm->model.vert_count, dm->model.index_count / 3,
           dm->model.bone_count,
           dm->has_clip ? "baked clip" : "PLACEHOLDER swing (no clip yet)",
           dm->lo[0], dm->lo[1], dm->lo[2], dm->hi[0], dm->hi[1], dm->hi[2]);
    return s.n_models++;
}

int em_door_add(EmGfx *gfx, const char *scene_dir, const char *file,
                const float pos[3], float yaw, float radius)
{
    if (s.n_doors >= DOOR_MAX) return -1;
    door_sfx_manifest_scan(scene_dir);
    int mi = door_model_get(gfx, scene_dir, file);
    if (mi < 0) return -1;

    Door *d = &s.doors[s.n_doors];
    memset(d, 0, sizeof *d);
    d->model  = mi;
    d->pos[0] = pos[0];
    d->pos[1] = pos[1];
    d->pos[2] = pos[2];
    d->yaw    = yaw;
    d->radius = radius;
    d->state  = EM_DOOR_CLOSED;
    d->slider = s.models[mi].slider;

    /* Doorway CENTER — the use-scan + staging reference (func_00183EF0
     * class-5 / func_001BBE40 shared lateral term): 5 u from the hinge
     * along the panel toward the free edge for hinged models. */
    d->center[0] = d->pos[0];
    d->center[1] = d->pos[1];
    d->center[2] = d->pos[2];
    if (s.models[mi].hinged) {
        d->center[0] -= DOOR_CENTER_OFF * cosf(yaw);
        d->center[2] += DOOR_CENTER_OFF * sinf(yaw);
    }

    /* World AABB of the closed door: rotate the local box by yaw (about
     * the placement origin) and take the axis-aligned bounds. */
    const DoorModel *dm = &s.models[mi];
    const float c = cosf(yaw), sn = sinf(yaw);
    d->aabb_lo[0] = d->aabb_lo[2] =  1e9f;
    d->aabb_hi[0] = d->aabb_hi[2] = -1e9f;
    for (int i = 0; i < 4; i++) {
        float lx = (i & 1) ? dm->hi[0] : dm->lo[0];
        float lz = (i & 2) ? dm->hi[2] : dm->lo[2];
        float wx =  c * lx + sn * lz;
        float wz = -sn * lx + c * lz;
        if (wx < d->aabb_lo[0]) d->aabb_lo[0] = wx;
        if (wx > d->aabb_hi[0]) d->aabb_hi[0] = wx;
        if (wz < d->aabb_lo[2]) d->aabb_lo[2] = wz;
        if (wz > d->aabb_hi[2]) d->aabb_hi[2] = wz;
    }
    for (int k = 0; k < 3; k += 2) {
        d->aabb_lo[k] += d->pos[k];
        d->aabb_hi[k] += d->pos[k];
    }
    d->aabb_lo[1] = d->pos[1] + dm->lo[1];
    d->aabb_hi[1] = d->pos[1] + dm->hi[1];

    /* Closed pose now: a door added mid-frame (scene switch while black)
     * is draw-recorded before its first em_door_update pass. */
    door_build_palette(d);

    printf("door %d: %s at (%.1f, %.1f, %.1f) yaw %.3f r %.1f center "
           "(%.1f, %.1f)%s — hull (%.1f, %.1f, %.1f)..(%.1f, %.1f, %.1f)\n",
           s.n_doors, file, pos[0], pos[1], pos[2], yaw, radius,
           d->center[0], d->center[2],
           s.models[mi].hinged ? " (hinged: +5 off the hinge)" : "",
           d->aabb_lo[0], d->aabb_lo[1], d->aabb_lo[2],
           d->aabb_hi[0], d->aabb_hi[1], d->aabb_hi[2]);
    s.n_doors++;
    return 0;
}

int em_door_set_goto(int i, const char *target, const float spawn[3],
                     float spawn_yaw)
{
    if (i < 0 || i >= s.n_doors || !target || !target[0])
        return -1;
    Door *d = &s.doors[i];
    snprintf(d->goto_dir, sizeof d->goto_dir, "%s", target);
    d->goto_pos[0] = spawn[0];
    d->goto_pos[1] = spawn[1];
    d->goto_pos[2] = spawn[2];
    d->goto_yaw    = spawn_yaw;
    d->has_goto    = 1;
    printf("door %d: goto %s spawn (%.1f, %.1f, %.1f) yaw %.4f\n", i,
           target, spawn[0], spawn[1], spawn[2], spawn_yaw);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Articulation palette                                                 */
/* ------------------------------------------------------------------ */

/* Open fraction 0..1 from the clip time. */
static float door_open_frac(const Door *d)
{
    const DoorModel *dm = &s.models[d->model];
    float total = dm->has_clip ? (float)(dm->model.frame_count - 1)
                               : DOOR_SWING_FRAMES;
    float f = d->clip_t / total;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

static float door_clip_total(const Door *d)
{
    const DoorModel *dm = &s.models[d->model];
    if (!dm->has_clip)
        return DOOR_SWING_FRAMES;
    uint32_t ci = (d->clip_idx > 0 &&
                   (uint32_t)d->clip_idx < dm->model.clip_count)
                  ? (uint32_t)d->clip_idx : 0;
    return (float)(dm->model.clips[ci].frame_count - 1);
}

/* op 0x0B clip select — bind the ENGINE clip id (patched per side by
 * func_001BBE40: open 2/0, locked 3/1) through the EMDL's clip table.
 * Single-clip / placeholder EMDLs (no such id) fall back to clip 0 —
 * the previous fixed-clip behavior. */
static void door_clip_select(Door *d, unsigned engine_id)
{
    const DoorModel *dm = &s.models[d->model];
    int ci = dm->has_clip
             ? em_model_clip_index(&dm->model, (uint32_t)engine_id) : -1;
    d->clip_idx = ci < 0 ? 0 : ci;
    d->clip_t   = 0.0f;
}

/* Build the door's world palette: clip pose (real clip, or the flagged
 * placeholder hinge swing applied to the closed pose), then the
 * placement compose T(pos) * R_y(yaw) — the same composition em_game.c
 * uses for the player (palette_apply_placement). */
static void door_build_palette(Door *d)
{
    DoorModel *dm = &s.models[d->model];
    uint32_t n = dm->model.bone_count;

    if (dm->has_clip) {
        /* Engine path: evaluate the SELECTED clip at the current time
         * (clip_idx = the op 0x0B engine-id resolution; 0 until a
         * sequence binds one — frame 0 of every door clip is the
         * captured closed pose, s30). */
        em_model_palette_at(&dm->model, (uint32_t)d->clip_idx,
                            (double)d->clip_t, d->palette);
    } else {
        /* PLACEHOLDER (no disc clip located yet — see header): swing the
         * whole door 90 degrees about the placement origin's Y axis; the
         * panel's hinge edge sits at local x = 0, so this reads as a
         * hinged door opening away from the doorway. */
        float a = DOOR_SWING_ANGLE * door_open_frac(d);
        float c = cosf(a), sn = sinf(a);
        for (uint32_t b = 0; b < n; b++) {
            const float *src = dm->base + b * 16;
            float       *m   = d->palette + b * 16;
            for (int col = 0; col < 4; col++) {
                float x = src[col * 4 + 0];
                float y = src[col * 4 + 1];
                float z = src[col * 4 + 2];
                m[col * 4 + 0] =  c * x + sn * z;
                m[col * 4 + 1] = y;
                m[col * 4 + 2] = -sn * x + c * z;
                m[col * 4 + 3] = src[col * 4 + 3];
            }
        }
    }

    /* Placement: every bone matrix M becomes T(pos) * R_y(yaw) * M. */
    const float c = cosf(d->yaw), sn = sinf(d->yaw);
    for (uint32_t b = 0; b < n; b++) {
        float *m = d->palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + sn * z;
            m[col * 4 + 2] = -sn * x + c * z;
        }
        m[12] += d->pos[0];
        m[13] += d->pos[1];
        m[14] += d->pos[2];
    }
}

/* ------------------------------------------------------------------ */
/* Trigger scan + state machine                                         */
/* ------------------------------------------------------------------ */

/* Wrap an angle to (-pi, pi] — the engine's func_001B1470. */
static float door_norm_ang(float a)
{
    while (a >  DOOR_PI) a -= 2.0f * DOOR_PI;
    while (a < -DOOR_PI) a += 2.0f * DOOR_PI;
    return a;
}

/* func_00184BA0 — the player USE SCAN over last frame's interactive
 * list (filters: status bit 0, class flag 0x80, +0x0B == 0), per
 * candidate func_00183EF0. CLASS-5 DOOR path, read in full 2026-06-11
 * (FINDINGS "DOOR USE SCAN + STAGING MATH" — see the constants block):
 * horizontal distance from the doorway CENTER <= desc[0] (the manifest
 * radius), |dy| <= 8.0, side test (bearing(player - door_pos) within
 * pi/2 of the door yaw -> front), then FACING: the player yaw must be
 * within pi/4 of facing THROUGH the door (front: yaw + pi == door_yaw
 * +- pi/4; back: yaw == door_yaw +- pi/4). No LOS query, no 2-u auto
 * ring — both belonged to the class-7 prefix; the old port LOS pocket
 * hack is retired. The nearest passing candidate gets +0x0B = 4.
 *
 * ENGINE TRIGGER (decoded 2026-06-11 s58 from the .s — OVERTURNS the
 * s17 "walk-into via action-state 0x2D" contract and the s56 slider
 * walk-into reading): the use scan func_00184BA0 runs ONLY when the
 * USE button is newly pressed. Every caller — the locomotion-state
 * handlers func_00160220 / func_001612D0 / func_0016DE40 (x3) — gates
 * the call identically on
 *
 *     D_00810E74 & *(u16 *)0x70003B76
 *
 * where D_00810E74 = cur_held & ~prev_held (func_001B5BC0: E70 =
 * current inverted raw pad, E72 = previous — E74 is the PRESS-EDGE
 * mask) and spad 0x70003B76 is the config-mask block's USE entry,
 * default 0x0040 = CROSS (s29). There is NO walk-into arming for ANY
 * door family: the +0x1F0 == 0x2D check inside func_00183EF0 guards
 * only the CLASS-7 prefix (LOS / dist^2 <= 144 / 2-u ring / facing
 * dot), and when the player IS in 0x2D the class-5 door branch is
 * unreachable (the function returns 0 for non-class-7 candidates). So
 * CROSS — pressed in a normal ground state inside the class-5 window —
 * is the one and only door trigger, hinged and slider alike. This is
 * why s22's analog-only pad injection never armed a door organically.
 * The port's in->pressed has exactly the D_00810E74 edge semantics. */
static void door_trigger_scan(const EmCollision *coll, const float pp[3],
                              float pyaw, const EmFrameInput *in)
{
    int   best = -1;
    float best_d2 = 1e30f;

    (void)coll;   /* class-5 doors do no LOS query (decoded 2026-06-11) */
    if (!(in->pressed & EM_PAD_CROSS))
        return;   /* the use-button press edge — the scan's only entry */

    for (int i = 0; i < s.n_doors; i++) {
        Door *d = &s.doors[i];
        if (d->state != EM_DOOR_CLOSED || d->armed) continue;
        float dx = d->center[0] - pp[0];
        float dz = d->center[2] - pp[2];
        float d2 = dx * dx + dz * dz;
        if (d2 > d->radius * d->radius) continue;             /* desc[0] */
        if (fabsf(pp[1] - d->pos[1]) > DOOR_VERT_LIMIT) continue;

        /* side: front when bearing(player - door_pos) is within pi/2 of
         * the door yaw (atan2 in the engine's (sin, cos) convention). */
        float bearing = atan2f(pp[0] - d->pos[0], pp[2] - d->pos[2]);
        int   front   = fabsf(door_norm_ang(bearing - d->yaw))
                        <= DOOR_PI * 0.5f;
        /* facing: player yaw within pi/4 of facing through the door. */
        float fd = door_norm_ang(pyaw + (front ? DOOR_PI : 0.0f) - d->yaw);
        if (fabsf(fd) > DOOR_FACING_ANG) continue;

        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    if (best >= 0)
        s.doors[best].armed = 4;   /* the scan's +0x0B value */
}

/* func_001BBE40 — the transit KICKOFF, byte-decoded 2026-06-11 (the
 * 91.5%-matched C stub src/func_001BBE40.c, trig labels corrected:
 * func_0011DE90 = cos, func_0011E2A8 = sin; both s22 captured staging
 * points reproduce exactly). Latch the player's side, snap the player
 * yaw to the door normal (front: door_yaw + pi; back: door_yaw), LOCK
 * input, and walk the player to the STAGING point
 *
 *     staging = (door_x - 5*cos(door_yaw) - 5*sin(pyaw),
 *                player_y,
 *                door_z + 5*sin(door_yaw) - 5*cos(pyaw))
 *             = doorway CENTER + 5 u toward the player's side
 *
 * (s22: the engine SNAPs there; the port drives the same point through
 * the scripted MOVE-TO walk of func_00182F90 — flagged deviation). The
 * far-side spawn point CENTER - 5*n approximates the spawn-table
 * re-place for non-goto doors (the real office records flank the
 * center at +-7; goto doors carry the real decoded spawn). The
 * scripted sequence is what carries the player across the grid
 * room-BOUNDARY planes (the doorways are statically sealed).
 *
 * NOTE this kickoff is the m03-family brain only — m17/m09 sliders run
 * their own decoded variant flow (slider_kickoff below, 2026-06-11;
 * the old "sliders ride the m03 machine" stand-in is retired). */
static void door_transit_kickoff(Door *d, const float pp[3])
{
    float nx = sinf(d->yaw), nz = cosf(d->yaw);
    float side = (pp[0] - d->pos[0]) * nx + (pp[2] - d->pos[2]) * nz;
    /* SIDE LATCH (+0x2E): front = the +n side the door faces — the
     * engine's |norm(atan2(player - door) - yaw)| <= pi/2 test,
     * equivalently dot(player - door, n) >= 0. The latch patches the
     * open script's per-side values: player anim 0x45/0x43, wait
     * 90/70, and the yaw snap (front: yaw + pi, back: yaw) — which is
     * exactly transit_yaw below. */
    d->front        = side >= 0.0f;
    d->open_wait    = d->front ? DOOR_WAIT_FRONT : DOOR_WAIT_BACK;
    d->phase_t      = 0.0f;
    d->anim_started = 0;
    /* yaw snap = travel direction = the exit yaw (spawn recs face AWAY
     * from the door — s22 "yaw facing AWAY (exit pose)") */
    d->transit_yaw   = d->front ? d->yaw + DOOR_PI : d->yaw;
    {
        /* the exact func_001BBE40 staging algebra (lateral center term
         * folded into d->center; pyaw = the snapped yaw) */
        float py = d->transit_yaw;
        d->transit_to[0] = d->center[0] - DOOR_POINT_DIST * sinf(py);
        d->transit_to[1] = pp[1];               /* spad y = player y */
        d->transit_to[2] = d->center[2] - DOOR_POINT_DIST * cosf(py);
        /* spawn point: mirrored through the center (far side) */
        d->spawn_pt[0]   = d->center[0] + DOOR_POINT_DIST * sinf(py);
        d->spawn_pt[1]   = d->pos[1];
        d->spawn_pt[2]   = d->center[2] + DOOR_POINT_DIST * cosf(py);
    }
    d->transit       = 1;
    d->did_warp      = 0;
    /* Both locks engage at kickoff (em_door.h "THE TWO LOCKS"): the
     * scripted sequence owns the player AND blocks the menu poll. */
    s.lock_move      = 1;   /* until the arrival walk-out completes */
    s.lock_menu      = 1;   /* until the fade-in completes */
}

/* SLIDER kickoff — the trigger sub func_001BB560 (see the SLIDER block
 * above): side latch from the door->player bearing, yaw snapped
 * THROUGH the door, staging at door_pos - 6.0 * forward (the player's
 * side; the engine SNAPs there via func_00182F90 — the port walks the
 * same point, the established MOVE-TO deviation). The walk-through
 * target (op01 sub8) mirrors it on the far side. Scripted mode = both
 * locks (op07 sub0 — no fade on the slider open). */
static void slider_kickoff(Door *d, const float pp[3])
{
    /* bearing(door -> player) within pi/2 of the door yaw = front */
    float bearing = atan2f(pp[0] - d->pos[0], pp[2] - d->pos[2]);
    d->front      = fabsf(door_norm_ang(bearing - d->yaw)) <= DOOR_PI * 0.5f;
    /* yaw snap: front side walks along yaw+pi, back side along yaw —
     * always THROUGH the doorway (func_001BB560's +0xC4 writes; the
     * flags2-8/0x16 single-leaf inversion is not in the manifest,
     * flagged) */
    d->transit_yaw   = d->front ? d->yaw + DOOR_PI : d->yaw;
    d->transit_yaw   = door_norm_ang(d->transit_yaw);
    d->transit_to[0] = d->pos[0] - SLIDER_POINT_DIST * sinf(d->transit_yaw);
    d->transit_to[1] = pp[1];
    d->transit_to[2] = d->pos[2] - SLIDER_POINT_DIST * cosf(d->transit_yaw);
    /* walk-through destination: mirrored through the doorway center */
    d->spawn_pt[0]   = d->pos[0] + SLIDER_POINT_DIST * sinf(d->transit_yaw);
    d->spawn_pt[1]   = d->pos[1];
    d->spawn_pt[2]   = d->pos[2] + SLIDER_POINT_DIST * cosf(d->transit_yaw);
    d->transit       = 1;
    d->sl_phase      = 0;
    d->anim_started  = 0;
    d->did_warp      = 0;
    s.lock_move      = 1;   /* op07 sub0 scripted mode: input lock */
    s.lock_menu      = 1;   /* spad 3B8D gates the menu poll too */
}

/* One frame of the ARRIVAL WALK-OUT sub-machine (func_00183250's +0x06
 * phases — constants above). Called from em_door_update; the commanded
 * speed for THIS frame is read back by em_door_walkout_active. The
 * MOVEMENT lock clears exactly at the phase-3 exit (the engine's
 * state 5 -> 1 transition + the spad 3B8D defensive clear). */
static void walkout_tick(void)
{
    if (!s.wo_phase) return;
    switch (s.wo_phase) {
    case 1:   /* clip only — the engine never calls the mover here */
    case 2:   /* mover at the full 0.3 u/tick ramp */
        if (--s.wo_t <= 0) {
            s.wo_phase++;
            s.wo_t = (s.wo_phase == 2) ? WALKOUT_PHASE2_FRAMES
                                       : WALKOUT_PHASE3_FRAMES;
        }
        break;
    case 3:   /* mover while the ramp decays to 0 (~26 frames in) */
        s.wo_ramp -= WALKOUT_RAMP_STEP;
        if (s.wo_ramp < 0.0f) s.wo_ramp = 0.0f;
        if (--s.wo_t <= 0) {
            s.wo_phase  = 0;       /* engine exit: state 1/0, 3B8D = 0 */
            s.lock_move = 0;       /* movement control returns HERE */
        }
        break;
    default:
        s.wo_phase = 0;
        break;
    }
}

/* Arm the walk-out at the re-place post (the engine's func_001B07C0
 * reading spawn-record byte +0x14 == 1 — every decoded record carries
 * it, so every arrival walks out). */
static void walkout_start(float exit_yaw)
{
    s.wo_phase = 1;
    s.wo_t     = WALKOUT_PHASE1_FRAMES;
    s.wo_yaw   = exit_yaw;
    s.wo_ramp  = WALKOUT_SPEED_UPT;
}

void em_door_update(const EmCollision *coll, const float player_pos[3],
                    float player_yaw, const EmFrameInput *in)
{
    /* No new use-arm while a transit sequence is in flight (the engine's
     * scan filters +0x0B == 0 and the request block is busy anyway). */
    if (!s.lock_move && !s.lock_menu)
        door_trigger_scan(coll, player_pos, player_yaw, in);

    /* MENU unlock at fade-in completion (the func_001AE7E0 fade gate:
     * D_0028A9A0 back to 0; spad 3B8D was already cleared at the
     * re-place) — the re-place happened at black, the door is still
     * closing and the WALK-OUT is still running (movement stays locked
     * until its phases end in walkout_tick). */
    if (s.lock_menu && s.unlock_armed && !s.warp_pending &&
        !s.goto_pending && !em_frame_fade_active() &&
        em_frame_fade_level() <= 0.0f) {
        s.lock_menu    = 0;
        s.unlock_armed = 0;
    }

    /* ARRIVAL WALK-OUT phases (player state 5/1 — func_00183250). */
    walkout_tick();

    for (int i = 0; i < s.n_doors; i++) {
        Door *d = &s.doors[i];

        /* Walk-to completion: the MOVE-TO ends at the staging point. */
        if (d->transit) {
            float dx = player_pos[0] - d->transit_to[0];
            float dz = player_pos[2] - d->transit_to[2];
            if (dx * dx + dz * dz <= 0.09f)
                d->transit = 0;
        }

        switch (d->state) {
        case EM_DOOR_CLOSED:
            if (d->armed) {        /* sub 0 -> kickoff -> sub 1 or 3 */
                d->armed  = 0;
                d->clip_t = 0.0f;
                /* THE LOCK GATE (func_001BC350 sub 0, em_door.h "THE
                 * LOCKED SEQUENCE"): D_00810841 bit clear -> kickoff
                 * mode 1 -> the LOCKED TRY (engine sub 1). The same
                 * side-latch/yaw-snap/staging walk runs either way. */
                int refused = d->lock_gated && !d->unlocked;
                d->state = refused ? EM_DOOR_LOCKED_TRY : EM_DOOR_OPENING;
                if (d->slider)
                    slider_kickoff(d, player_pos);
                else
                    door_transit_kickoff(d, player_pos);
                if (refused)
                    printf("door %d: LOCKED try (%s side) — unlock bit "
                           "clear\n", i, d->front ? "front" : "back");
            }
            break;
        case EM_DOOR_LOCKED_TRY:
            /* The LOCKED TRY script D_0024DEC0 (hinged) / D_0024DA40
             * (slider — camera + VO only, FLAGGED approximation: no
             * exported locked slider). The engine SNAPPED to staging;
             * the port's walk-to replaces the snap as in the open
             * flow. */
            if (d->transit)
                break;
            if (!d->anim_started) {
                /* Arrival: op09 locked-look camera CUT (consumed by
                 * em_game through em_door_locked_look), op0A player
                 * try anim 0x46/0x44 rate 1.0, op0B locked jiggle
                 * clip 3/1 (no sound). */
                d->anim_started = 1;
                d->lk_look      = 1;
                d->lk_fired     = 0;
                d->phase_t      = 0.0f;
                if (!d->slider) {
                    em_game_anim_request(d->front ? DOOR_ANIM_LOCK_FRONT
                                                  : DOOR_ANIM_LOCK_BACK,
                                         1.0f);
                    door_clip_select(d, d->front ? DOOR_CLIP_LOCK_FRONT
                                                 : DOOR_CLIP_LOCK_BACK);
                }
            }
            d->phase_t += 1.0f;
            if (!d->slider && d->clip_t < door_clip_total(d))
                d->clip_t += 1.0f;
            /* op 0x02 wait 60 -> op 0x17 rattle 0x3F2 -> op 0x09 VO =
             * the RADIO/EXAMINE message machine, line 6 (em_hud_radio;
             * text-only — the optional lockedvo audio id also plays if
             * the registry ever resolves one). Sliders skip the rattle
             * (D_0024DA40 has no sound record), wait 40, same VO. */
            if (!d->lk_fired &&
                d->phase_t >= (d->slider ? DOOR_LOCK_SLIDER_WAIT
                                         : DOOR_LOCK_RATTLE_AT)) {
                d->lk_fired = 1;
                if (!d->slider) {
                    /* op 0x17 sub 0: play_sound(owner, 300.0, id) —
                     * positional at the DOOR (em_sfx.h decode) */
                    em_sfx_play_at(EM_SFX_DOOR_RATTLE, d->pos, 300.0f);
                    s.rattles++;
                }
                em_hud_radio(DOOR_RADIO_LINE);  /* op09 func_001BBAE0 */
                if (s.vo_real)
                    em_sfx_play(s.vo_id);   /* radio voice: center */
            }
            /* The op09 VO native is PUMPED until the message machine
             * reports done (func_001BBAE0 phase 1 polls D_002821B4 ==
             * 2), THEN op 0x0B sub 1 waits door clip end (200 f from
             * kickoff — normally past the message, which clears at
             * ~60+119), then sub 1 queues the FINISH script D_0024DBC0
             * (op07 sub4 EXIT: restore camera + control). Sliders have
             * no clip — their script ends right when the VO native
             * does (wait 40 + the message duration). */
            if (d->lk_fired && !em_hud_radio_active() &&
                (d->slider || d->clip_t >= door_clip_total(d))) {
                d->state    = EM_DOOR_LOCKED_END;
                d->lk_look  = 0;        /* camera restore (op07 sub4) */
                s.lock_move = 0;        /* control returns             */
                s.lock_menu = 0;
                em_game_anim_cancel();  /* script teardown: +0x1F2 = 0
                                         * (the try clip ended at rest) */
            }
            break;
        case EM_DOOR_LOCKED_END:
            /* engine sub 2: pump done -> +0x0B = 0 (re-arm), sub 0.
             * The jiggle clip ended AT rest, so reset to the closed
             * pose directly. The door never opened — no warp, no fade,
             * the player stands at the staging point. */
            d->clip_t   = 0.0f;
            d->clip_idx = 0;
            d->armed    = 0;
            d->state    = EM_DOOR_CLOSED;
            break;
        case EM_DOOR_OPENING:
            if (d->slider) {       /* func_001BB860 state 2: the OPEN
                                    * script D_0024D900 (SLIDER block) */
                switch (d->sl_phase) {
                case 0:            /* staging walk (engine: snap) */
                    if (d->transit)
                        break;
                    d->sl_phase = 1;
                    /* op17 sub0: ONE positional door sound at slide
                     * start. The slider's own pair (D_0024DB80 family
                     * 6) is BSS-undumped — the manifest doorsfx pair
                     * stands in (the s32 flag, unchanged). */
                    if (s.sfx_real) {
                        unsigned id = s.sfx_pair[d->front ? 0 : 1];
                        printf("door sfx: slider open id 0x%03X (%s "
                               "side)\n", id, d->front ? "front" : "back");
                        em_sfx_play_at(id, d->pos, 300.0f);  /* op17
                                         * sub0: at the door, r=300 */
                    } else {
                        em_sfx_play_at(EM_SFX_DOOR_OPEN, d->pos,
                                       300.0f);     /* PLACEHOLDER */
                    }
                    break;
                case 1:            /* op09 func_001BB400: panels part
                                    * 0.2 u/frame (the baked 46-frame
                                    * clip at 1.0/frame). NO player
                                    * anim — he stands at the staging
                                    * point. */
                    d->clip_t += 1.0f;
                    if (d->clip_t >= door_clip_total(d)) {
                        d->clip_t  = door_clip_total(d);
                        d->sl_phase = 2;
                        /* op01 sub8: scripted walk-through to the
                         * mirrored point (the MOVE-TO machinery; the
                         * locomotion walk clip — no gesture anim). */
                        d->transit_to[0] = d->spawn_pt[0];
                        d->transit_to[1] = player_pos[1];
                        d->transit_to[2] = d->spawn_pt[2];
                        d->transit       = 1;
                    }
                    break;
                case 2:            /* walk-through -> handoff */
                    if (d->transit)
                        break;
                    if (d->has_goto) {
                        /* area-change slider: the engine's state-3
                         * COMMIT (func_001BC150) — the port reuses the
                         * m03 fade + scene switch (sequencing flagged:
                         * the native commit/fade interleave for the
                         * slider's cross-area path is unread). */
                        d->state = EM_DOOR_OPEN;
                    } else {
                        /* intra-room slider: scripted mode ends, the
                         * player keeps playing — the door stays parted
                         * (re-closes when the player leaves, the s32
                         * reverse-clip flag). */
                        s.lock_move = 0;
                        s.lock_menu = 0;
                        d->state    = EM_DOOR_OPEN;
                    }
                    break;
                }
                break;
            }
            /* hinged m03 family: the OPEN script D_0024DE40 + the
             * clip pump func_001BC0E0 */
            /* Walk-to staging still in flight: the engine SNAPPED here,
             * so its script ran immediately; the port's walk replaces
             * the snap (flagged deviation) and the script chain fires
             * on arrival. The walk-through keeps the normal locomotion
             * walk anim. */
            if (d->transit)
                break;
            if (!d->anim_started) {
                /* Arrival = the script's anim/sound/clip records run
                 * back to back: player anim 0x45/0x43 rate 1.0 (op 0x0A
                 * sub 0, patched by the side latch), door sound + door
                 * clip start (op 0x0B sub 6). The player already faces
                 * the door — the kickoff's yaw snap (transit_yaw) IS
                 * the front/back snap of func_001BBE40. */
                d->anim_started = 1;
                em_game_anim_request(d->front ? DOOR_ANIM_OPEN_FRONT
                                              : DOOR_ANIM_OPEN_BACK,
                                     1.0f);
                /* op 0x0B sub 6 carries the side-patched ENGINE clip
                 * id too: open 2 (front) / 0 (back) — resolved through
                 * the EMDL clip table (the [0,2,1,3] s30 bake). */
                door_clip_select(d, d->front ? DOOR_CLIP_OPEN_FRONT
                                             : DOOR_CLIP_OPEN_BACK);
                /* Door sound (op 0x0B sub 6): the engine plays
                 * pair[side] — D_0024DB80[link>>8] patched in by
                 * func_001BBD60. The pair arrives via the doorsfx
                 * manifest line (see DOOR_SFX_KEYWORD); without it the
                 * legacy PLACEHOLDER id fires as before. */
                if (s.sfx_real) {
                    unsigned id = s.sfx_pair[d->front ? 0 : 1];
                    printf("door sfx: open id 0x%03X (%s side, "
                           "D_0024DB80 pair)\n", id,
                           d->front ? "front" : "back");
                    em_sfx_play_at(id, d->pos, 300.0f);  /* at the
                                     * door — op-0x17 family, r=300 */
                } else {
                    em_sfx_play_at(EM_SFX_DOOR_OPEN, d->pos,
                                   300.0f);         /* PLACEHOLDER */
                }
            }
            /* Clip pump (1.0/frame) + the script's op 0x02 wait: the
             * phase runs 90 (front) / 70 (back) frames, then the script
             * STOPs and the transition COMMIT follows. The back-side
             * commit leaves the PLACEHOLDER 90-frame swing at 70/90 —
             * the engine's back clip (index 0) is its own, shorter,
             * animation; honest until the real door clips are found. */
            if (d->clip_t < door_clip_total(d))
                d->clip_t += 1.0f;
            d->phase_t += 1.0f;
            if (d->phase_t >= d->open_wait)
                d->state = EM_DOOR_OPEN;
            break;
        case EM_DOOR_OPEN:
            if (d->slider && !d->has_goto) {
                /* intra-room slider stays parted; when the player
                 * leaves the scan window (+ hysteresis) it re-closes —
                 * the engine recloses via room re-entry state, the
                 * port runs the slide backwards (s32 flag). */
                float dx = player_pos[0] - d->pos[0];
                float dz = player_pos[2] - d->pos[2];
                float lim = d->radius + SLIDER_LEAVE_PAD;
                if (dx * dx + dz * dz > lim * lim)
                    d->state = EM_DOOR_CLOSING;
                break;
            }
            /* one-frame COMMIT (func_001BC240 -> func_001BC150): arm
             * the 64-frame fade-out. Room move (B8 == 2): NO audio
             * fade (area changes only). */
            em_frame_fade_start(1, DOOR_FADE_SPEED);
            d->state = EM_DOOR_CLOSING;
            break;
        case EM_DOOR_CLOSING:      /* engine sub 5: transition pending */
            if (d->slider && !d->has_goto) {
                /* reverse slide to rest, then re-arm (+0x0B = 0) */
                d->clip_t -= 1.0f;
                if (d->clip_t <= 0.0f) {
                    d->clip_t   = 0.0f;
                    d->state    = EM_DOOR_CLOSED;
                    d->armed    = 0;
                    d->sl_phase = 0;
                }
                break;
            }
            if (!d->did_warp) {
                /* Wait out the fade-out; at black, post the re-place
                 * (spawn point behind the door, exit yaw), arm the
                 * fade-in, and start closing — the engine's "B8
                 * cleared" moment. */
                if (!em_frame_fade_active() &&
                    em_frame_fade_level() >= 1.0f) {
                    if (d->has_goto) {
                        /* GOTO door: post the SCENE SWITCH instead of
                         * the same-scene re-place — em_game runs
                         * em_game_scene_switch + the spawn placement
                         * while black (this door is freed by the
                         * switch; the transit-wide lock + armed
                         * unlock survive em_door_scene_clear). */
                        s.goto_pending = 1;
                        snprintf(s.goto_dir, sizeof s.goto_dir, "%s",
                                 d->goto_dir);
                        s.warp_pos[0] = d->goto_pos[0];
                        s.warp_pos[1] = d->goto_pos[1];
                        s.warp_pos[2] = d->goto_pos[2];
                        s.warp_yaw    = d->goto_yaw;
                    } else {
                        s.warp_pending = 1;
                        s.warp_pos[0]  = d->spawn_pt[0];
                        s.warp_pos[1]  = d->spawn_pt[1];
                        s.warp_pos[2]  = d->spawn_pt[2];
                        s.warp_yaw     = d->transit_yaw;
                    }
                    d->did_warp    = 1;
                    s.unlock_armed = 1;
                    /* The re-place arms the ARRIVAL WALK-OUT (engine
                     * func_001B07C0: spawn rec +0x14 == 1 -> player
                     * state 5/1) along the exit yaw — both the
                     * same-scene re-place and the goto switch (the
                     * engine walks out of EVERY decoded spawn). */
                    walkout_start(s.warp_yaw);
                    /* Script teardown under black: the player anim
                     * resets with the re-place (the op 0x18 family's
                     * +0x1F2 = 0) — locomotion resumes into the
                     * walk-out clip. */
                    em_game_anim_cancel();
                    em_frame_fade_start(-1, DOOR_FADE_SPEED);
                    /* Close sound: the decoded open script D_0024DE40
                     * carries a SINGLE sound record — no close sound is
                     * engine-documented (func_001BBD20 is a possible
                     * close path, undecoded). With the real pair active
                     * the port stays faithful and plays NOTHING here;
                     * the legacy PLACEHOLDER fires only in the
                     * no-doorsfx configuration, as before. */
                    if (!s.sfx_real)
                        em_sfx_play_at(EM_SFX_DOOR_CLOSE, d->pos,
                                       300.0f);     /* PLACEHOLDER */
                }
                break;
            }
            /* func_001BC290: clip back to rest, then re-arm
             * (+0x0B = 0) -> sub 0. */
            d->clip_t -= 1.0f;
            if (d->clip_t <= 0.0f) {
                d->clip_t   = 0.0f;
                d->state    = EM_DOOR_CLOSED;
                d->armed    = 0;
                d->did_warp = 0;
            }
            break;
        default:
            break;
        }
        door_build_palette(d);
    }
}

/* ------------------------------------------------------------------ */
/* Draw + collision accessors                                           */
/* ------------------------------------------------------------------ */

int em_door_count(void) { return s.n_doors; }

/* The two-lock split — em_door.h "THE TWO LOCKS". */
int em_door_movement_locked(void) { return s.lock_move; }
int em_door_menu_locked(void)     { return s.lock_menu; }

/* THE LOCK GATE (em_door.h "THE LOCKED SEQUENCE") ------------------- */

int em_door_set_locked(int i)
{
    if (i < 0 || i >= s.n_doors)
        return -1;
    s.doors[i].lock_gated = 1;
    printf("door %d: LOCK-GATED (manifest `locked` — D_00810841 bit "
           "clear at boot)\n", i);
    return 0;
}

void em_door_unlock(int i)
{
    if (i < 0 || i >= s.n_doors)
        return;
    if (s.doors[i].lock_gated && !s.doors[i].unlocked)
        printf("door %d: UNLOCKED (D_00810841 bit set)\n", i);
    s.doors[i].unlocked = 1;
}

int em_door_is_locked(int i)
{
    if (i < 0 || i >= s.n_doors)
        return 0;
    return s.doors[i].lock_gated && !s.doors[i].unlocked;
}

int em_door_locked_look(float out_door_pos[3], float *out_door_yaw,
                        float *out_snap_yaw)
{
    for (int i = 0; i < s.n_doors; i++) {
        const Door *d = &s.doors[i];
        if (!d->lk_look) continue;
        out_door_pos[0] = d->pos[0];
        out_door_pos[1] = d->pos[1];
        out_door_pos[2] = d->pos[2];
        *out_door_yaw   = d->yaw;
        if (out_snap_yaw)
            *out_snap_yaw = d->transit_yaw;  /* the kickoff snap (s71) */
        return 1;
    }
    return 0;
}

int em_door_rattles(void) { return s.rattles; }

int em_door_walkout_active(float *out_yaw, float *out_speed)
{
    if (!s.wo_phase) return 0;
    *out_yaw = s.wo_yaw;
    /* Commanded translation for THIS frame: phase 1 plays the clip in
     * place (the engine calls no mover); phases 2/3 move at the ramp
     * (full 0.3 u/tick, then the phase-3 decay). u/tick -> u/sec. */
    *out_speed = (s.wo_phase >= 2) ? s.wo_ramp * WALKOUT_TICK_HZ : 0.0f;
    return 1;
}

int em_door_warp_pending(float out_pos[3], float *out_yaw)
{
    if (!s.warp_pending) return 0;
    out_pos[0]     = s.warp_pos[0];
    out_pos[1]     = s.warp_pos[1];
    out_pos[2]     = s.warp_pos[2];
    *out_yaw       = s.warp_yaw;
    s.warp_pending = 0;     /* one-shot */
    return 1;
}

int em_door_goto_pending(char *dir, unsigned dir_size, float out_pos[3],
                         float *out_yaw)
{
    if (!s.goto_pending) return 0;
    snprintf(dir, dir_size, "%s", s.goto_dir);
    out_pos[0]     = s.warp_pos[0];
    out_pos[1]     = s.warp_pos[1];
    out_pos[2]     = s.warp_pos[2];
    *out_yaw       = s.warp_yaw;
    s.goto_pending = 0;     /* one-shot */
    return 1;
}

void em_door_scene_clear(EmGfx *gfx)
{
    /* The transit that triggered the switch is still mid-flight: keep
     * the two locks, the armed fade-in unlock AND the arrival walk-out
     * alive across the door teardown (the engine's player state + fade
     * machine survive the area load; the new scene's doors arrive
     * CLOSED and idle). */
    int   lock_move = s.lock_move, lock_menu = s.lock_menu;
    int   unlock_armed = s.unlock_armed;
    int   wo_phase = s.wo_phase, wo_t = s.wo_t;
    float wo_yaw = s.wo_yaw, wo_ramp = s.wo_ramp;
    em_door_shutdown(gfx);     /* frees + memsets s */
    s.lock_move    = lock_move;
    s.lock_menu    = lock_menu;
    s.unlock_armed = unlock_armed;
    s.wo_phase     = wo_phase;
    s.wo_t         = wo_t;
    s.wo_yaw       = wo_yaw;
    s.wo_ramp      = wo_ramp;
}

int em_door_transit_active(float out_target[3], float *out_yaw)
{
    for (int i = 0; i < s.n_doors; i++) {
        const Door *d = &s.doors[i];
        if (!d->transit) continue;
        out_target[0] = d->transit_to[0];
        out_target[1] = d->transit_to[1];
        out_target[2] = d->transit_to[2];
        *out_yaw      = d->transit_yaw;
        return 1;
    }
    return 0;
}

void em_door_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count)
{
    const Door      *d  = &s.doors[i];
    const DoorModel *dm = &s.models[d->model];
    *mesh       = dm->mesh;
    *palette    = d->palette;
    *bone_count = dm->model.bone_count;
}

int em_door_state(int i) { return s.doors[i].state; }

void em_door_pos(int i, float out[3])
{
    out[0] = s.doors[i].pos[0];
    out[1] = s.doors[i].pos[1];
    out[2] = s.doors[i].pos[2];
}

/* Segment-vs-AABB slab test. Returns the entry t in [0,1] or -1. */
static float seg_aabb(const float a[3], const float b[3],
                      const float lo[3], const float hi[3], int *axis)
{
    float t0 = 0.0f, t1 = 1.0f;
    int   ax = -1;
    for (int k = 0; k < 3; k++) {
        float dk = b[k] - a[k];
        if (fabsf(dk) < 1e-9f) {
            if (a[k] < lo[k] || a[k] > hi[k]) return -1.0f;
            continue;
        }
        float inv = 1.0f / dk;
        float n   = (lo[k] - a[k]) * inv;
        float f   = (hi[k] - a[k]) * inv;
        if (n > f) { float tmp = n; n = f; f = tmp; }
        if (n > t0) { t0 = n; ax = k; }
        if (f < t1) t1 = f;
        if (t0 > t1) return -1.0f;
    }
    if (ax < 0) return -1.0f;   /* segment starts inside: no entry face */
    *axis = ax;
    return t0;
}

int em_door_probe(const float from[3], const float to[3], EmCollHit *hit)
{
    float best_t = 2.0f;
    int   best = -1, best_axis = 0;

    for (int i = 0; i < s.n_doors; i++) {
        const Door *d = &s.doors[i];
        if (d->state == EM_DOOR_OPEN)
            continue;   /* collision suppressed only when FULLY open */
        int   axis;
        float t = seg_aabb(from, to, d->aabb_lo, d->aabb_hi, &axis);
        if (t >= 0.0f && t <= 1.0f && t < best_t) {
            best_t    = t;
            best      = i;
            best_axis = axis;
        }
    }
    if (best < 0) return 0;
    if (hit) {
        memset(hit, 0, sizeof *hit);
        for (int k = 0; k < 3; k++) {
            hit->point[k] = from[k] + (to[k] - from[k]) * best_t;
            hit->delta[k] = hit->point[k] - to[k];
        }
        hit->normal[best_axis] =
            (to[best_axis] > from[best_axis]) ? -1.0f : 1.0f;
        hit->kind       = EM_COLL_SET_HULLS;
        hit->poly       = -1;
        hit->surf_class = EM_SURF_WALL;
    }
    return 1;
}

void em_door_shutdown(EmGfx *gfx)
{
    for (int i = 0; i < s.n_models; i++) {
        if (s.models[i].mesh) {
            em_gfx_mesh_destroy(gfx, s.models[i].mesh);
            em_model_free(&s.models[i].model);
        }
    }
    memset(&s, 0, sizeof s);
}
