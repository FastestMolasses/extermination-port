/* The live first-level smoke (SCENE_COORDINATOR_DESIGN.md step S13). See
 * em_level_smoke_test.h and docs/LEVEL_SMOKE.md.
 *
 * The phases follow the original route (docs/FIRST_LEVEL_ROUTE.md section
 * 3, main line 01 -> 14). A phase is LIVE when this file has a runner for it;
 * a runner is added in the step that makes the phase's original owners live
 * in the port (the `lands` column), never before. Until then the phase
 * reports NOT-LIVE, naming the original owner, the binding the port runs
 * for it today and the step it waits on; the run stops there, because every
 * later beat starts from the state the earlier ones leave. A NOT-LIVE phase a
 * later live phase needs the state of is "driven" (Phase.driven): its runner
 * plays it through the owner's current port binding and it is reported
 * NOT-LIVE driven, never passed (none since WP-6 made the battery live).
 *
 * Each runner drives pad input only (as the route captures do) and asserts
 * the original values it can observe in process. The tick-by-tick comparison
 * against the original captures reads the scene tick log
 * (EM_AREA_CHANGE_LOG) in tools/test_level_smoke.py; the runner prints the
 * join key it needs (D_00810750 at first control). */
#include "game/em_level_smoke_test.h"
#include "em_input.h"
#include "game/em_area11_bindings.h"
#include "game/em_area11_boxes.h"
#include "game/em_area11_door.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_pad_actuator.h"
#include "game/em_frame.h"
#include "game/em_game.h"
#include "game/em_game_internal.h"
#include "game/em_interaction_runtime.h"
#include "game/em_interaction_scene.h"
#include "game/em_pickup_owner.h"
#include "game/em_weapon.h"
#include "game/em_opening_runtime.h"
#include "game/em_pickup.h"
#include "game/em_pickup_original.h"
#include "game/em_scene_bindings.h"
#include "game/em_scene_state.h"
#include "game/em_status_background.h"
#include "game/em_status_models.h"
#include "game/em_status_runtime.h"
#include "game/em_stream_live.h"
#include "game/em_task.h"
#include "game/em_frontend.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *beat;     /* route capture (../Extermination/build/s87/route/<beat>) */
    uint32_t owner;       /* original owner callback (0: the player's own states) */
    const char *original; /* original owners and scripts (FIRST_LEVEL_ROUTE.md section 3) */
    const char *lands;    /* the step that makes the phase live */
    void (*begin)(void);  /* NULL: NOT-LIVE */
    int (*frame)(void);   /* after each frame: 0 continue, 1 passed (failures call fail()) */
    /* 1: a NOT-LIVE phase a later live phase needs the state of (the
     * battery for the panel): the runner drives it through the owner's
     * current port binding and reports it NOT-LIVE ("driven"), never PASS;
     * the later phases' capture checks compare only their own windows. */
    int driven;
    /* 1: a side beat (FIRST_LEVEL_ROUTE.md section 3: beats 00 and 09 have
     * their own snapshots and are not on the main line). The main line
     * skips it and names it NOT-LIVE / side; EM_LEVEL_SMOKE_UNTIL=<it>
     * runs the main line up to it and then only it. */
    int side;
    /* n > 0: a side beat that starts from the end of the side beat n
     * phases before it (the C7 side-1 capture starts from beat 09's end,
     * n = 1; the BRANCH beats br_05 / br_10 from beat 09's too):
     * EM_LEVEL_SMOKE_UNTIL=<it> runs the main line, that side beat, then
     * it. */
    int from_side;
    /* 1: the phase's frame function runs once per main-loop iteration,
     * including the iterations that close out neither a world nor a
     * status frame (the status screen's entry and exit iterations), so a
     * replayed pad script stays on the capture's frames
     * (em_level_smoke_test_tick_end). */
    int every_tick;
} Phase;

static void first_control_begin(void);
static int first_control_frame(void);
static void panel_no_battery_begin(void);
static int panel_no_battery_frame(void);
static void status_begin(void);
static int status_frame(void);
static void status_pages_begin(void);
static int status_pages_frame(void);
static void battery_begin(void);
static int battery_frame(void);
static void refusal_begin(void);
static int refusal_frame(void);
static void panel_begin(void);
static int panel_frame(void);
static void elevator_begin(void);
static int elevator_frame(void);
static void boxes_begin(void);
static int boxes_frame(void);
static void slide_begin(void);
static int slide_frame(void);
static void truck_preview_begin(void);
static int truck_preview_frame(void);
static void truck_crossing_begin(void);
static int truck_crossing_frame(void);
static void fence_door_begin(void);
static int fence_door_frame(void);
static void fence_door_side1_begin(void);
static int fence_door_side1_frame(void);
static void aim_hold_begin(void);
static int aim_r1_hold_frame(void);
static int aim_r2_hold_frame(void);
static int aim_replay_frame(void);
static void aim_script_begin(void);
static int walk_path(const float (*path)[2], int count, float tol);
static void cage_ladders_begin(void);
static int cage_ladders_frame(void);
static void director_begin(void);
static int cage_roof_frame(void);
static void crevice_climbs_begin(void);
static int crevice_climbs_frame(void);
static int crevice_prompt_frame(void);
static void crevice_jump_begin(void);
static int crevice_jump_frame(void);
static void east_tower_climb_begin(void);
static int east_tower_climb_frame(void);
static int east_tower_frame(void);
static void roger_begin(void);
static int roger_frame(void);
static void exit_begin(void);
static int exit_frame(void);
static void a01_arrival_begin(void);
static int a01_arrival_frame(void);
static void a01_route_begin(void);
static int a01_route_frame(void);
static void dmg_begin(void);
static int dmg_frame(void);
static void br_begin(void);
static int br_frame(void);
static void opt_begin(void);
static int opt_frame(void);

static const Phase k_phases[] = {
    {"first_control", "01_battery (row f0 = slot 04)", 0,
     "0x1AE040 state 1 with 001AE5E0; the AREA11 pool of 49 nodes (ORIGINAL_FRAME_ORDER.md 2, 4)",
     "S12a", first_control_begin, first_control_frame, 0, 0, 0, 0},
    {"panel_no_battery", "00_panel_no_battery (side, slot 04)", 0x00159210u,
     "panel 00159210 (r18) without item 0x1B: script 0x246F20, message 0x80000018, letterbox",
     "WP-4 (the panel's original owner and its scripts)", panel_no_battery_begin, panel_no_battery_frame, 0,
     1, 0, 0},
    {"status", "01_battery (status exit); frame_trace2/status_04.json", 0,
     "001AE7E0 r==2 -> state 3 (0020E060, 0020CDC0) -> state 5 -> state 1 (ORIGINAL_FRAME_ORDER.md Q7)",
     "S11b", status_begin, status_frame, 0, 0, 0, 0},
    {"status_pages", "(designed: no route capture shows a status page open)", 0x0020CDC0u,
     "0020CDC0 phase 3: DATABASE 00214020, SPR4 00211970 with its part pages, the ITEM children "
     "00214570 / 00215870 / 002160B0, and the takes of 0x1E / 0x1F / 0x32 / 0x10",
     "chain C8b FAILSTOPS (em_status_pages_live)", status_pages_begin, status_pages_frame, 0, 1, 0, 0},
    {"battery", "01_battery", 0x00219550u,
     "pickup 00219550 g0.0 (item 0x1B): take script 0x266620, B0=1/B1=0x1B, status ITEM page",
     "WP-6 (pickup owner and Use arbiter) with WP-5 (status ITEM page)", battery_begin, battery_frame,
     0, 0, 0, 0},
    {"elevator_refusal", "02_elevator_refusal", 0x00827B10u,
     "terminal 0x827B10 (r19): refusal script 0x82A990, message 0x8000001A, letterbox",
     "WP-4", refusal_begin, refusal_frame, 0, 0, 0, 0},
    {"br_panel_decline", "br_03_panel_decline (side, from 02; decomp CAPTURES_C10.md BRANCH)", 0x00159210u,
     "panel 00159210 (r18) with the battery: script 0x2477A0, the BATTERY page's two-unit prompt, No, Triangle: the cancel script 0x247DA0; the power stays off",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 0, 0},
    {"panel", "03_panel_power", 0x00159210u,
     "panel 00159210 (r18): script 0x2477A0, 00157F60 B0=1/B1=0x82 (BATTERY page), discharge, "
     "script 0x247BE0, power bit 0x80",
     "WP-4", panel_begin, panel_frame, 0, 0, 0, 0},
    {"elevator", "04_elevator_ride", 0x00827B10u,
     "terminal 0x827B10: powered script 0x82A750, clip 0x47, carry 0x828050 down to y 190", "WP-4",
     elevator_begin, elevator_frame, 0, 0, 0, 0},
    {"br_elevator_up", "br_02_elevator_up (side, from 04; decomp CAPTURES_C10.md BRANCH)", 0x00827B10u,
     "terminal 0x827B10 on the lower floor: the powered script 0x82A750 and the carry 0x828050 back up (D_0081083A 1 -> 0)",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 0, 0},
    {"br_crate_stack", "br_04_crate_stack_break (side, from 04; decomp CAPTURES_C10.md BRANCH)", 0x001551B0u,
     "the light melee 001735C0 on box r5 (001551B0): its damage break, the raised r3 woken (+0x0A), its fall and break",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 0, 0},
    {"boxes", "05_boxes", 0x001551B0u, "ledge climb (state 2, +1F0 8) onto crates r4 and r3 (001551B0)",
     "the Use chain and the crates' original owners (census L25)", boxes_begin, boxes_frame, 0, 0, 0, 0},
    {"br_ledge_ammo", "br_00_ledge_ammo (side, from 05; decomp CAPTURES_C10.md BRANCH)", 0x00219550u,
     "pickup 00219550 g0.3 (item 0x1E, puid 6) on the 220 ledge: the take, the status page, the taken bit",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 0, 0},
    {"slide", "06_hill_slide", 0, "slope slide 0016C6A0 (state 0x1C, +1F0 0x30)",
     "the slope slide on the live record (em_player_slide; census L03)", slide_begin, slide_frame, 0, 0, 0, 0},
    {"br_map_item", "br_01_map_item (side, from 06; decomp CAPTURES_C10.md BRANCH)", 0x0015AFA0u,
     "the map item 0015AFA0 g0.6 (item 0x08, puid 9): the grab clip 0x40, the status page, the taken bit",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 0, 0},
    {"truck_preview", "07_truck_preview", 0x008251E0u,
     "trigger 0x8251E0 (r17): camera script 0x8292C0, letterbox, D_00810792=1",
     "the trigger and the AREA11 script host (census L23, L19)", truck_preview_begin,
     truck_preview_frame, 0, 0, 0, 0},
    {"dmg_pit_fall", "dmg_07_pit_fall (side, from 07; decomp CAPTURES_C10.md DAMAGE)", 0x00823FF0u,
     "the truck's fall into the pit, the walk off its roof onto the attribute-0x5D floor: 0021D250 (+5 = 0x16), "
     "0021D2E0, game over 001AD4E0 (module 0x27)",
     "the DAMAGE step (docs/DAMAGE.md)", dmg_begin, dmg_frame, 0, 1, 0, 0},
    {"truck_crossing", "08_truck_crossing", 0x00823FF0u,
     "truck 0x823FF0 (r16): stand-on arm, shake, fall, D_00810792=0xFF",
     "the truck's original owner (census L23)", truck_crossing_begin, truck_crossing_frame, 0, 0, 0, 0},
    {"opt_00", "opt_00_browse_close (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "SELECT; every row browsed down (the wrap) and up; closed by Cross on the exit row, Circle, Triangle and SELECT (0022A650 states 0, 1, 12; 0022AEA0)",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"opt_01", "opt_01_vibration (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "the vibration row: 00201720 (Right flips +1; switching it on rumbles 001B61C0), kept with Cross, twice",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"opt_02", "opt_02_sound (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "the sound row: 00201720 (Right flips +4; 001FB100 commits D_0028215B), kept with Cross, twice",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"opt_03", "opt_03_screen_position (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "screen position: state 10 (0022A590, module 0x2B), 00201F70 moves 0x70003B94 / 96; kept twice, cancelled with Circle",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"opt_04", "opt_04_brightness (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "brightness: state 10, 00202BA0's still screen, left with Cross, Circle and Triangle (states 11, 12)",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"opt_05", "opt_05_button_config (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "button config: state 10, 00202D10, types B, C and A each kept with Cross (001AF470's masks)",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"opt_06", "opt_06_default (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "vibration off; the default prompt 00201C50: No, then Yes (the defaults)",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"opt_07", "opt_07_load_cancel (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "the load row: 001AF6F0, 00225AC0(0) (module 0x2A, the card poll 001FECB0 / 001FE9A0 over em_memcard) to the slot choice 00226070, left with Triangle",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"opt_08", "opt_08_quit_cancel (side, from 08; decomp CAPTURES_C10.md OPTIONS)", 0x0022A650u,
     "the quit prompt 0022B420: Right to Yes and back, Cross on No, Circle, Triangle (state 12)",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", opt_begin, opt_frame, 0, 1, 0, 1},
    {"fence_door", "09_fence_door (side, from 08)", 0x001BC350u,
     "door 001BC350 (r0): scripts 0x24DE40 / 0x24DC00, clip 0x45, room move B7=2/B8=2 to entry 2",
     "the fence door's original owner and the ELF program on the AREA11 script host (census L18)",
     fence_door_begin, fence_door_frame, 0, 1, 0, 0},
    {"fence_door_side1", "c7_door1_fence_door_side1 (side, from 09; decomp CAPTURES_C7.md section 4)", 0x001BC350u,
     "door 001BC350 (r0) from behind the fence: scripts 0x24DE40 / 0x24DC00, clip 0x43, room move B7=1/B8=2 "
     "to entry 1; the arrival walk-out 001B07C0(1) 5/1/0 on the player's 0015B610 / 00183250",
     "the player's +4 = 5 handler 0015B610 and 00183250 (fence door side 1)", fence_door_side1_begin,
     fence_door_side1_frame, 0, 1, 1, 0},
    {"br_west_ledge", "br_05_west_ladder_up .. br_08_west_ladder_down (side, from 09; decomp CAPTURES_C10.md BRANCH)", 0,
     "the corridor box's ledge climb and step-off, the west-yard ladder up (0x15 / 0x17 / 0x18), box r6 broken by the light melee, pickup g0.5 (item 0x10, puid 8), the ladder down (the grab from above, 0x16)",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 2, 0},
    {"br_yard_ammo", "br_10_yard_ammo (side, from 09; decomp CAPTURES_C10.md BRANCH)", 0x00219550u,
     "pickup 00219550 g0.1 (item 0x1E, puid 4) on the yard floor: the take, the status page, the taken bit",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 3, 0},
    {"aim_r1_hold", "aim_00_r1_hold (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "R1 stance 0016FCF0 (+5 0x1D, +1F0 0x31), camera action 1 00197D20 and its release 00197490",
     "the aim camera (chain step AIMCAM; CAMERA_LIVE.md section 7)", aim_hold_begin, aim_r1_hold_frame, 0, 1,
     0, 0},
    {"aim_r2_hold", "aim_01_r2_hold (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "R2 stance 001703E0 (+5 0x1E, +1F0 0x32), camera action 2 00198650 and its release 00197490",
     "the aim camera (chain step AIMCAM; CAMERA_LIVE.md section 7)", aim_hold_begin, aim_r2_hold_frame, 0, 1,
     0, 0},
    {"aim_fire", "aim_03_single_fire (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "R1 / R2 single fire: 00170A60 states 0x0A / 0x0B, the round 001861C0, the muzzle node 001F5040 (00187CC0), the shell casing 001F4010, the impact marker 0018ABA0",
     "the original aim / fire path (chain step AIMLIVE; AIM_FIRE.md)", aim_hold_begin, aim_replay_frame, 0, 1, 0, 0},
    {"aim_reload", "aim_06_reload_partial (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "L3 reload 0017B300 mode 2 (+6 3, +1F0 0x33), L3 on a full magazine, the release mid-reload (0016F600)",
     "the original aim / fire path (chain step AIMLIVE; AIM_FIRE.md)", aim_hold_begin, aim_replay_frame, 0, 1, 0, 0},
    {"aim_reload_empty", "aim_07_reload_empty (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "60 single rounds: the automatic reload at an empty magazine, the empty reserve, dry presses",
     "the original aim / fire path (chain step AIMLIVE; AIM_FIRE.md)", aim_hold_begin, aim_replay_frame, 0, 1, 0, 0},
    {"aim_light", "aim_08_light_holster (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "Square: the gun light D_00810D3C / the lamp D_008106C7 (0017A970), Cross 0017AAD0, holster, R2 redraw",
     "the original aim / fire path (chain step AIMLIVE; AIM_FIRE.md)", aim_hold_begin, aim_replay_frame, 0, 1, 0, 0},
    {"aim_both", "aim_02_r1_r2_both (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "R1 and R2 together: the stance switch 0x1D / 0x1E, both from idle",
     "the original aim / fire path (chain step AIMLIVE; AIM_FIRE.md)", aim_hold_begin, aim_replay_frame, 0, 1, 0, 0},
    {"aim_melee", "aim_09_melee (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "melee from idle: Circle 001735C0 (+5 0x21, +1F0 0x36) and its combo, Square 00173E60 (+5 0x22)",
     "the original aim / fire path (chain step AIMLIVE; AIM_FIRE.md)", aim_hold_begin, aim_replay_frame, 0, 1, 0, 0},
    {"aim_world", "aim_04_world_hit (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "the walk and the left-stick aim (0017ABA0), rounds into the ground, the pillar, past the fence and a miss: "
     "the impact marker 0018ABA0 and the impact effects 0x80000060 (001EACF0, the streak program 0x230800)",
     "the original aim / fire path (chain step AIMLIVE; AIM_FIRE.md)", aim_script_begin, aim_replay_frame, 0, 1,
     0, 0},
    {"aim_burst", "aim_05_burst_fire (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "START: the status screen's SPR4 page 00211970 and its SELECTOR part page 00217FA0 (3-round burst, "
     "D_00810C61 = 1), then R1 burst fire: 00170A60 states 0x14..0x17",
     "the original aim / fire path and the status pages (AIM_FIRE.md, STATUS_PAGES.md)", aim_script_begin,
     aim_replay_frame, 0, 1, 0, 1},
    {"aim_cable", "aim_10_cable_shots, then aim_11_cable_melee (side, from 08; decomp CAPTURES_C10.md AIM)", 0,
     "R1 / R2 rounds aimed at the security gun's cable (onto the pillar), then the knife at the cable's foot: "
     "the cable reaction 001EFE00 / 001EFEB0 / 0021AAC0 / 0021A500, the gun's lifecycle 2, taken bit 0x50",
     "the original aim / fire path and the cable reaction (AIM_FIRE.md)", aim_script_begin, aim_replay_frame, 0,
     1, 0, 0},
    {"br_cage_key", "br_09_cage_key (side, from 08; decomp CAPTURES_C10.md BRANCH)", 0x00219550u,
     "ladder A to the cage floor, pickup 00219550 g0.4 (item 0x32, puid 7): the take, the status page, the taken bit",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 0, 0},
    {"cage_ladders", "10_cage_roof_roger", 0,
     "ladder column x 360: Use 0015D4C0 case 0x32, entry 00165B60 (state 0xB), climb 001662D0 (state 0xC)",
     "the ladder entry and climb on the live record (census L09, L10)", cage_ladders_begin,
     cage_ladders_frame, 0, 0, 0, 0},
    {"cage_roof", "10_cage_roof_roger", 0x008253F0u,
     "director 0x8253F0 beat 0 script 0x8294C0 (260 <= Y <= 280, quad 0x82ABE0); Roger 0x8237E0 script "
     "0x828990 (voiced line 0x7F, VOICE.DAT cues 143..148); D_00810813 0 -> 1 -> 0x10 -> 0x11",
     "census L21 with WP-8b (the director on its original scripts, the voice lanes)", director_begin,
     cage_roof_frame, 0, 0, 0, 0},
    {"crevice_climbs", "11_crevice_prompt", 0,
     "tank ledge climb (state 2, +1F0 8), the pipes (fall 5 / 0xB), pipe-end ledge climb",
     "the ledge climb and fall on the live record (census L04, L02)", crevice_climbs_begin,
     crevice_climbs_frame, 0, 0, 0, 0},
    {"crevice_prompt", "11_crevice_prompt", 0x008253F0u,
     "director beat 1 script 0x829A40 (Y >= 275, quad 0x82AC20; line 0x97, VOICE.DAT cue 150); D_00810813 -> 0x20",
     "census L21 with WP-8b", director_begin, crevice_prompt_frame, 0, 0, 0, 0},
    {"dmg_flame", "dmg_00_flame_hit .. dmg_04_new_game (side, from 11; decomp CAPTURES_C10.md DAMAGE)", 0x008235F0u,
     "the flame's contact 0x823580 (001A8BE0 / 001A8660, 001EFE00(0x80000027): 0022BBC0), 0021C440's hits and "
     "flinch 0021D800, the low-health latch and heartbeat, death 0021E240 / 0021D2E0 (001F77B0), game over "
     "001AD4E0 (module 0x27), 001AC070 from a death, New Game to first control",
     "the DAMAGE step (docs/DAMAGE.md)", dmg_begin, dmg_frame, 0, 1, 0, 0},
    {"dmg_load", "dmg_00_flame_hit .. dmg_03, dmg_05_load_screen (side, from 11; decomp CAPTURES_C10.md DAMAGE)",
     0x00225AC0u,
     "the title after a death: Cross on LOAD GAME (001AC070 state 2's 00225A00, D_00275BE0 = 1), state 5's "
     "memory-card screen 00225AC0(0) (module 0x2A, the slot choice 00226070), Triangle back to the title menu",
     "the OPTIONS step (audit 1b item 15; docs/OPTIONS.md)", dmg_begin, dmg_frame, 0, 1, 0, 0},
    {"dmg_crevice_fall", "dmg_06_crevice_fall (side, from 11; decomp CAPTURES_C10.md DAMAGE)", 0,
     "a walking jump short of the north block: the landing hit 0017C580 / 00163E90 (+6 = 3), 0021C350",
     "the DAMAGE step (docs/DAMAGE.md)", dmg_begin, dmg_frame, 0, 1, 0, 0},
    {"br_plateau", "br_11_plateau_ladder_up .. br_13_plateau_ladder_down (side, from 11; decomp CAPTURES_C10.md BRANCH)", 0x00219550u,
     "the raised pipe's ledge climb and step-off, the plateau ladder up (0x15 / 0x17 / 0x18), pickup g0.2 (item 0x1F, puid 5) on the 355 top, the ladder down (the grab from above, 0x16)",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 0, 0},
    {"crevice_jump", "12_crevice_jump", 0,
     "running jump 0015EC50 / 001634A0 (+1F0 0x0C, state 6) onto the north block, landing 8 / 0xF",
     "the running jump on the live record (census L11)", crevice_jump_begin, crevice_jump_frame, 0, 0, 0, 0},
    {"east_tower_climb", "13_east_tower", 0, "high ledge climb (state 2, +1F0 8) onto the east tower top",
     "the ledge climb on the live record (census L04)", east_tower_climb_begin, east_tower_climb_frame, 0, 0, 0, 0},
    {"east_tower", "13_east_tower", 0x008253F0u,
     "director beat 2 script 0x829CC0 (Y >= 285, quad 0x82AC60; line 0x99, VOICE.DAT cue 149); D_00810813 -> 0xFF",
     "census L21 with WP-8b", director_begin, east_tower_frame, 0, 0, 0, 0},
    {"roger", "14_roger_encounter", 0x008237E0u,
     "running jump; Roger 0x8237E0 quad 0x82AB80, script 0x8283D0 (bank 96), 0x8107D8=1",
     "Roger's original owner and scripts (census L22)", roger_begin, roger_frame, 0, 0, 0, 0},
    {"br_roger_talk", "br_14_roger_talk (side, from 14; decomp CAPTURES_C10.md BRANCH)", 0x008237E0u,
     "Roger 0x8237E0 after the encounter (D_008107D8 = 1): the use scan marks him (+0x0B = 4), his third branch starts the talk script 0x828810",
     "the BRANCH step (audit 1b item 16; LEVEL_SMOKE.md \"The BRANCH side runs\")", br_begin, br_frame, 0, 1, 0, 0},
    {"exit", "exit_00_departure, exit_01_movie_arrival (decomp CAPTURES_C10.md EXIT; route beat 15)", 0x008237E0u,
     "fan r2 00827630's exit bit D_008107D8 |= 0x80; Roger 0x8237E0's departure script 0x828A10 (op0F: the "
     "movie E001.PSS), 001B0C60(1, 0, 4), 001AD010 / 001ADF50, 001FF080(1, 0) (001FFCD0: AREA01 sub 0), "
     "the AREA01 arrival 0x1AE040 state 0 (spawn entry 4)",
     "the level exit (audit 1b item 17; docs/FIRST_LEVEL_EXIT.md)", exit_begin, exit_frame, 0, 0, 0, 0},
    {"a01_arrival", "15_level_exit f741..f801 (AREA01 arrival and 60 neutral world ticks)", 0x001AE040u,
     "AREA01 sub 0 entry 4: the arrival rebuild followed by the original idle player, camera and owners",
     "AREA01 live binding (LEVEL2_BINDING.md); the route's last phase, every missing worker still fail-stops",
     a01_arrival_begin, a01_arrival_frame,
     0, 0, 0, 1},
    {"a01_s0", "a01_s0_npc_first_talk (side, from 15_level_exit)", 0,
     "NPC first talk from the completed AREA01 arrival idle", "AREA01 side route and strict capture comparison",
     a01_route_begin, a01_route_frame, 0, 1, 0, 1},
    {"a01_s2", "a01_s2_control_room_items (side, from a01_s0_npc_first_talk)", 0,
     "control-room items after the first NPC talk", "AREA01 side route and strict capture comparison",
     a01_route_begin, a01_route_frame, 0, 1, 1, 1},
    {"a01_s5", "a01_s5_duct (side, from a01_s0_npc_first_talk)", 0,
     "control-room duct and healing pickup after the first NPC talk", "AREA01 side route and strict capture comparison",
     a01_route_begin, a01_route_frame, 0, 1, 2, 1},
    {"a01_s3", "a01_s3_fire_contact (side, from 15_level_exit)", 0,
     "fire contact from the completed AREA01 arrival idle", "AREA01 side route and strict capture comparison",
     a01_route_begin, a01_route_frame, 0, 1, 0, 1},
    {"a01_s4", "a01_s4_east_room (side, from 15_level_exit)", 0,
     "east room and save terminal from the completed AREA01 arrival idle", "AREA01 side route and strict capture comparison",
     a01_route_begin, a01_route_frame, 0, 1, 0, 1},
    {"a01_s6", "a01_s6_bridge_blocked (side, from 15_level_exit)", 0,
     "blocked north bridge from the completed AREA01 arrival idle", "AREA01 side route and strict capture comparison",
     a01_route_begin, a01_route_frame, 0, 1, 0, 1},
    {"a01_00", "a01_00_train_room", 0,
     "AREA01 first-visit recorded pad route", "AREA01 route binding and tick comparison",
     a01_route_begin, a01_route_frame, 0, 0, 0, 1},
    {"a01_01", "a01_01_tunnel", 0,
     "AREA01 first-visit recorded pad route", "AREA01 route binding and tick comparison",
     a01_route_begin, a01_route_frame, 0, 0, 0, 1},
    {"a01_s1", "a01_s1_sentry_doc (side, from a01_01_tunnel)", 0,
     "sentry document after the recorded tunnel route", "AREA01 side route and strict capture comparison",
     a01_route_begin, a01_route_frame, 0, 1, 0, 1},
    {"a01_02", "a01_02_shaft_landing", 0,
     "AREA01 first-visit recorded pad route", "AREA01 route binding and tick comparison",
     a01_route_begin, a01_route_frame, 0, 0, 0, 1},
    {"a01_03", "a01_03_shaft_locked", 0,
     "shaft door locked script", "AREA01 route binding and tick comparison",
     a01_route_begin, a01_route_frame, 0, 0, 0, 1},
    {"a01_04", "a01_04_return_north", 0,
     "return through the train room to control-room entry 1", "AREA01 route binding and tick comparison",
     a01_route_begin, a01_route_frame, 0, 0, 0, 1},
    {"a01_05", "a01_05_npc_bridge_talk", 0,
     "NPC bridge talk script", "AREA01 route binding and tick comparison",
     a01_route_begin, a01_route_frame, 0, 0, 0, 1},
    {"a01_s7", "a01_s7_npc_third_talk (side, from a01_05_npc_bridge_talk)", 0,
     "NPC third talk after the recorded bridge talk", "AREA01 side route and strict capture comparison",
     a01_route_begin, a01_route_frame, 0, 1, 0, 1},
    {"a01_06", "a01_06_return_south", 0,
     "return through the tunnel to the shaft landing", "AREA01 route binding and tick comparison",
     a01_route_begin, a01_route_frame, 0, 0, 0, 1},
    {"a01_07", "a01_07_level_exit f0..f529 (AREA00 arrival before rebuild)", 0,
     "shaft door exit and original area load through AREA00 arrival state 0",
     "AREA01 gameplay ticks and loader events; stop before level 3 rebuild",
     a01_route_begin, a01_route_frame, 0, 0, 0, 1},
};
enum { PHASE_COUNT = (int)(sizeof k_phases / sizeof k_phases[0]) };

/* The first-control census (ORIGINAL_FRAME_ORDER.md section 4: nodes #0..#48,
 * record 13 freed on the second world frame, Q3). */
enum { FIRST_CONTROL_CENSUS = 49 };
/* Test timeouts (not game behaviour). */
enum { FIRST_CONTROL_TIMEOUT = 3000, STATUS_HOLD_FRAMES = 30, STATUS_CLOSE_TIMEOUT = 120 };

static struct {
    int active, failed, until, current, passed;
    int last_live;    /* index of the last phase that passed (-1: none) */
    int stop_pending; /* the run passed; quit after one more frame (finish) */
    uint32_t stop_counter; /* the main-loop counter of the frame that set stop_pending */
    /* status */
    int step, frames, close_frames, resumed;
    /* the hub's draws: D_002655A0 steps and UI+0x20 against the hub
     * frames the page core ran at sub-state 1 */
    unsigned long hub_background_base;
    unsigned hub_draws;
    unsigned long hub_models_base;
    uint8_t hub_phase, hub_step;
    float frozen_pos[3], frozen_eye[3];
    int32_t frozen_variants;
    /* the pad-input navigation of the route phases (nav_*) */
    int pad_on;
    EmPadState pad;
    int nav_frames, nav_hist_n;
    float nav_hist[64][2];
    int32_t scan_variants; /* D_00810750 at the Use scan's tick (scan_accepted) */
    uint8_t saw[8];        /* per-phase observations (see each runner) */
    int page_capture_frames; /* EM_LEVEL_SMOKE_PAGE_CAPTURE (page_capture) */
} t;

static void fail(const char *reason)
{
    fprintf(stderr, "level smoke: FAIL phase=%s frame=%d: %s\n",
            t.current < PHASE_COUNT ? k_phases[t.current].name : "-", g.frame_no, reason);
    t.failed = 1;
    em_frame_request_quit();
}

static void pad_key(int key, int down)
{
    EmEvent event = {0};
    event.type = down ? EM_EVENT_KEY_DOWN : EM_EVENT_KEY_UP;
    event.key = key;
    em_input_handle_event(&event);
}

static uint8_t task_byte(unsigned offset)
{
    EmTask *task = em_task_current(); /* the hook runs inside the slot-0 task */
    const uint8_t *b = task ? em_scene_task_byte(task->user, offset) : NULL;
    return b ? *b : 0xFF;
}

/* A side phase the run to `until` plays: `until` itself, or the side phase
 * it starts from (Phase.from_side). */
static int side_played(int i)
{
    return i == t.until || (k_phases[t.until].from_side && i == t.until - k_phases[t.until].from_side);
}

static void report_not_live(int from)
{
    for (int i = from; i <= t.until; ++i) {
        const Phase *p = &k_phases[i];
        if (p->side && !side_played(i))
            continue;
        const char *binding = p->owner ? em_scene_bindings_pool_binding(p->owner) : NULL;
        char owner[160] = "";
        if (p->owner)
            snprintf(owner, sizeof owner, "; port binding of %08X: %s", (unsigned)p->owner,
                     binding ? binding : "no live node");
        fprintf(stderr, "level smoke: %s: NOT-LIVE (route beat %s; original: %s%s; lands with %s)%s\n",
                p->name, p->beat, p->original, owner, p->lands,
                i > from && p->begin ? " [runner present, not reached]" : "");
    }
}

static void finish(void)
{
    fprintf(stderr, "level smoke: PASS %d live phase%s through %s", t.passed, t.passed == 1 ? "" : "s",
            k_phases[t.last_live].name);
    if (t.current <= t.until)
        fprintf(stderr, "; NOT-LIVE from %s through %s (not verified)\n", k_phases[t.current].name,
                k_phases[t.until].name);
    else
        fputc('\n', stderr);
    /* The stream drive's mode and counters over the run (IOP_STREAM.md
     * "Drive model"; tools/test_level_smoke.py reads the mode from this line
     * and checks the voiced lines and the opening's end for it). */
    em_stream_live_drive_report(stderr);
    em_scene_bindings_module_loader_report(stderr);
    /* Hand the pad back to the keyboard map (the navigation below drives
     * it through the gamepad overlay, em_input_set_gamepad). */
    if (t.pad_on)
        em_input_set_gamepad(NULL);
    t.pad_on = 0;
    /* The authorized level-2 endpoint is AREA00 arrival state 0, before
     * any level-3 rebuild. Finish this frame and use its explicit tail;
     * the usual extra frame would execute the out-of-scope rebuild. */
    if(t.last_live>=0 && !strcmp(k_phases[t.last_live].name,"a01_07")) {
        em_scene_bindings_log_request_tail();
        em_frame_request_quit();
        return;
    }
    /* Quit after one more frame, not now. The route captures sample after
     * the original frame, and the original's 0x28A9A0 fade ticks after the
     * slot-0 task, so the port's post-frame fade is the next tick's start
     * sample in the scene tick log (em_scene_bindings.c log_tick_begin);
     * tools/test_level_smoke.py needs that tick after the last live phase.
     * The extra frame takes no input (test driver, not game behaviour). */
    t.stop_pending = 1;
    t.stop_counter = em_frame_counter();
}

/* The extra frame's end (finish): quit, or fail when it faulted. Run by the
 * hook of a later frame than the one that set stop_pending. 1 when handled. */
static int stop_frame(void)
{
    if (!t.stop_pending || em_frame_counter() == t.stop_counter)
        return 0;
    t.stop_pending = 0;
    if (em_scene_faulted(em_scene_state())) {
        fail("the scene coordinator faulted on the frame after the last phase");
    } else {
        /* The exit's last frame is the AREA01 arrival, which has no next
         * tick to carry its post-frame values: the tick log's tail. */
        if (t.last_live >= 0 && k_phases[t.last_live].frame == exit_frame)
            em_scene_bindings_log_request_tail();
        em_frame_request_quit();
    }
    return 1;
}

/* Advance to the next phase, or stop at the first NOT-LIVE one. A driven
 * phase (Phase.driven) is reported NOT-LIVE, never counted as passed. */
static void next_phase(void)
{
    const Phase *done = &k_phases[t.current];
    /* Verification aid (LEVEL_SMOKE.md "Frame captures"):
     * EM_LEVEL_SMOKE_PHASE_CAPTURE=<phase>:<path.bmp> saves the frame that
     * ends <phase>, for a look beside the route beat's original.png. */
    const char *pc = getenv("EM_LEVEL_SMOKE_PHASE_CAPTURE");
    const char *colon = pc ? strchr(pc, ':') : NULL;
    if (colon && colon[1] && (size_t)(colon - pc) == strlen(done->name) &&
        strncmp(pc, done->name, (size_t)(colon - pc)) == 0)
        em_gfx_request_capture(em_frame_gfx(), colon + 1);
    /* The fb2 pixel harness (tools/test_fb2_pixels.py):
     * EM_LEVEL_SMOKE_FB_CAPTURE=<phase>+<k>:<path.bmp> saves the frame of
     * the k-th scene tick after the one that ended <phase> (k >= 1; the
     * hook runs after that tick's frame). The harness checks the logged
     * tick against the route alignment before it compares a pixel. */
    const char *fc = getenv("EM_LEVEL_SMOKE_FB_CAPTURE");
    const char *plus = fc ? strchr(fc, '+') : NULL;
    const char *fcolon = plus ? strchr(plus, ':') : NULL;
    if (fcolon && fcolon[1] && (size_t)(plus - fc) == strlen(done->name) &&
        strncmp(fc, done->name, (size_t)(plus - fc)) == 0) {
        const int k = atoi(plus + 1);
        if (k >= 1) {
            const uint32_t tick = em_scene_bindings_log_tick_next() + (uint32_t)(k - 1);
            em_scene_bindings_capture_tick(tick, fcolon + 1);
            fprintf(stderr, "level smoke: fb capture: %s+%d = tick %u\n", done->name, k, (unsigned)tick);
        }
    }
    if (done->driven) {
        const char *binding = done->owner ? em_scene_bindings_pool_binding(done->owner) : NULL;
        fprintf(stderr, "level smoke: %s: NOT-LIVE driven (route beat %s; original: %s; port binding of "
                "%08X: %s; lands with %s): driven through that binding only so the later phases start "
                "from its state; not verified\n", done->name, done->beat, done->original,
                (unsigned)done->owner, binding ? binding : "no live node", done->lands);
    } else {
        ++t.passed;
        t.last_live = t.current;
    }
    ++t.current;
    /* The main line passes a side beat by: named, not run (it starts from
     * its own snapshot in the route). */
    while (t.current < t.until && k_phases[t.current].side && !side_played(t.current)) {
        const Phase *p = &k_phases[t.current];
        if (p->begin) {
            fprintf(stderr, "level smoke: %s: side beat, not on the main line (route beat %s; live, run on its "
                    "own with EM_LEVEL_SMOKE_UNTIL=%s)\n", p->name, p->beat, p->name);
        } else {
            const char *binding = em_scene_bindings_pool_binding(p->owner);
            fprintf(stderr, "level smoke: %s: side beat, not on the main line (route beat %s; NOT-LIVE: "
                    "original: %s; port binding of %08X: %s; lands with %s)\n", p->name, p->beat, p->original,
                    (unsigned)p->owner, binding ? binding : "no live node", p->lands);
        }
        ++t.current;
    }
    memset(t.saw, 0, sizeof t.saw);
    t.step = 0;
    if (t.current > t.until) {
        finish();
        return;
    }
    if (!k_phases[t.current].begin) {
        report_not_live(t.current);
        finish();
        return;
    }
    k_phases[t.current].begin();
}

/* ------------------------------------------------------- first_control */

static void first_control_begin(void) {}

/* The end of the first world frame with the opening runtime finished and the
 * fade clear (the newgame-control handoff). Original: gameplay runs 0x1AE040
 * state 1 (+B = 1, +C = 0) under 001ACEC0 +8 = 3 and 001AD250 +9 = 1 with
 * selector 3B8D = 0 (ORIGINAL_FRAME_ORDER.md section 2, status_04.json frame
 * 0), in area 0x0B room 0 (route 01 row f0), with 49 pool nodes. */
static int first_control_frame(void)
{
    if (g.frame_no > FIRST_CONTROL_TIMEOUT) {
        fail("no first control within the opening timeout");
        return 0;
    }
    if (em_opening_runtime_busy() || em_frame_transition()->substate != 0)
        return 0;
    const EmSceneState *s = em_scene_state();
    int census = em_scene_bindings_pool_census();
    uint8_t b8 = task_byte(EM_SCENE_TASK_08), b9 = task_byte(EM_SCENE_TASK_09);
    uint8_t bb = task_byte(EM_SCENE_TASK_0B), bc = task_byte(EM_SCENE_TASK_0C);
    if (b8 != 3 || b9 != 1 || bb != 1 || bc != 0 || s->spad3B8D != 0 || census != FIRST_CONTROL_CENSUS ||
        s->d810700 != 0x0B || s->d810701 != 0) {
        fprintf(stderr, "level smoke: first_control: task +8/+9/+B/+C=%u/%u/%u/%u 3B8D=%u census=%d area=%02X/%u\n",
                b8, b9, bb, bc, s->spad3B8D, census, s->d810700, s->d810701);
        fail("first control is not gameplay state 1 with selector 0 and the original 49 nodes");
        return 0;
    }
    fprintf(stderr, "level smoke: first_control: PASS frame=%d task=%u/%u/%u/%u selector=%u census=%d "
            "d810750=%d\n", g.frame_no, b8, b9, bb, bc, s->spad3B8D, census, (int)s->d810750);
    return 1;
}

/* -------------------------------------------------------------- status */

static int world_unchanged(void)
{
    for (unsigned axis = 0; axis < 3; ++axis)
        if (g.pos[axis] != t.frozen_pos[axis] || g.cam.eye[axis] != t.frozen_eye[axis])
            return 0;
    return em_scene_state()->d810750 == t.frozen_variants;
}

/* START at the end of the first-control frame. It reaches D_00810E74 at the
 * next tick's step C, so 001AE7E0 returns 2 before another world frame. */
static void status_begin(void)
{
    memcpy(t.frozen_pos, g.pos, sizeof t.frozen_pos);
    memcpy(t.frozen_eye, g.cam.eye, sizeof t.frozen_eye);
    t.frozen_variants = em_scene_state()->d810750;
    t.step = 1;
    t.hub_background_base = em_status_background_live_steps();
    t.hub_models_base = em_status_models_drawn(em_area11_interaction_host_status_models());
    t.hub_draws = 0;
    t.hub_phase = t.hub_step = 0;
    pad_key(EM_KEY_RETURN, 1); /* START */
}

/* 0020CDC0 phase 1 draws only at sub-state 1: 0020A7A0 (one D_002655A0
 * step) and 00209DF0 (00208AD0 advances UI+0x20 once). A frame whose tick
 * started at sub-state 1 drew the hub, the close-edge frame included;
 * the cold entry, sub-state 0 and the 0020E0C0 exit frames draw nothing. */
static int hub_draws_checked(void)
{
    const EmStatusRuntime *status = em_area11_interaction_host_status();
    const EmStatusPage *page = status ? em_status_runtime_page(status) : NULL;
    if (!page) {
        fail("the AREA11 status runtime is missing");
        return 0;
    }
    if (t.hub_phase == 1 && t.hub_step == 1)
        ++t.hub_draws;
    t.hub_phase = page->phase;
    t.hub_step = page->step;
    if (em_status_background_live_steps() - t.hub_background_base != t.hub_draws ||
        (page->phase == 1 && em_status_runtime_ui_clock(status) != t.hub_draws)) {
        fail("the hub did not step 0020A7A0 and draw 00209DF0 exactly once per sub-state-1 frame");
        return 0;
    }
    /* The models (em_status_models): 0020CDC0 sub-state 0 fills the pool
     * D_0028B020 as in the status-hub capture (record 0 the menu player
     * 0020E6F0, records 1..6 the letters 0020E460 with the glyphs 0020E250
     * derives from CA4..CA7 = FF 05 00 07: '/', '@', '0', '1', '2', '8').
     * The first sub-state-1 walk only initialises them (0020E6F0 and
     * 0020E460 state 0 draw nothing); every later hub frame draws each
     * record once (001CB580). */
    const EmStatusModels *models = em_area11_interaction_host_status_models();
    const EmStatusScenePool *pool = em_status_models_pool(models);
    static const uint8_t glyphs[6] = {0x2F, 0x40, 0x30, 0x31, 0x32, 0x38};
    unsigned used = 0;
    for (unsigned i = 0; pool && i < EM_STATUS_SCENE_POOL_RECORDS; ++i)
        used += pool->record[i].b00 != 0;
    unsigned long drawn = em_status_models_drawn(models) - t.hub_models_base;
    if (!pool || (t.hub_draws && page->phase == 1 &&
                  (used != 7 || pool->record[0].w10 != 0x0020E6F0u ||
                   drawn != 7ul * (t.hub_draws - 1)))) {
        fail("the hub models are not the capture's seven records drawn once per hub frame");
        return 0;
    }
    for (unsigned i = 0; t.hub_draws && page->phase == 1 && i < 6; ++i)
        if (pool->record[1 + i].w10 != 0x0020E460u || pool->record[1 + i].b0D != glyphs[i]) {
            fail("the hub's letter models are not the status-hub capture's");
            return 0;
        }
    /* Verification aid: EM_LEVEL_SMOKE_HUB_CAPTURE=<path.bmp> writes the
     * hub frame whose walk equals the status-hub capture (walk 10, the
     * menu player's captured breathe/yaw; tests/status_models_test.c). */
    const char *capture = getenv("EM_LEVEL_SMOKE_HUB_CAPTURE");
    if (capture && *capture && t.hub_draws == 9 && page->phase == 1 && page->step == 1)
        em_gfx_request_capture(em_frame_gfx(), capture);
    return 1;
}

/* Original (ORIGINAL_FRAME_ORDER.md section 2 and Q7; status_04.json): the
 * r == 2 tick and state 3 sub-step 0 draw no frame; every later state-3
 * frame runs 0020CDC0 and 001D1EA0(0) with the world frozen (no variant: no
 * D_00810750 increment, no player or camera update) and 3B8D stays 0; the
 * hub's close leads to +B = 5, one state-5 tick without a frame, then state
 * 1 with one 001AE5E0 per tick, under the 001AEE40(0x20) fade-in. The request
 * bytes B0 and C5 are clear after the close. The tick-exact sequence and the
 * exit fade are compared with the captures by tools/test_level_smoke.py. */
static int status_frame(void)
{
    const EmSceneState *s = em_scene_state();
    uint8_t state = task_byte(EM_SCENE_TASK_0B);
    if (s->spad3B8D != 0) {
        fail("selector 3B8D left 0 during a status screen opened from gameplay");
        return 0;
    }
    if (t.step == 1 || t.step == 2) {
        if (t.step == 1)
            pad_key(EM_KEY_RETURN, 0);
        if (state != 3 || task_byte(EM_SCENE_TASK_0C) != 1 || !world_unchanged()) {
            fail(t.step == 1 ? "START did not open the status screen with the world frozen"
                             : "the world advanced while the status screen showed");
            return 0;
        }
        t.step = 2;
        if (!hub_draws_checked())
            return 0;
        if (++t.frames == STATUS_HOLD_FRAMES) {
            pad_key('i', 1); /* TRIANGLE */
            t.step = 3;
        }
        return 0;
    }
    if (t.step == 3) {
        if (t.close_frames++ == 0)
            pad_key('i', 0);
        if (s->d810750 == t.frozen_variants) {
            if (state == 3 && !hub_draws_checked())
                return 0;
            if ((state != 3 && state != 5) || !world_unchanged()) {
                fail("the world advanced before the status screen closed");
                return 0;
            }
            if (t.close_frames > STATUS_CLOSE_TIMEOUT)
                fail("TRIANGLE did not close the status screen");
            return 0;
        }
        t.step = 4;
    }
    /* Resumed: state 1, one gameplay variant per frame. */
    if (state != 1 || s->d810750 != t.frozen_variants + 1 + t.resumed) {
        fail("the frames after the close are not one gameplay variant per frame in state 1");
        return 0;
    }
    if (s->req[EM_SCENE_REQ_B0] != 0 || s->req[EM_SCENE_REQ_C5] != 0) {
        fail("status request bytes B0/C5 still set after the close");
        return 0;
    }
    ++t.resumed;
    if (em_frame_transition()->substate != 0)
        return 0;
    if (em_status_background_live_steps() - t.hub_background_base != t.hub_draws ||
        t.hub_draws < 2) {
        fail("0020A7A0 stepped outside the hub's sub-state-1 frames");
        return 0;
    }
    fprintf(stderr, "level smoke: status: PASS status_frames=%d close_frames=%d resumed_frames=%d "
            "variants_frozen_at=%d hub_draws=%u\n", t.frames, t.close_frames, t.resumed,
            (int)t.frozen_variants, t.hub_draws);
    return 1;
}

/* ------------------------------------------------ route navigation (pad only)
 *
 * The closed loop of route_capture.py (FIRST_LEVEL_ROUTE.md section 1):
 * the left stick points at a world (x, z) target relative to the camera
 * forward D_00810600 (g.cam.fwd): stick up = forward, stick right =
 * (-fz, fx). It drives the analog stick through the gamepad overlay
 * (em_input_set_gamepad), the pad path a DualShock takes; every value here
 * is test input, not game behaviour. */
enum { NAV_LIMIT = 900, NAV_STUCK_WINDOW = 30 };

static void pad_apply(uint16_t buttons, float lx, float ly)
{
    t.pad = (EmPadState){buttons, lx, ly, 0, 0};
    t.pad_on = 1;
    em_input_set_gamepad(&t.pad);
}

static float nav_stick_toward(float x, float z, float magnitude)
{
    float dx = x - g.pos[0], dz = z - g.pos[2];
    float dist = sqrtf(dx * dx + dz * dz);
    float fx = g.cam.fwd[0], fz = g.cam.fwd[2];
    float norm = sqrtf(fx * fx + fz * fz);
    if (norm <= 0)
        norm = 1;
    fx /= norm;
    fz /= norm;
    float d = dist > 0 ? dist : 1;
    float up = (dx * fx + dz * fz) / d;
    float right = (-dx * fz + dz * fx) / d;
    pad_apply(0, magnitude * right, -magnitude * up);
    return dist;
}

static void nav_reset(void)
{
    t.nav_frames = 0;
    t.nav_hist_n = 0;
}

/* 1 reached (or stuck with stuck_ok), 0 continue, -1 failed (reported). */
static int nav_goto(float x, float z, float tol, float magnitude, int stuck_ok)
{
    if (nav_stick_toward(x, z, magnitude) <= tol) {
        pad_apply(0, 0, 0);
        nav_reset();
        return 1;
    }
    if (++t.nav_frames > NAV_LIMIT) {
        fail("navigation did not reach its target");
        return -1;
    }
    int slot = t.nav_hist_n % 64;
    t.nav_hist[slot][0] = g.pos[0];
    t.nav_hist[slot][1] = g.pos[2];
    ++t.nav_hist_n;
    if (t.nav_hist_n > NAV_STUCK_WINDOW) {
        int old = (t.nav_hist_n - 1 - NAV_STUCK_WINDOW) % 64;
        float mx = g.pos[0] - t.nav_hist[old][0], mz = g.pos[2] - t.nav_hist[old][1];
        if (sqrtf(mx * mx + mz * mz) < 0.05f) {
            pad_apply(0, 0, 0);
            nav_reset();
            if (stuck_ok)
                return 1;
            fail("navigation stuck");
            return -1;
        }
    }
    return 0;
}

static int in_control(void)
{
    return em_scene_state()->spad3B8D == 0 && !player_pose_owned() &&
           !em_game_player_interact_busy() && task_byte(EM_SCENE_TASK_0B) == 1;
}

static int idle_clip(void)
{
    unsigned clip;
    return player_pose_source(&clip, NULL, NULL, NULL) && clip == 0;
}

/* route_capture settle(): idle `frames`, then wait for control and the idle
 * clip. 1 done, 0 continue, -1 failed. */
static int nav_settle(int frames)
{
    pad_apply(0, 0, 0);
    ++t.nav_frames;
    if (t.nav_frames <= frames)
        return 0;
    if (in_control() && idle_clip()) {
        nav_reset();
        return 1;
    }
    if (t.nav_frames > frames + 600) {
        fail("control did not return (settle)");
        return -1;
    }
    return 0;
}

/* route_capture face(): short stick taps toward body yaw `yaw` (X = sin,
 * Z = cos) until within 0.12, at most 40 frames. */
static int nav_face(float yaw)
{
    float diff = fmodf(yaw - g.yaw + 3.14159265f, 6.28318531f);
    if (diff < 0)
        diff += 6.28318531f;
    diff -= 3.14159265f;
    if (fabsf(diff) <= 0.12f || ++t.nav_frames > 40) {
        pad_apply(0, 0, 0);
        nav_reset();
        return 1;
    }
    nav_stick_toward(g.pos[0] + 100 * sinf(yaw), g.pos[2] + 100 * cosf(yaw), 0.6f);
    return 0;
}

/* Hold `buttons` for `frames` frames, then release. */
static int nav_press(uint16_t buttons, int frames)
{
    if (t.nav_frames++ < frames) {
        pad_apply(buttons, 0, 0);
        return 0;
    }
    pad_apply(0, 0, 0);
    nav_reset();
    return 1;
}

/* Run the navigation step `r` of a phase: advance on 1, stop on -1. */
#define NAV_STEP(expr)                                                                             \
    do {                                                                                           \
        int r_ = (expr);                                                                           \
        if (r_ < 0)                                                                                \
            return 0;                                                                              \
        if (r_ > 0)                                                                                \
            ++t.step;                                                                              \
        return 0;                                                                                  \
    } while (0)

static int power_bit(void)
{
    return em_game_terminal_powered();
}

/* The scan tick: 00184BA0 accepted the Use (3B8D = 3; the terminal's
 * script turns it into 2 on the next frame, the panel's in the same frame,
 * as in route beats 02 f200 and 03 f231), latched on every frame from the
 * press on as the first frame with 3B8D != 0. The capture checks align on
 * it (tools/test_level_smoke.py). */
static int scan_accepted(void)
{
    if (!t.saw[7] && em_scene_state()->spad3B8D != 0) {
        t.saw[7] = 1;
        t.scan_variants = em_scene_state()->d810750;
    }
    return t.saw[7];
}

/* ---------------------------------------------------- panel_no_battery
 *
 * Route beat 00 (a side beat from slot 04, the first-control state):
 * without item 0x1B, Cross at the panel 00159210 starts 0x246F20 (the scan
 * and the script's op07/2 in the same frame, 3B8D 0 -> 2, f75; the player
 * placed at (239.7, y, 223.8) facing 0), message 0x80000018 in mode 2
 * (f79..f229), the bars, then the release at f230. The runner walks from
 * first control to the route's press stance (242.605, 226.742; f71) and
 * faces its heading 0.69894 (navigation input, as the battery's), then
 * presses Cross as route_capture's beat_panel_no_battery does. In process:
 * the scan, the message, the letterbox and camera byte 1 were seen, no
 * item and no power, and control returns. tools/test_level_smoke.py
 * check_panel_no_battery compares the capture row for row. Run on its
 * own: EM_LEVEL_SMOKE_UNTIL=panel_no_battery (make test-level-smoke-full). */
static void panel_no_battery_begin(void)
{
    nav_reset();
    if (em_pickup_item_count(0x1B) != 0 || power_bit())
        fail("route beat 00 starts without item 0x1B and without power");
}

static int panel_no_battery_frame(void)
{
    const EmMessageBlock *message = em_message_live_block();
    if (message && message->phase && message->line == 0x80000018u)
        t.saw[0] = 1;
    if (em_frame_screen_fade()->state == 3 || em_frame_screen_fade()->state == 1)
        t.saw[1] = 1;
    if (g.cam.top_mode == 1)
        t.saw[2] = 1;
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(242.605f, 226.742f, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(242.605f, 226.742f, 0.1f, 0.4f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(0.69894f));
    case 4: NAV_STEP(nav_settle(10));
    case 5:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6:
        if (scan_accepted()) {
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 60) {
            fail("Cross at the panel did not win the use scan (3B8D stayed 0)");
        }
        return 0;
    case 7:
        if (!in_control()) {
            if (++t.nav_frames > 1000)
                fail("the panel's 0x246F20 did not release the player");
            return 0;
        }
        if (!t.saw[0] || !t.saw[1] || !t.saw[2]) {
            fprintf(stderr, "level smoke: panel_no_battery: message 0x80000018 %s, letterbox %s, camera byte 1 "
                    "%s\n", t.saw[0] ? "seen" : "missing", t.saw[1] ? "seen" : "missing",
                    t.saw[2] ? "seen" : "missing");
            fail("the panel without the battery did not run 0x246F20's message, letterbox and camera");
            return 0;
        }
        if (em_pickup_item_count(0x1B) != 0 || power_bit()) {
            fail("the panel without the battery changed the item or the power");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    case 8: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        fprintf(stderr, "level smoke: panel_no_battery: PASS scan_d810750=%d player=(%.3f,%.5f,%.3f) "
                "yaw=%.5f\n", (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------- battery
 *
 * Route beat 01 (live since WP-6): the battery pickup g0.0 (00219550, item
 * 0x1B) at (211.6, 229.9, 227.2). The runner walks to the route's stance
 * before its press (218.212, 222.373, facing -0.9588; f118..f124) and
 * presses Cross as route_capture's beat_battery does. 00184BA0 arms the
 * original owner (3B8D = 3, the scan tick is printed); its take program
 * 0x266620 turns the player, plays the grab clip, settles the camera target
 * and consumes the item (001C47A0: 001C40B0, B0 = 1, B1 = 0x1B), and the
 * status screen pops up on it (route f189 post, f192 open). In process:
 * the screen opens (+B = 3) with item 0x1B taken, the page is the ITEM root
 * in its BATTERY child (0020EE50 state 5 after module 0x21) showing
 * 002149F0's acquisition notice (sub-state 3) with charge and capacity 12,
 * the request consumed (B0 = 0); the notice hands over to the list
 * (sub-state 1) after route 01's 239 frames; TRIANGLE 20 frames later
 * (route f459 -> f479) closes the screen, control returns and the owner has
 * set its taken bit. tools/test_level_smoke.py check_battery compares the
 * take with route 01 row for row (LEVEL_SMOKE.md). */
/* ------------------------------------------------------------ status_pages
 *
 * A designed side run from first control (no route capture shows a status
 * page open; docs/STATUS_PAGES.md section 6): the pad opens every status
 * page the first level reaches from the hub (DATABASE, SPR4 with the LOWER
 * U.R.S. and SELECTOR SWITCH part pages, MAP with no map owned, and the
 * ITEM children EQUIPMENT, EVENT and HEALING) and backs out of each with
 * Circle (pad bit 0x20; the pages' back), Triangle (0x10) and START (0x800)
 * closing the screen, then the take of each item type with a page (0x1E
 * and 0x1F HEALING, the key 0x32 DATABASE, the magazine 0x10 SPR4, the map
 * 0x08 MAP) runs its original owner (the use claim the scan makes, taken
 * directly: test input) and the page it opens, including a medicine used
 * from health 30 (test input) through 002160B0's count-up and 0015C700, and
 * the map: MAP opens zoomed on map 8 (its two model nodes lit and drawn),
 * R1 zooms, the D-pad pans, Circle to the list, Cross zooms again (the
 * player's marker, the current map), Circle twice to the hub; then MAP from
 * the hub with map 8 owned.
 * Every page call is traced (EM_STATUS_PAGES_TRACE) and the checker replays
 * each through the original instructions (tools/test_status_pages_live.py).
 * Every value here is test input, not game behaviour. */
enum { SP_PRESS = 1, SP_STICK, SP_HOLD, SP_WAIT, SP_BACK, SP_HOVER, SP_TAKE, SP_HEALTH, SP_CHECK,
       SP_END };
typedef struct {
    int op;
    uint16_t buttons; /* SP_PRESS */
    float lx, ly;     /* SP_STICK */
    int frames;       /* SP_PRESS / SP_HOLD */
    int phase, screen, child; /* SP_WAIT: page core t[1], t[0x10], ITEM t[4] (-1: any) */
    int value;        /* SP_HOVER (t[0x11]), SP_TAKE (uid), SP_CHECK (what) */
    const char *what;
} PageStep;

#define PRESS(b) {SP_PRESS, (b), 0, 0, 2, 0, 0, 0, 0, NULL}
#define STICK(x, y) {SP_STICK, 0, (x), (y), 0, 0, 0, 0, 0, NULL}
#define HOLD(n) {SP_HOLD, 0, 0, 0, (n), 0, 0, 0, 0, NULL}
#define WAIT(ph, sc, ch, w) {SP_WAIT, 0, 0, 0, 0, (ph), (sc), (ch), 0, (w)}
/* Circle (a 2-frame press every 20 frames, at most 5) until WAIT's state. */
#define BACK(ph, sc, ch, w) {SP_BACK, 0, 0, 0, 0, (ph), (sc), (ch), 0, (w)}
#define HOVER(v, w) {SP_HOVER, 0, 0, 0, 0, 0, 0, 0, (v), (w)}
#define TAKE(u, w) {SP_TAKE, 0, 0, 0, 0, 0, 0, 0, (u), (w)}
enum { CHECK_FIRE_MODE = 1, CHECK_HEALTH_60, CHECK_HEALTH_100, CHECK_CLOSED };
#define CHECK(v, w) {SP_CHECK, 0, 0, 0, 0, 0, 0, 0, (v), (w)}

static const PageStep k_page_steps[] = {
    /* 120 idle gameplay frames first: the run's whole-run checks (the render
     * context, the rand() order) need gameplay ticks, not status frames. */
    HOLD(120), PRESS(EM_PAD_START), WAIT(1, -1, -1, "START opens the hub"),
    /* DATABASE: hub hover 1 (stick down), the category list, D-pad right
     * (the next category), Cross, Circle twice back to the hub. */
    STICK(0, 1), HOVER(1, "hub hover 1 (DATABASE)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    WAIT(3, 3, -1, "Cross opens DATABASE"), HOLD(30), PRESS(EM_PAD_RIGHT), HOLD(20),
    PRESS(EM_PAD_CROSS), HOLD(30), BACK(1, -1, -1, "Circle returns from DATABASE to the hub"),
    /* SPR4: hub hover 2 (right); the part selector (0020D930 mode 2): hover 6
     * (down-left) LOWER U.R.S., Cross on its equipped entry (refused),
     * Circle; hover 4 (right) SELECTOR SWITCH, down to the other fire
     * mode, Cross, Yes (left), Cross; Circle back to the hub. */
    STICK(1, 0), HOVER(2, "hub hover 2 (SPR4)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    WAIT(3, 2, -1, "Cross opens SPR4"), HOLD(30),
    STICK(-0.7071f, 0.7071f), HOVER(6, "SPR4 hover 6 (LOWER U.R.S.)"), PRESS(EM_PAD_CROSS),
    STICK(0, 0), HOLD(40), PRESS(EM_PAD_CROSS), HOLD(30), BACK(3, 2, -1, "Circle returns to the selector"),
    HOLD(30),
    STICK(1, 0), HOVER(4, "SPR4 hover 4 (SELECTOR SWITCH)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    HOLD(40), PRESS(EM_PAD_DOWN), HOLD(20), PRESS(EM_PAD_CROSS), HOLD(10), PRESS(EM_PAD_LEFT),
    HOLD(10), PRESS(EM_PAD_CROSS), HOLD(30), CHECK(CHECK_FIRE_MODE, "SELECTOR's Yes set D_00810C61"),
    BACK(1, -1, -1, "Circle returns from SPR4 to the hub"),
    /* MAP: hub hover 3 (up); no map is owned yet: the list view with no
     * model; Circle back to the hub. */
    STICK(0, -1), HOVER(3, "hub hover 3 (MAP)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    WAIT(3, 1, -1, "Cross opens MAP"), HOLD(30), PRESS(EM_PAD_DOWN), HOLD(10),
    BACK(1, -1, -1, "Circle returns from MAP to the hub"),
    /* ITEM: hub hover 4 (left); the root's hovers 1 (down) EQUIPMENT, 4
     * (left) EVENT and 5 (down-left) HEALING, each opened and left. */
    STICK(-1, 0), HOVER(4, "hub hover 4 (ITEM)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    WAIT(3, 0, 1, "Cross opens the ITEM root"), HOLD(10),
    STICK(0, 1), HOVER(1, "ITEM hover 1 (EQUIPMENT)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    WAIT(3, 0, 4, "Cross opens EQUIPMENT"), HOLD(30), PRESS(EM_PAD_CROSS), HOLD(20),
    BACK(3, 0, 1, "Circle returns from EQUIPMENT to the root"), HOLD(10),
    STICK(-1, -0.2f), HOVER(4, "ITEM hover 4 (EVENT)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    WAIT(3, 0, 6, "Cross opens EVENT"), HOLD(30), PRESS(EM_PAD_CROSS), HOLD(10),
    BACK(3, 0, 1, "Circle returns from EVENT to the root"), HOLD(10),
    STICK(-0.7071f, 0.7071f), HOVER(5, "ITEM hover 5 (HEALING)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    WAIT(3, 0, 7, "Cross opens HEALING"), HOLD(30),
    BACK(3, 0, 1, "Circle returns from HEALING to the root"), HOLD(10),
    BACK(1, -1, -1, "Circle returns from ITEM to the hub"), HOLD(10), PRESS(EM_PAD_START),
    CHECK(CHECK_CLOSED, "START closes the status screen"),
    /* The takes. 0x1E from health 30: the notice, Cross (skips it), Cross
     * (the prompt, No), left (Yes), Cross: the count-up to 60, START. */
    {SP_HEALTH, 0, 0, 0, 0, 0, 0, 0, 30, "health set to 30 (test input)"},
    TAKE(0x0B04, "the take of 0x1E (0x0B04)"), WAIT(3, 0, 7, "the 0x1E take opens HEALING"),
    HOLD(20), PRESS(EM_PAD_CROSS), HOLD(10), PRESS(EM_PAD_CROSS), HOLD(10), PRESS(EM_PAD_LEFT),
    HOLD(10), PRESS(EM_PAD_CROSS), HOLD(200), CHECK(CHECK_HEALTH_60, "0x1E's count-up ends at 60"),
    PRESS(EM_PAD_START), CHECK(CHECK_CLOSED, "START closes the status screen"),
    /* 0x1F: the same use, to 100. */
    TAKE(0x0B05, "the take of 0x1F (0x0B05)"), WAIT(3, 0, 7, "the 0x1F take opens HEALING"),
    HOLD(20), PRESS(EM_PAD_CROSS), HOLD(10), PRESS(EM_PAD_CROSS), HOLD(10), PRESS(EM_PAD_LEFT),
    HOLD(10), PRESS(EM_PAD_CROSS), HOLD(200), CHECK(CHECK_HEALTH_100, "0x1F's count-up ends at 100"),
    PRESS(EM_PAD_START), CHECK(CHECK_CLOSED, "START closes the status screen"),
    /* The key 0x32: DATABASE on its record; Circle to the list, START. */
    TAKE(0x0B07, "the take of the key 0x32 (0x0B07)"), WAIT(3, 3, -1, "the 0x32 take opens DATABASE"),
    HOLD(40), PRESS(EM_PAD_CIRCLE), HOLD(20), PRESS(EM_PAD_START),
    CHECK(CHECK_CLOSED, "START closes the status screen"),
    /* The magazine 0x10: SPR4's take notice, START. */
    TAKE(0x0B08, "the take of 0x10 (0x0B08)"), WAIT(3, 2, -1, "the 0x10 take opens SPR4"),
    HOLD(60), PRESS(EM_PAD_START), CHECK(CHECK_CLOSED, "START closes the status screen"),
    /* The map 0x08: MAP zoomed on map 8 (0020F950 mode 0 with the request),
     * R1 (pad bit 8: zoom in), the D-pad right and up (pans), Circle to the
     * list, Cross (zoom again, from the list: the player's marker), Circle
     * twice to the hub, START. */
    TAKE(0x0B09, "the take of the map 0x08 (0x0B09)"), WAIT(3, 1, -1, "the 0x08 take opens MAP"),
    HOLD(20), {SP_PRESS, EM_PAD_R1, 0, 0, 8, 0, 0, 0, 0, NULL}, HOLD(10),
    {SP_PRESS, EM_PAD_RIGHT, 0, 0, 6, 0, 0, 0, 0, NULL}, {SP_PRESS, EM_PAD_UP, 0, 0, 6, 0, 0, 0, 0, NULL},
    HOLD(10), PRESS(EM_PAD_CIRCLE), HOLD(20), PRESS(EM_PAD_CROSS), HOLD(20),
    {SP_PRESS, EM_PAD_R2, 0, 0, 4, 0, 0, 0, 0, NULL}, HOLD(10), PRESS(EM_PAD_CIRCLE), HOLD(10),
    BACK(1, -1, -1, "Circle returns from MAP to the hub"), HOLD(10),
    /* MAP from the hub with map 8 owned: the list with its model, Circle. */
    STICK(0, -1), HOVER(3, "hub hover 3 (MAP, map 8 owned)"), PRESS(EM_PAD_CROSS), STICK(0, 0),
    WAIT(3, 1, -1, "Cross opens MAP"), HOLD(20), PRESS(EM_PAD_CROSS), HOLD(20),
    PRESS(EM_PAD_CIRCLE), HOLD(10), BACK(1, -1, -1, "Circle returns from MAP to the hub"),
    HOLD(10), PRESS(EM_PAD_START), CHECK(CHECK_CLOSED, "START closes the status screen"),
    {SP_END, 0, 0, 0, 0, 0, 0, 0, 0, NULL},
};

static struct {
    unsigned i;
    int frames, total, press;
    float lx, ly;
    uint16_t buttons;
    uint8_t fire_mode;
    unsigned seen[8]; /* page core states visited: MAP/SPR4/DATABASE, ITEM children */
    unsigned captured[2], page_key, page_frames;
} sp;

static const EmStatusPage *page_core(void)
{
    return em_status_runtime_page(em_area11_interaction_host_status());
}

static void status_pages_begin(void)
{
    memset(&sp, 0, sizeof sp);
    sp.fire_mode = em_weapon_fire_mode();
    nav_reset();
    pad_apply(0, 0, 0);
}

static void sp_next(void)
{
    ++sp.i;
    sp.frames = 0;
}

static int status_pages_frame(void)
{
    const PageStep *step = &k_page_steps[sp.i];
    const EmStatusPage *page = page_core();
    if (getenv("EM_LEVEL_SMOKE_PAGES_DEBUG") && page)
        fprintf(stderr, "pages: step %u op %d f%d t1=%u t2=%u t3=%u t4=%u t5=%u t10=%u t11=%u B0=%u\n",
                sp.i, step->op, sp.frames, page->phase, page->step, page->transition_step,
                page->item.state, page->item.step, page->item.screen, page->item.hover,
                em_scene_state()->req[EM_SCENE_REQ_B0]);
    if (++sp.total > 6000) {
        fail("status_pages: the script did not finish in 6000 frames");
        return 0;
    }
    if (page && page->phase == 3 && page->step == 2) {
        if (page->item.screen >= 1 && page->item.screen <= 3)
            sp.seen[page->item.screen] = 1;
        if (page->item.screen == 0 && page->item.state >= 4 && page->item.state <= 7)
            sp.seen[page->item.state] = 1;
        /* Verification aid: EM_LEVEL_SMOKE_PAGES_CAPTURE=<dir> writes one
         * frame of each page state 25 frames after the script reaches it
         * (<dir>/page_<t[0x10]>_<t[4]>.bmp; MAP: map_<t[3], + 4 once map 8 is
         * owned>.bmp), for a look. */
        const char *dir = getenv("EM_LEVEL_SMOKE_PAGES_CAPTURE");
        /* MAP (screen 1): its mode t[3], and whether map 8 is owned. */
        const unsigned key = page->item.screen == 1
                                 ? 16u + page->transition_step + (em_pickup_maps()[8] ? 4u : 0u)
                                 : page->item.screen * 16u + page->item.state;
        if (dir && *dir && key < 64 && !(sp.captured[key / 32] & (1u << (key % 32)))) {
            if (sp.page_key != key + 1) {
                sp.page_key = key + 1;
                sp.page_frames = 0;
            } else if (++sp.page_frames == 25) {
                char path[512];
                if (page->item.screen == 1) /* map_<t[3] + 4 once map 8 is owned> */
                    snprintf(path, sizeof path, "%s/map_%u.bmp", dir, key - 16u);
                else
                    snprintf(path, sizeof path, "%s/page_%u_%u.bmp", dir, (unsigned)page->item.screen,
                             (unsigned)page->item.state);
                em_gfx_request_capture(em_frame_gfx(), path);
                sp.captured[key / 32] |= 1u << (key % 32);
            }
        }
    }
    switch (step->op) {
    case SP_PRESS:
        if (sp.frames++ < step->frames) {
            pad_apply(step->buttons, sp.lx, sp.ly);
            return 0;
        }
        pad_apply(0, sp.lx, sp.ly);
        sp_next();
        return 0;
    case SP_STICK:
        sp.lx = step->lx;
        sp.ly = step->ly;
        pad_apply(0, sp.lx, sp.ly);
        sp_next();
        return 0;
    case SP_HOLD:
        pad_apply(0, sp.lx, sp.ly);
        if (++sp.frames >= step->frames)
            sp_next();
        return 0;
    case SP_HOVER:
        pad_apply(0, sp.lx, sp.ly);
        if (page && page->item.hover == step->value) {
            sp_next();
            return 0;
        }
        if (++sp.frames > 60)
            fail(step->what);
        return 0;
    case SP_WAIT: {
        pad_apply(0, sp.lx, sp.ly);
        const int ok = page && page->phase == step->phase &&
                       (step->phase == 1 ? page->step == 1
                                         : page->step == 2 &&
                                               (step->screen < 0 || page->item.screen == step->screen) &&
                                               (step->child < 0 || page->item.state == step->child));
        if (ok) {
            sp_next();
            return 0;
        }
        if (++sp.frames > 900)
            fail(step->what);
        return 0;
    }
    case SP_BACK: {
        const int ok = page && page->phase == step->phase &&
                       (step->phase == 1 ? page->step == 1
                                         : page->step == 2 &&
                                               (step->screen < 0 || page->item.screen == step->screen) &&
                                               (step->child < 0 || page->item.state == step->child) &&
                                               (step->screen != 2 || page->item.state == 1));
        if (ok && sp.frames % 20 >= 2) {
            pad_apply(0, sp.lx, sp.ly);
            sp_next();
            return 0;
        }
        pad_apply(sp.frames % 20 < 2 ? EM_PAD_CIRCLE : 0, sp.lx, sp.ly);
        if (++sp.frames > 100)
            fail(step->what);
        return 0;
    }
    case SP_HEALTH:
        g.status.health = (float)step->value;
        sp_next();
        return 0;
    case SP_TAKE: {
        /* The use scan's claim of the owner (em_interaction_runtime_claim
         * with the scan's 3B8D = 3 and the owner armed), as the host
         * fixture's other_take makes it. */
        if (sp.frames++ == 0) {
            EmInteractionSceneOwner *record =
                em_interaction_scene_pickup(em_area11_interaction_host_scene(), (uint16_t)step->value);
            EmPickupOwner *owner = record ? record->native_owner : NULL;
            if (!owner || !in_control() || !player_pose_use_accepted_port() ||
                !em_interaction_runtime_claim(em_area11_interaction_host_shared(), owner)) {
                fail(step->what);
                return 0;
            }
            em_area11_interaction_host_camera_fields();
            owner->armed = 4;
        }
        if (task_byte(EM_SCENE_TASK_0B) == 3) {
            sp_next();
            return 0;
        }
        if (sp.frames > 900)
            fail(step->what);
        return 0;
    }
    case SP_CHECK: {
        pad_apply(0, sp.lx, sp.ly);
        int ok = 0;
        if (step->value == CHECK_FIRE_MODE)
            ok = em_weapon_fire_mode() != sp.fire_mode;
        else if (step->value == CHECK_HEALTH_60)
            ok = g.status.health == 60.0f && page && page->item.step == 1;
        else if (step->value == CHECK_HEALTH_100)
            ok = g.status.health == 100.0f && page && page->item.step == 1;
        else if (step->value == CHECK_CLOSED)
            ok = in_control();
        if (ok) {
            sp_next();
            return 0;
        }
        if (++sp.frames > (step->value == CHECK_CLOSED ? 600 : 1))
            fail(step->what);
        return 0;
    }
    default:
        break;
    }
    pad_apply(0, 0, 0);
    if (!sp.seen[1] || !sp.seen[2] || !sp.seen[3] || !sp.seen[4] || !sp.seen[6] || !sp.seen[7]) {
        fail("status_pages: a page was not shown (MAP, SPR4, DATABASE, EQUIPMENT, EVENT, HEALING)");
        return 0;
    }
    fprintf(stderr, "level smoke: status_pages: PASS frames=%d pages=MAP,SPR4,DATABASE,EQUIPMENT,"
            "EVENT,HEALING takes=0x1E,0x1F,0x32,0x10,0x08 fire_mode=%u->%u health=%.1f\n", sp.total,
            (unsigned)sp.fire_mode, (unsigned)em_weapon_fire_mode(), (double)g.status.health);
    return 1;
}

static void battery_begin(void)
{
    nav_reset();
}

static int battery_frame(void)
{
    const EmStatusRuntime *status = em_area11_interaction_host_status();
    const EmStatusPage *page = status ? em_status_runtime_page(status) : NULL;
    const EmSceneState *s = em_scene_state();
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(218.212f, 222.373f, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(218.212f, 222.373f, 0.1f, 0.4f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(-0.9588f));
    case 4: NAV_STEP(nav_settle(10));
    case 5:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6:
        (void)scan_accepted();
        if (task_byte(EM_SCENE_TASK_0B) == 3) {
            if (em_pickup_item_count(0x1B) != 1 || s->req[EM_SCENE_REQ_B1] != 0x1B) {
                fail("the status screen opened without the item 0x1B take's request");
                return 0;
            }
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > 600)
            fail("the battery take did not open the status screen");
        return 0;
    case 7:
        if (page && page->phase == 3 && page->item.screen == 0 && page->item.state == 5 &&
            page->item.step == 3) {
            if (em_pickup_battery_charge() != 12 || em_pickup_battery_capacity() != 12 ||
                s->req[EM_SCENE_REQ_B0] != 0) {
                fail("the BATTERY acquisition notice did not set charge/capacity 12 or keep B0");
                return 0;
            }
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > 120)
            fail("the status screen did not show the BATTERY acquisition notice");
        return 0;
    case 8:
        if (page && page->item.state == 5 && page->item.step == 1) {
            fprintf(stderr, "level smoke: battery: pop-up notice %d frames, then the list\n",
                    t.nav_frames);
            /* Route 01_battery: the ITEM root is at state 5 step 3 from
             * f219 (its countdown 0xF0) to step 1 at f459; counted from
             * the frame after step 3 is seen, as here, that is 239
             * frames. The notice is the BATTERY page's own timer. */
            if (t.nav_frames != 239) {
                fail("the BATTERY acquisition notice did not last route 01's 239 frames");
                return 0;
            }
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > 400)
            fail("the BATTERY acquisition notice did not hand over to the list");
        return 0;
    case 9:
        if (++t.nav_frames < 20)
            return 0;
        ++t.step;
        nav_reset();
        return 0;
    case 10: NAV_STEP(nav_press(EM_PAD_TRIANGLE, 1));
    case 11:
        if (task_byte(EM_SCENE_TASK_0B) == 1 && s->req[EM_SCENE_REQ_B0] == 0 &&
            em_frame_transition()->substate == 0) {
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > STATUS_CLOSE_TIMEOUT)
            fail("TRIANGLE did not close the battery pop-up");
        return 0;
    case 12: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        /* 00219550's completion: the taken bit (001B1190) and the owner
         * freed with its light child. */
        if (!t.saw[7] || !em_pickup_taken(0x0B01) || em_pickup_item_count(0x1B) != 1) {
            fail("the battery owner did not complete its take (taken bit, count 1)");
            return 0;
        }
        fprintf(stderr, "level smoke: battery: PASS scan_d810750=%d player=(%.3f,%.3f,%.3f) yaw=%.5f "
                "charge=%d\n", (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw,
                em_pickup_battery_charge());
        return 1;
    }
    default:
        return 1;
    }
}

/* Route beats 02 and 04: the terminal 00827B10 at (224, 230, 250.7). Its
 * script aligns the player to (222, y, 250) facing -1.3037; approach along
 * -x so 00183EF0's facing gate passes (route_capture use_elevator_terminal). */
static int use_terminal(void)
{
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(229.0f, 250.4f, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(223.5f, 250.4f, 0.6f, 0.5f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(-1.3037f));
    case 4: NAV_STEP(nav_settle(10));
    case 5:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6:
        /* 00160220 -> 00184BA0: the terminal wins the scan (3B8D = 3). */
        if (scan_accepted()) {
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > 60)
            fail("Cross at the terminal did not win the use scan (3B8D stayed 0)");
        return 0;
    default:
        return 1;
    }
}

/* ---------------------------------------------------- elevator_refusal
 *
 * Route beat 02: with the power bit D_0081084C & 0x80 clear the terminal
 * 00827B10 runs the refusal 0x82A990: the scripted frame (3B8D 3 -> 2,
 * camera byte 1, the letterbox fading in), message 0x8000001A, then the
 * release (3B8D 0) with the player placed at (222, y, 250). In process:
 * the scan, the letterbox and the message were seen, the power bit stays
 * clear and control returns. The tick-by-tick comparison with the capture
 * is tools/test_level_smoke.py's. */
static void refusal_begin(void)
{
    nav_reset();
    if (power_bit())
        fail("the power bit is set before the refusal (route beat 02 has it clear)");
}

static int refusal_frame(void)
{
    if (t.step <= 6)
        return use_terminal(), 0;
    const EmMessageBlock *message = em_message_live_block();
    if (message && message->phase && message->line == 0x8000001Au)
        t.saw[0] = 1;
    /* Verification aid (tools/test_message_capture.py):
     * EM_LEVEL_SMOKE_MESSAGE_CAPTURE=<path.bmp> writes the frame whose step
     * F presents 0x8000001A for the fifth time (+0x68 = 5), the state of the
     * elevator/refusal capture; this hook runs before that frame's step F. */
    const char *capture = getenv("EM_LEVEL_SMOKE_MESSAGE_CAPTURE");
    if (capture && *capture && message && message->phase == 1 &&
        message->line == 0x8000001Au && message->frames == 4)
        em_gfx_request_capture(em_frame_gfx(), capture);
    if (em_frame_screen_fade()->state == 3 || em_frame_screen_fade()->state == 1)
        t.saw[1] = 1;
    if (g.cam.top_mode == 1)
        t.saw[2] = 1;
    if (t.step == 7) {
        if (power_bit()) {
            fail("the refusal set the power bit");
            return 0;
        }
        if (!in_control()) {
            if (++t.nav_frames > 1500)
                fail("the refusal script did not release the player");
            return 0;
        }
        if (!t.saw[0] || !t.saw[1] || !t.saw[2]) {
            fprintf(stderr, "level smoke: elevator_refusal: message 0x8000001A %s, letterbox %s, camera "
                    "byte 1 %s\n", t.saw[0] ? "seen" : "missing", t.saw[1] ? "seen" : "missing",
                    t.saw[2] ? "seen" : "missing");
            fail("the refusal did not run the original script's message, letterbox and camera");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    }
    if (t.step == 8) {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        fprintf(stderr, "level smoke: elevator_refusal: PASS scan_d810750=%d player=(%.3f,%.3f,%.3f) "
                "yaw=%.5f power=%d\n", (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw,
                power_bit());
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------- panel
 *
 * Route beat 03: Cross at the panel 00159210 with item 0x1B starts
 * 0x2477A0 (message 0x80000018); its 00157F60 posts B0 = 1 / B1 = 0x82 and
 * the status screen opens on the BATTERY page's "consume 2 units" prompt
 * (default No); LEFT then Cross select Yes; the discharge runs 12 -> 10 ->
 * 8 half-units; 0x247BE0 plays clip 0x15C and 001580C0 sets the power bit
 * 0x80 before control returns. */
static void panel_begin(void)
{
    nav_reset();
    if (em_pickup_item_count(0x1B) != 1 || em_pickup_battery_charge() != 12)
        fail("the panel phase needs item 0x1B with charge 12 (route beat 03 starts so)");
}

static int prompt_open(void)
{
    const EmStatusRuntime *status = em_area11_interaction_host_status();
    const EmStatusPage *page = status ? em_status_runtime_page(status) : NULL;
    return task_byte(EM_SCENE_TASK_0B) == 3 && page && page->phase == 3 && page->step == 2 &&
           page->item.step == 4;
}

static int panel_frame(void)
{
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(239.7f, 216.0f, 1.0f, 1.0f, 1));
    /* route_capture aims at (239.7, 222.0); the original's walk carried the
     * player on to (241.4, 225.3) before the press (beat 03 f228). The
     * port's walk stops shorter, so the runner aims at that press point:
     * 00183EF0's panel radius is 9.5 around (240, 232.8). */
    case 1: NAV_STEP(nav_goto(240.5f, 225.0f, 0.8f, 0.5f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(0.0f));
    case 4: NAV_STEP(nav_settle(10));
    case 5:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6:
        if (scan_accepted()) {
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 60) {
            fail("Cross at the panel did not win the use scan (3B8D stayed 0)");
        }
        return 0;
    case 7:
        /* 00157F60's request and the BATTERY prompt on its status page. */
        if (em_scene_state()->req[EM_SCENE_REQ_B0] == 1 && em_scene_state()->req[EM_SCENE_REQ_B1] == 0x82)
            t.saw[0] = 1;
        if (prompt_open()) {
            if (!t.saw[0]) {
                fail("the BATTERY page opened without the 00157F60 request B0 = 1 / B1 = 0x82");
                return 0;
            }
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 900) {
            fail("the BATTERY prompt did not open");
        }
        return 0;
    case 8:
        /* route_capture: idle 30, LEFT 2 (+10), Cross 2. */
        if (++t.nav_frames > 30) {
            ++t.step;
            nav_reset();
        }
        pad_apply(0, 0, 0);
        return 0;
    case 9: NAV_STEP(nav_press(EM_PAD_LEFT, 2));
    case 10:
        pad_apply(0, 0, 0);
        if (++t.nav_frames > 10) {
            ++t.step;
            nav_reset();
        }
        return 0;
    case 11: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 12:
        if (em_pickup_battery_charge() == 8)
            t.saw[1] = 1;
        if (power_bit()) {
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 900) {
            fail("the power bit 0x80 was not set");
        }
        return 0;
    case 13:
        if (in_control()) {
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 900) {
            fail("the panel script did not release the player");
        }
        return 0;
    case 14: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        if (!t.saw[1] || em_pickup_battery_charge() != 8 || em_pickup_item_count(0x1B) != 1) {
            fail("the discharge did not leave item 0x1B with charge 8 (route beat 03)");
            return 0;
        }
        fprintf(stderr, "level smoke: panel: PASS scan_d810750=%d power=%d charge=%d player=(%.3f,%.3f,%.3f)"
                " yaw=%.5f\n", (int)t.scan_variants, power_bit(), em_pickup_battery_charge(), g.pos[0],
                g.pos[1], g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ elevator
 *
 * Route beat 04: with the power bit set the terminal runs 0x82A750: the
 * scripted frame, clip 0x47, the 150-call carry 00828050, and the release
 * at (222, 190, 250) with the elevator's floor byte D_0081083A toggled to
 * the lower floor. */
static void elevator_begin(void)
{
    nav_reset();
    if (!power_bit())
        fail("the elevator phase needs the power bit (route beat 04 starts powered)");
}

static int elevator_frame(void)
{
    if (t.step <= 6)
        return use_terminal(), 0;
    if (t.step == 7) {
        if (!in_control()) {
            if (++t.nav_frames > 1500)
                fail("the powered terminal script did not release the player");
            return 0;
        }
        const uint8_t *floor = em_scene_progress_at(em_scene_state(), 0x0081083Au, 1);
        if (!floor || *floor != 1 || fabsf(g.pos[1] - 190.0f) > 0.01f) {
            fprintf(stderr, "level smoke: elevator: floor byte %d, player y %.5f\n", floor ? *floor : -1,
                    g.pos[1]);
            fail("the ride did not carry the player down to the lower floor (y 190, D_0081083A = 1)");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    }
    int r = nav_settle(30);
    if (r <= 0)
        return 0;
    fprintf(stderr, "level smoke: elevator: PASS scan_d810750=%d player=(%.3f,%.5f,%.3f) yaw=%.5f\n",
            (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw);
    return 1;
}

/* --------------------------------------------------------------- boxes
 *
 * Route beat 05: from the elevator's release the player walks to crate r4
 * (001551B0, 0x7A7C70, top y 203.8) and stands before it at the route's
 * stance (228.787, 281.266, facing 0.0265; route f160..f172). Cross: the Use
 * dispatcher 00160220 runs its ledge probes (0015DF10) and enters the ledge
 * climb (+5 = 2, +1F0 = 8; route f175), clips 0x70, 0x78 and 0x8C, and the
 * player stands on r4 (y 203.776, ground 0x7A7C70; f253/f254). Then west on
 * the crate top to (226.237, 288.309), facing -x (-1.5708; f380..f390), Cross
 * again: the climb onto the raised crate r3 (0x7A7980, y 217.786; f393 ..
 * f472), and north onto the upper ledge (f640: y 219.26 on the grid). In
 * process: both climbs are entered and end on their crates; the tick-by-tick
 * comparison of the climbs with the capture is tools/test_level_smoke.py's
 * check_boxes. */
static void boxes_begin(void)
{
    nav_reset();
}

/* The climb after a Cross: 1 once the stage has entered +5 = 2 and handed
 * back to control with the idle clip, 0 continue, -1 failed. */
static int boxes_climb(int index)
{
    uint8_t state = em_live_u8(player_states_actor(), 5);
    if (state == 2)
        t.saw[index] = 1;
    /* After the hand-back the pad stays neutral through the idle return
     * (route f254..f265: the clip 0 countdown from 12), as in the route. */
    if (t.saw[index] && state != 2 && in_control() && idle_clip() && ++t.saw[index + 2] > 12) {
        nav_reset();
        return 1;
    }
    if (++t.nav_frames > 200) {
        fail(t.saw[index] ? "the ledge climb did not hand back to control"
                          : "Cross did not enter the ledge climb (+5 = 2)");
        return -1;
    }
    return 0;
}

static int boxes_frame(void)
{
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(228.787f, 275.0f, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(228.787f, 281.266f, 0.1f, 0.4f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(0.0265f));
    case 4: NAV_STEP(nav_settle(30));
    case 5: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6: NAV_STEP(boxes_climb(0));
    case 7:
        if (fabsf(g.pos[1] - 203.776f) > 0.001f) {
            fprintf(stderr, "level smoke: boxes: after the first climb y %.5f\n", g.pos[1]);
            fail("the first climb did not end on crate r4 (y 203.776, route f253)");
            return 0;
        }
        ++t.step;
        return 0;
    case 8: NAV_STEP(nav_goto(226.237f, 288.309f, 0.1f, 0.4f, 1));
    case 9: NAV_STEP(nav_settle(20));
    case 10: NAV_STEP(nav_face(-1.5708f));
    case 11: NAV_STEP(nav_settle(30));
    case 12: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 13: NAV_STEP(boxes_climb(1));
    case 14:
        if (fabsf(g.pos[1] - 217.78619f) > 0.001f) {
            fprintf(stderr, "level smoke: boxes: after the second climb y %.5f\n", g.pos[1]);
            fail("the second climb did not end on crate r3 (y 217.786, route f471)");
            return 0;
        }
        ++t.step;
        return 0;
    case 15: NAV_STEP(nav_goto(219.6f, 305.2f, 0.3f, 1.0f, 1));
    case 16: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        if (g.pos[1] < 219.0f) {
            fprintf(stderr, "level smoke: boxes: on the ledge y %.5f\n", g.pos[1]);
            fail("the player did not step onto the upper ledge (route f640: y 219.26)");
            return 0;
        }
        fprintf(stderr, "level smoke: boxes: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0], g.pos[1],
                g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* --------------------------------------------------------------- slide
 *
 * Route beat 06 (route_capture.py beat_hill_slide): from the upper ledge
 * the stick points at (240, 312) at full deflection until within 1.5, then
 * at (262, 356) without a stop. The floor service's apply 00175CF0 meets
 * the hill's authored class-0x1000 grid nodes and 001796C0 enters the
 * slope slide (+5 = 0x1C, +1F0 = 0x30; route f72 at (251.1, 207.1,
 * 328.1)); 0016C6A0 runs it with clips 0x5E / 0x61 while the stick stays
 * on (262, 356). When +1F0 leaves 0x30 (route f138, on the low ground at
 * y 185.0) the stick is released; the skid-out (clips 0x60 / 0x65) hands
 * back to state 0 (f181) and the player idles at (265.7, 185.3, 373.7).
 * In process: the slide is entered, its action ends on the low ground and
 * control returns there; tools/test_level_smoke.py check_slide compares
 * the slide with the capture row for row. */
enum { SLIDE_ENTRY_LIMIT = 300, SLIDE_LIMIT = 400 };

static void slide_begin(void)
{
    nav_reset();
}

static int slide_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t state = em_live_u8(a, 5), mode = em_live_u8(a, 0x1F0);
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(219.594f, 305.214f, 0.1f, 0.4f, 1));
    case 1: NAV_STEP(nav_settle(20));
    case 2: NAV_STEP(nav_face(-0.04141f));
    case 3: NAV_STEP(nav_settle(30));
    case 4:
        /* route_capture goto(240, 312, tol 1.5, stop=False). */
        if (nav_stick_toward(240.0f, 312.0f, 1.0f) > 1.5f) {
            if (++t.nav_frames > NAV_LIMIT)
                fail("navigation did not reach the top of the hill");
            return 0;
        }
        nav_reset();
        ++t.step;
        /* fall through: the stick turns to (262, 356) on this frame */
    case 5:
        nav_stick_toward(262.0f, 356.0f, 1.0f);
        if (state == 0x1C && mode == 0x30) {
            t.saw[0] = 1;
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > SLIDE_ENTRY_LIMIT)
            fail("walking down the hill did not enter the slope slide (+5 = 0x1C, +1F0 = 0x30)");
        return 0;
    case 6:
        if (mode == 0x30) {
            nav_stick_toward(262.0f, 356.0f, 1.0f);
            if (++t.nav_frames > SLIDE_LIMIT)
                fail("the slide action did not end");
            return 0;
        }
        pad_apply(0, 0, 0);
        if (state != 0x1C || g.pos[1] > 186.0f) {
            fprintf(stderr, "level smoke: slide: action ended at +5 %u y %.5f\n", state, g.pos[1]);
            fail("the slide action did not end in state 0x1C on the low ground (route f138: y 185.0)");
            return 0;
        }
        nav_reset();
        ++t.step;
        return 0;
    case 7: {
        /* Neutral through the skid-out, the hand-back (route f181) and past
         * the idle return the capture check compares (12 rows). */
        int r = nav_settle(60);
        if (r <= 0)
            return 0;
        if (g.pos[1] > 186.0f) {
            fprintf(stderr, "level smoke: slide: settled at y %.5f\n", g.pos[1]);
            fail("the player did not settle on the low ground (route f181: y 185.28)");
            return 0;
        }
        fprintf(stderr, "level smoke: slide: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0], g.pos[1],
                g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------- truck_preview
 *
 * Route beat 07: route_capture.py's beat_truck_preview. From the slide's
 * end the stick walks the waypoints (280, 392), (300, 400), (328, 412) at
 * full deflection (walk_path: each until within 2.0, or blocked) and is
 * released once the trigger 008251E0 (state 4) has started 0x8292C0: the
 * first frame with 3B8D != 0 (the trigger's +0x04 = 1 is one frame
 * earlier). The script runs 364 frames: 3B8D = 2 with camera byte 1 and the
 * letterbox, the player placed at (327.4, y, 396.7) and turned to 1.97222,
 * the camera shots, then 07/4; the trigger stores D_00810792 = 1 and frees
 * itself. In process: the script's frame, bars and camera byte were seen,
 * the placement and heading are the record's, D_00810792 = 1 and the
 * trigger node is gone. tools/test_level_smoke.py check_truck_preview
 * compares the script window with the capture row for row. */
enum { TRUCK_PREVIEW_LIMIT = 900 };
static const float k_truck_path[3][2] = {{280.0f, 392.0f}, {300.0f, 400.0f}, {328.0f, 412.0f}};

static uint8_t story_792(void)
{
    const uint8_t *b = em_scene_progress_at(em_scene_state(), 0x00810792u, 1);
    return b ? *b : 0xEE;
}

static void truck_preview_begin(void)
{
    nav_reset();
    if (story_792() != 0 || em_scene_bindings_pool_count(0x008251E0u) != 1)
        fail("the truck preview needs D_00810792 = 0 and the trigger node (route beat 07 starts so)");
}

/* walk_path: one waypoint at a time until within 2.0, or blocked (45 frames
 * moving less than 0.3). 1 when the path ends, 0 continue. */
static int truck_walk(void)
{
    if (t.step >= 3)
        return 1;
    const float *wp = k_truck_path[t.step];
    int slot = t.nav_hist_n % 64;
    t.nav_hist[slot][0] = g.pos[0];
    t.nav_hist[slot][1] = g.pos[2];
    ++t.nav_hist_n;
    int blocked = 0;
    if (t.nav_hist_n > 45) {
        int old = (t.nav_hist_n - 1 - 45) % 64;
        blocked = hypotf(g.pos[0] - t.nav_hist[old][0], g.pos[2] - t.nav_hist[old][1]) < 0.3f;
    }
    if (nav_stick_toward(wp[0], wp[1], 1.0f) <= 2.0f || blocked) {
        nav_reset();
        ++t.step;
    }
    return 0;
}

static int truck_preview_frame(void)
{
    const EmSceneState *s = em_scene_state();
    if (s->spad3B8D == 2)
        t.saw[0] = 1;
    if (em_frame_screen_fade()->state == 3 || em_frame_screen_fade()->state == 1)
        t.saw[1] = 1;
    if (g.cam.top_mode == 1)
        t.saw[2] = 1;
    if (t.step < 4) {
        if (s->spad3B8D != 0) {
            pad_apply(0, 0, 0);
            nav_reset();
            t.step = 4;
            return 0;
        }
        if (++t.nav_frames > TRUCK_PREVIEW_LIMIT) {
            fail("the walk did not reach the truck trigger's band (3B8D stayed 0)");
            return 0;
        }
        int n = t.nav_frames;
        (void)truck_walk();
        t.nav_frames = n;
        if (t.step >= 3)
            fail("the walk ended without the trigger starting its script");
        return 0;
    }
    if (t.step == 4) {
        pad_apply(0, 0, 0);
        if (story_792() != 1 || !in_control()) {
            if (++t.nav_frames > TRUCK_PREVIEW_LIMIT)
                fail("the truck preview script did not end with D_00810792 = 1 and control");
            return 0;
        }
        if (!t.saw[0] || !t.saw[1] || !t.saw[2]) {
            fail("the truck preview did not open the scripted frame (3B8D 2) with the letterbox and camera "
                 "byte 1");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    }
    int r = nav_settle(30);
    if (r <= 0)
        return 0;
    /* 0x8292C0's 01/1 placement and 04/8 heading (FIRST_LEVEL_ROUTE.md 07). */
    if (fabsf(g.pos[0] - 327.4f) > 1e-3f || fabsf(g.pos[2] - 396.7f) > 1e-3f || fabsf(g.yaw - 1.97222f) > 1e-4f ||
        em_scene_bindings_pool_count(0x008251E0u) != 0) {
        fprintf(stderr, "level smoke: truck_preview: player (%.4f, %.4f) yaw %.5f trigger nodes %d\n", g.pos[0],
                g.pos[2], g.yaw, em_scene_bindings_pool_count(0x008251E0u));
        fail("the preview did not leave the player at the script's placement, or the trigger did not free itself");
        return 0;
    }
    fprintf(stderr, "level smoke: truck_preview: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f story792=%u\n", g.pos[0],
            g.pos[1], g.pos[2], g.yaw, story_792());
    return 1;
}

/* ------------------------------------------------------ truck_crossing
 *
 * Route beat 08: route_capture.py's beat_truck_crossing. The stick walks
 * (345, 390) then (365, 368) (walk_path, tolerance 2.0, a blocked waypoint
 * is skipped after 45 frames), is released, and the run waits for
 * D_00810792 = 0xFF and settles. Standing on the truck (the player's
 * +0x214 = the truck record, its +0x0D = 9, player +0x0A != 0) arms it
 * (00823FF0 state 4): 001B1E20(0, 0), the 47-tick shake, then the 119-beat
 * fall (state 1) and state 2 with D_00810792 = 0xFF. In process: the player
 * stood on the truck record, the truck armed, fell and stored 0xFF, the pad
 * block's rumble ran, and the player is back on the low ground north of the
 * pit (route f172: y 184.84). tools/test_level_smoke.py check_truck_crossing
 * compares the truck record with the capture row for row from the arm. */
enum { TRUCK_CROSSING_LIMIT = 900 };
static const float k_crossing_path[2][2] = {{345.0f, 390.0f}, {365.0f, 368.0f}};

static void truck_crossing_begin(void)
{
    nav_reset();
    if (story_792() != 1 || em_scene_bindings_pool_count(0x00823FF0u) != 1)
        fail("the truck crossing needs D_00810792 = 1 and the truck node (route beat 08 starts so)");
}

static int truck_crossing_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint32_t record;
    uint8_t head[16], t2dc[20];
    float truck_pos[3];
    int truck = em_area11_boxes_truck_state(&record, head, truck_pos, t2dc);
    if (truck && a->link_owner && em_scene_bindings_pool_address(a->link_owner) == record)
        t.saw[0] = 1;                                    /* stood on the truck */
    if (truck && t2dc[16] != 0)
        t.saw[1] = 1;                                    /* +0x2EC: armed */
    if (truck && head[4] == 1)
        t.saw[2] = 1;                                    /* state 1: falling */
    if (em_pad_actuator_block()[0x16])
        t.saw[3] = 1;                                    /* 001B61C0 ran */
    if (t.step < 2) {
        const float *wp = k_crossing_path[t.step];
        int slot = t.nav_hist_n % 64;
        t.nav_hist[slot][0] = g.pos[0];
        t.nav_hist[slot][1] = g.pos[2];
        ++t.nav_hist_n;
        int blocked = 0;
        if (t.nav_hist_n > 45) {
            int old = (t.nav_hist_n - 1 - 45) % 64;
            blocked = hypotf(g.pos[0] - t.nav_hist[old][0], g.pos[2] - t.nav_hist[old][1]) < 0.3f;
        }
        if (nav_stick_toward(wp[0], wp[1], 1.0f) <= 2.0f || blocked) {
            t.nav_hist_n = 0;
            ++t.step;
        }
        if (++t.nav_frames > TRUCK_CROSSING_LIMIT)
            fail("the walk across the truck did not end");
        return 0;
    }
    if (t.step == 2) {
        pad_apply(0, 0, 0);
        if (story_792() != 0xFF) {
            if (++t.nav_frames > TRUCK_CROSSING_LIMIT)
                fail("the truck did not fall (D_00810792 never 0xFF)");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    }
    int r = nav_settle(30);
    if (r <= 0)
        return 0;
    /* TRUCK_ORIGINAL.md: a whole set piece spawns 32 effects (12 in the
     * shake, 20 in the fall), counted at the gap (census L26). */
    if (em_area11_boxes_effect_spawns() != 32) {
        fprintf(stderr, "level smoke: truck_crossing: %u effect spawns\n", em_area11_boxes_effect_spawns());
        fail("the truck did not spawn the set piece's 32 effects");
        return 0;
    }
    if (!t.saw[0] || !t.saw[1] || !t.saw[2] || !t.saw[3] || !truck || head[4] != 2) {
        fprintf(stderr, "level smoke: truck_crossing: stood %u armed %u fell %u rumble %u state %d\n", t.saw[0],
                t.saw[1], t.saw[2], t.saw[3], truck ? head[4] : -1);
        fail("the player did not arm the truck from its top, or the truck did not shake, fall and rest in state 2");
        return 0;
    }
    /* Step I's countdown 001B5B70 stopped the rumbles through 001B6250: the
     * pad block's active byte +0x16 and duration +0x28 are 0 again, as in
     * route 08's end snapshot (eeMemory at f239). */
    const uint8_t *pad = em_pad_actuator_block();
    if (pad[0x16] != 0 || pad[0x28] != 0 || pad[0x29] != 0) {
        fail("the truck's rumble was not stopped by the step-I countdown (pad block +0x16 / +0x28)");
        return 0;
    }
    if (g.pos[1] > 186.0f || g.pos[1] < 184.0f || g.pos[2] > 385.0f) {
        fprintf(stderr, "level smoke: truck_crossing: player (%.3f, %.5f, %.3f)\n", g.pos[0], g.pos[1], g.pos[2]);
        fail("the player is not on the low ground north of the pit (route f172: y 184.84)");
        return 0;
    }
    fprintf(stderr, "level smoke: truck_crossing: PASS player=(%.3f,%.5f,%.3f) truck_y=%.5f story792=%u\n",
            g.pos[0], g.pos[1], g.pos[2], truck_pos[1], story_792());
    return 1;
}

/* ---------------------------------------------------------- fence_door
 *
 * Route beat 09 (a side beat from the truck crossing's end, route_capture.py
 * beat_fence_door): the stick walks to the fence door along the route's
 * path (tolerance 1.5; the fence stops the walk at (414.9, 292.8), f135),
 * then to the route's press stance (417.786, 293.837; f255) at 0.4 stick
 * (navigation input, within 0.1, as the other presses), faces its heading
 * 2.4073 and presses Cross (f306). 00184BA0 selects the door (00183EF0's
 * class-5 branch) and arms +0x0B bit 2; the door's 001BBE40 runs in the same
 * frame (f309: +5 = 3, the player aligned and facing the door, the program
 * 0x24DE40 started): the frame, the camera retarget (op0D sub 5), clip 0x45
 * (f313), the door clip 2 with cue 0x401 and the 90-tick wait; its end
 * (f406) takes the door to +5 = 4: 001BC150 (fade, B8 = 2, B7 = 2; f407),
 * then +5 = 5 until 0x1AE040 state 4 re-places the player at entry 2
 * (f472) and 001BC290 closes the door. In process: the scan, the door's
 * phases 3, 4 and 5, the room move to D_00810702 = 2 and control back.
 * tools/test_level_smoke.py check_fence_door compares the capture row for
 * row. */
enum { FENCE_DOOR_LIMIT = 900 };
static const float k_fence_path[4][2] = {{386.0f, 348.0f}, {395.0f, 340.0f}, {402.0f, 317.0f}, {413.0f, 296.0f}};

static void fence_door_begin(void)
{
    nav_reset();
    if (em_scene_state()->d810700 != 0x0B || em_scene_state()->d810702 != 0)
        fail("the fence door starts in AREA11 room 0 (route beat 09 from 08)");
}

static int fence_door_frame(void)
{
    uint8_t head[16], block[16];
    int door = em_area11_door_state(head, block);
    if (door && head[5] == 3) t.saw[0] = 1;
    if (door && head[5] == 4) t.saw[1] = 1;
    if (door && head[5] == 5) t.saw[2] = 1;
    if (em_scene_state()->req[EM_SCENE_REQ_B8] == 2) t.saw[3] = 1;
    switch (t.step) {
    case 0: NAV_STEP(walk_path(k_fence_path, 4, 1.5f));
    case 1: NAV_STEP(nav_settle(5));
    case 2: NAV_STEP(nav_goto(417.786f, 293.837f, 0.1f, 0.4f, 1));
    case 3: NAV_STEP(nav_settle(20));
    case 4: NAV_STEP(nav_face(2.4073f));
    case 5: NAV_STEP(nav_settle(30));
    case 6:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 7:
        (void)scan_accepted();
        pad_apply(0, 0, 0);
        if (!(door && t.saw[2] && head[5] == 0 && em_scene_state()->d810702 == 2 && in_control())) {
            if (++t.nav_frames > FENCE_DOOR_LIMIT)
                fail(t.saw[0] ? "the door's room move did not end with the player re-placed at entry 2"
                              : "Cross at the fence door did not start the door (+5 = 3)");
            return 0;
        }
        nav_reset();
        ++t.step;
        return 0;
    default: {
        /* 70 frames: the capture ends 60 rows after the re-place (f532), and
         * the follow camera is compared to its end. */
        int r = nav_settle(70);
        if (r <= 0)
            return 0;
        if (!t.saw[7] || !t.saw[0] || !t.saw[1] || !t.saw[2] || !t.saw[3] || head[0x0B] != 0) {
            fprintf(stderr, "level smoke: fence_door: scan %u phases 3/4/5 %u/%u/%u B8=2 %u armed %02X\n", t.saw[7],
                    t.saw[0], t.saw[1], t.saw[2], t.saw[3], head[0x0B]);
            fail("the door did not run its phases 3, 4, 5 and the room move from the Use scan");
            return 0;
        }
        fprintf(stderr, "level smoke: fence_door: PASS scan_d810750=%d player=(%.3f,%.5f,%.3f) yaw=%.5f room=%u\n",
                (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw, em_scene_state()->d810702);
        return 1;
    }
    }
}

/* ----------------------------------------------------- aim_r1/r2_hold
 *
 * The AIM captures aim_00_r1_hold / aim_01_r2_hold (decomp
 * docs/CAPTURES_C10.md "AIM"; build/aimfire/capture/<beat>/trace.json)
 * start from route 08's end snapshot: the player idle at (371.50317,
 * 184.84026, 361.34225), heading 2.38104. The side run walks there from
 * the truck crossing's end (navigation input, as fence_door's): to a point
 * behind it, then straight in along the heading, so it stands within about
 * 0.8 of it with a heading within 0.015 (the stick's resolution); it
 * settles, and waits for the idle clip's +3C to count down to
 * 13.0: the capture's row before the stance (f12; the stance row f13 has
 * 12.0), so the pose the draw starts from is the same idle frame. Then it
 * holds R1 (R2) for 79 ticks from the stance, as the capture (press f10,
 * release f89; the stance f13, +6 = 0x63 at f92), and runs to the
 * capture's last row (aim_00 f146, aim_01 f154). In process: the
 * stance and its action code at the first held tick, +6 = 2 sixteen ticks
 * later, 0x63 on the release tick, idle at the end, no fault.
 * tools/test_level_smoke.py check_aim_hold compares every row from the
 * stance on (LEVEL_SMOKE.md "aim_r1_hold, aim_r2_hold"). The original
 * aim / fire path is the only one (AIM_FIRE.md section 10). */
/* The rows after the stance: aim_00 ends at f146, aim_01 at f154 (its
 * ramp-out is eight frames longer). */
enum { AIM_HOLD_TICKS = 79, AIM_R1_TAIL = 133, AIM_R2_TAIL = 141, AIM_CLOCK_LIMIT = 400 };
/* Test navigation (not game behaviour): the run-up, the walk-in's stick
 * magnitude (0.3 does not walk; 0.6 walks 0.1 per tick) and where the stick
 * is released (the walk-stop, +1F0 5, slides about 1.76 further). */
#define AIM_RUNUP 6.0f
#define AIM_WALK 0.6f
#define AIM_STOP 1.80f

/* The idle clip's +3C aim_hold_frame aligns on (13.0: aim_00 / aim_01's
 * f12; the AIM replays set their capture's own). */
static uint32_t s_aim_align_clock = 0x41500000u;

static void aim_hold_begin(void)
{
    nav_reset();
    if (em_scene_state()->d810700 != 0x0B || em_scene_state()->d810702 != 0)
        fail("the aim side run starts in AREA11 room 0 (route 08's end)");
}

static int aim_hold_frame(uint16_t button, unsigned state, unsigned action, int tail)
{
    const EmPlayerLiveActor *a = player_states_actor();
    /* The start pose: a point AIM_RUNUP behind it along the heading, then a
     * straight walk-in along the heading (the stick held on a far point of
     * that line) released AIM_STOP short of it (the run-stop's slide), so
     * the heading and the place end near the capture's. */
    const float hx = sinf(2.38104f), hz = cosf(2.38104f);
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(371.50317f - AIM_RUNUP * hx, 361.34225f - AIM_RUNUP * hz, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(371.50317f - AIM_RUNUP * hx, 361.34225f - AIM_RUNUP * hz, 0.1f, 0.3f, 1));
    case 2: NAV_STEP(nav_settle(10));
    case 3: {
        float along = (g.pos[0] - 371.50317f) * hx + (g.pos[2] - 361.34225f) * hz;
        if (along >= -AIM_STOP) {
            pad_apply(0, 0, 0);
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > NAV_LIMIT) {
            fail("the walk-in to the aim captures' start did not arrive");
            return 0;
        }
        nav_stick_toward(371.50317f + 100.0f * hx, 361.34225f + 100.0f * hz, AIM_WALK);
        return 0;
    }
    case 4: NAV_STEP(nav_settle(120));
    case 5: {
        /* The idle clip's +3C at the capture's row f12. */
        pad_apply(0, 0, 0);
        uint32_t clock = em_live_u32(a, 0x3C);
        if (idle_clip() && in_control() && clock == s_aim_align_clock) {
            pad_apply(button, 0, 0);
            t.frames = 0;
            ++t.step;
        } else if (++t.nav_frames > AIM_CLOCK_LIMIT) {
            fail("the idle clip's +3C did not reach 13.0 (the capture's row before the stance)");
        }
        return 0;
    }
    case 6:
        ++t.frames;
        if (t.frames == 1 && (em_live_u8(a, 5) != state || em_live_u8(a, 0x1F0) != action)) {
            fail("the trigger did not enter the stance on the next tick (the capture's f13)");
            return 0;
        }
        if (t.frames == 17 && em_live_u8(a, 6) != 2) {
            fail("the stance did not reach +6 = 2 sixteen ticks after it started (the capture's f29)");
            return 0;
        }
        if (t.frames < AIM_HOLD_TICKS) {
            pad_apply(button, 0, 0);
            return 0;
        }
        pad_apply(0, 0, 0);
        ++t.step;
        return 0;
    default:
        pad_apply(0, 0, 0);
        ++t.frames;
        if (t.frames == AIM_HOLD_TICKS + 1 && (em_live_u8(a, 5) != state || em_live_u8(a, 6) != 0x63)) {
            fail("the release did not enter +6 = 0x63 on the next tick (the capture's f92)");
            return 0;
        }
        if (t.frames <= tail)
            return 0;
        if (em_live_u8(a, 5) != 0) {
            fail("the player is not back in idle at the capture's last row");
            return 0;
        }
        fprintf(stderr, "level smoke: %s: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", k_phases[t.current].name,
                g.pos[0], g.pos[1], g.pos[2], g.yaw);
        return 1;
    }
}

static int aim_r1_hold_frame(void) { return aim_hold_frame(EM_PAD_R1, 0x1D, 0x31, AIM_R1_TAIL); }
static int aim_r2_hold_frame(void) { return aim_hold_frame(EM_PAD_R2, 0x1E, 0x32, AIM_R2_TAIL); }

/* ---------------------------------------------------- the AIM replays
 *
 * The AIM captures (decomp docs/CAPTURES_C10.md "AIM") that start from
 * route 08's end, replayed from the same place: the walk-in of
 * aim_hold_frame and its alignment on the idle clip's +3C at the capture's
 * row first + 2 (each beat's own value), then the capture's own pad script
 * from its frame `first` on (10, or 5 for the stick replays). A capture pad
 * state set before frame f shows in the player at f + 3, the port's at the
 * next tick: the port's tick k after the alignment takes the capture's
 * input of frame k + first and is compared with its row k + first + 2, to
 * the capture's last row. In process: no fault, the capture's +5 / +1F0 at
 * k = 1 (the stance, the melee state, or idle for a stick replay), the
 * player idle at the end.
 * tools/test_level_smoke.py check_aim_replay compares the rows
 * (LEVEL_SMOKE.md "The AIM replays"). The pad scripts are the captures'
 * (test input, not game data). The stick replays (aim_world, aim_cable)
 * start at the capture's first stick input, frame 5 (`first`), and are
 * compared from its row 8; the other replays' first input is frame 10.
 * aim_burst (aim_05_burst_fire) takes its pad script from the same file
 * (START, the stick and the menu buttons): its status screen runs
 * iterations that close out neither a world nor a status frame, so the
 * phase asks for every iteration (Phase.every_tick) and its script stays
 * on the capture's frames. */
typedef struct {
    int f;
    uint16_t buttons;
} AimInput;
typedef struct {
    const char *phase;
    const AimInput *inputs;      /* NULL: the stick script (aim_script_begin) */
    unsigned count;
    uint32_t align_clock;        /* the idle clip's +3C at the capture's row first + 2 */
    uint8_t state, action;       /* +5 / +1F0 at the capture's row first + 3 */
    int last;                    /* the capture's last row */
    int first;                   /* the capture's first input frame */
} AimReplay;
/* The replays with the left stick (aim_world: aim_04; aim_cable: aim_10
 * and then aim_11 from aim_10's end, its frame f as frame 1448 + f: the
 * seven frames between the two recordings, their frame counters 9386 to
 * 9393, are idle: the idle clip's +3C runs 43.0 to 36.0) take the captures' pad
 * scripts with both stick bytes from the file EM_AIM_PAD_SCRIPT, which
 * tools/test_level_smoke_aim.py writes from the captures' trace.json
 * (lines "frame buttons lx ly"): test input, not game data. */
typedef struct {
    int f;
    uint16_t buttons;
    uint8_t lx, ly;
} AimStick;
enum { AIM_SCRIPT_MAX = 4096 };
static AimStick s_aim_script[AIM_SCRIPT_MAX];
static unsigned s_aim_script_n;
static const AimInput k_aim_fire_inputs[] = { /* aim_03_single_fire */
    {10, 0x0800}, {39, 0x2800}, {41, 0x0800}, {55, 0x2800}, {57, 0x0800}, {71, 0x2800}, {73, 0x0800},
    {87, 0x2800}, {177, 0x0800}, {207, 0x0000}, {244, 0x0200}, {273, 0x2200}, {275, 0x0200}, {289, 0x2200},
    {291, 0x0200}, {305, 0x2200}, {365, 0x0200}, {395, 0x0000}};
static const AimInput k_aim_reload_inputs[] = { /* aim_06_reload_partial */
    {10, 0x0800}, {39, 0x2800}, {41, 0x0800}, {55, 0x2800}, {57, 0x0800}, {71, 0x2800}, {73, 0x0800},
    {87, 0x2800}, {89, 0x0800}, {103, 0x2800}, {105, 0x0800}, {119, 0x0802}, {121, 0x0800}, {221, 0x0802},
    {223, 0x0800}, {263, 0x2800}, {265, 0x0800}, {279, 0x2800}, {281, 0x0800}, {295, 0x0802}, {297, 0x0800},
    {310, 0x0000}};
static const AimInput k_aim_reload_empty_inputs[] = { /* aim_07_reload_empty */
    {10, 0x0800}, {39, 0x2800}, {41, 0x0800}, {55, 0x2800}, {57, 0x0800}, {71, 0x2800}, {73, 0x0800},
    {87, 0x2800}, {89, 0x0800}, {103, 0x2800}, {105, 0x0800}, {119, 0x2800}, {121, 0x0800}, {135, 0x2800},
    {137, 0x0800}, {151, 0x2800}, {153, 0x0800}, {167, 0x2800}, {169, 0x0800}, {183, 0x2800}, {185, 0x0800},
    {199, 0x2800}, {201, 0x0800}, {215, 0x2800}, {217, 0x0800}, {231, 0x2800}, {233, 0x0800}, {247, 0x2800},
    {249, 0x0800}, {263, 0x2800}, {265, 0x0800}, {279, 0x2800}, {281, 0x0800}, {295, 0x2800}, {297, 0x0800},
    {311, 0x2800}, {313, 0x0800}, {327, 0x2800}, {329, 0x0800}, {343, 0x2800}, {345, 0x0800}, {359, 0x2800},
    {361, 0x0800}, {375, 0x2800}, {377, 0x0800}, {391, 0x2800}, {393, 0x0800}, {407, 0x2800}, {409, 0x0800},
    {423, 0x2800}, {425, 0x0800}, {439, 0x2800}, {441, 0x0800}, {455, 0x2800}, {457, 0x0800}, {471, 0x2800},
    {473, 0x0800}, {487, 0x2800}, {489, 0x0800}, {503, 0x2800}, {505, 0x0800}, {598, 0x2800}, {600, 0x0800},
    {614, 0x2800}, {616, 0x0800}, {630, 0x2800}, {632, 0x0800}, {646, 0x2800}, {648, 0x0800}, {662, 0x2800},
    {664, 0x0800}, {678, 0x2800}, {680, 0x0800}, {694, 0x2800}, {696, 0x0800}, {710, 0x2800}, {712, 0x0800},
    {726, 0x2800}, {728, 0x0800}, {742, 0x2800}, {744, 0x0800}, {758, 0x2800}, {760, 0x0800}, {774, 0x2800},
    {776, 0x0800}, {790, 0x2800}, {792, 0x0800}, {806, 0x2800}, {808, 0x0800}, {822, 0x2800}, {824, 0x0800},
    {838, 0x2800}, {840, 0x0800}, {854, 0x2800}, {856, 0x0800}, {870, 0x2800}, {872, 0x0800}, {886, 0x2800},
    {888, 0x0800}, {902, 0x2800}, {904, 0x0800}, {918, 0x2800}, {920, 0x0800}, {934, 0x2800}, {936, 0x0800},
    {950, 0x2800}, {952, 0x0800}, {966, 0x2800}, {968, 0x0800}, {982, 0x2800}, {984, 0x0800}, {998, 0x2800},
    {1000, 0x0800}, {1014, 0x2800}, {1016, 0x0800}, {1030, 0x2800}, {1032, 0x0800}, {1046, 0x2800},
    {1048, 0x0800}, {1062, 0x2800}, {1064, 0x0800}, {1078, 0x2800}, {1080, 0x0800}, {1110, 0x2800},
    {1112, 0x0800}, {1142, 0x0802}, {1144, 0x0800}, {1174, 0x0000}};
static const AimInput k_aim_light_inputs[] = { /* aim_08_light_holster */
    {10, 0x0800}, {39, 0x8800}, {41, 0x0800}, {81, 0x4800}, {83, 0x0800}, {123, 0x0000}, {170, 0x0200},
    {219, 0x8200}, {221, 0x0200}, {251, 0x0000}};
static const AimInput k_aim_both_inputs[] = { /* aim_02_r1_r2_both */
    {10, 0x0800}, {49, 0x0A00}, {89, 0x0800}, {129, 0x0A00}, {159, 0x0200}, {199, 0x0000}, {264, 0x0A00},
    {303, 0x0000}};
static const AimInput k_aim_melee_inputs[] = { /* aim_09_melee */
    {10, 0x2000}, {12, 0x0000}, {71, 0x2000}, {73, 0x0000}, {86, 0x2000}, {88, 0x0000}, {100, 0x2000},
    {102, 0x0000}, {181, 0x8000}, {183, 0x0000}};
static const AimReplay k_aim_replays[] = {
    {"aim_fire", k_aim_fire_inputs, sizeof k_aim_fire_inputs / sizeof k_aim_fire_inputs[0], 0x41400000u, 0x1D, 0x31, 460, 10},
    {"aim_reload", k_aim_reload_inputs, sizeof k_aim_reload_inputs / sizeof k_aim_reload_inputs[0], 0x41000000u, 0x1D, 0x31, 417, 10},
    {"aim_reload_empty", k_aim_reload_empty_inputs, sizeof k_aim_reload_empty_inputs / sizeof k_aim_reload_empty_inputs[0], 0x41500000u, 0x1D, 0x31, 1231, 10},
    {"aim_light", k_aim_light_inputs, sizeof k_aim_light_inputs / sizeof k_aim_light_inputs[0], 0x41500000u, 0x1D, 0x31, 316, 10},
    {"aim_both", k_aim_both_inputs, sizeof k_aim_both_inputs / sizeof k_aim_both_inputs[0], 0x41400000u, 0x1D, 0x31, 368, 10},
    {"aim_melee", k_aim_melee_inputs, sizeof k_aim_melee_inputs / sizeof k_aim_melee_inputs[0], 0x41500000u, 0x21, 0x36, 267, 10},
    {"aim_world", NULL, 0, 0x41200000u, 0x00, 0x00, 1404, 5},
    {"aim_burst", NULL, 0, 0x41500000u, 0x00, 0x00, 471, 10},
    {"aim_cable", NULL, 0, 0x41900000u, 0x00, 0x00, 1448 + 585, 5},
};

static uint16_t aim_input_at(const AimInput *in, unsigned n, int frame)
{
    uint16_t b = 0;
    for (unsigned i = 0; i < n && in[i].f <= frame; ++i) b = in[i].buttons;
    return b;
}


static void aim_script_begin(void)
{
    aim_hold_begin();
    s_aim_script_n = 0;
    const char *path = getenv("EM_AIM_PAD_SCRIPT");
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) {
        fail("the stick replays need the capture's pad script (EM_AIM_PAD_SCRIPT; tools/test_level_smoke_aim.py)");
        return;
    }
    int fr;
    unsigned b, lx, ly;
    while (fscanf(f, "%d %x %u %u", &fr, &b, &lx, &ly) == 4) {
        if (s_aim_script_n == AIM_SCRIPT_MAX || lx > 255 || ly > 255 ||
            (s_aim_script_n && fr < s_aim_script[s_aim_script_n - 1].f)) {
            fail("the pad script is too long, out of range or out of order");
            break;
        }
        s_aim_script[s_aim_script_n++] = (AimStick){fr, (uint16_t)b, (uint8_t)lx, (uint8_t)ly};
    }
    fclose(f);
    if (!s_aim_script_n) fail("the pad script is empty");
}

/* The capture's pad at its frame: both stick bytes as the pad delivers them
 * (em_pad_raw: 0x80 + axis * 128). Before the script's first entry the
 * stick rests at 0x80 and no button is held. */
static void aim_script_apply(int frame)
{
    const AimStick *in = NULL;
    for (unsigned i = 0; i < s_aim_script_n && s_aim_script[i].f <= frame; ++i) in = &s_aim_script[i];
    if (!in) {
        pad_apply(0, 0, 0);
        return;
    }
    pad_apply(in->buttons, (in->lx - 128) / 128.0f, (in->ly - 128) / 128.0f);
}

static int aim_replay_frame(void)
{
    const AimReplay *r = NULL;
    for (unsigned i = 0; i < sizeof k_aim_replays / sizeof k_aim_replays[0]; ++i)
        if (strcmp(k_aim_replays[i].phase, k_phases[t.current].name) == 0) r = &k_aim_replays[i];
    if (!r) {
        fail("an AIM replay phase without its capture script");
        return 0;
    }
    const EmPlayerLiveActor *a = player_states_actor();
    if (t.step < 6) {
        /* The walk-in and the alignment of aim_hold_frame, which presses
         * the capture's input of frame 10 at the aligned tick. */
        s_aim_align_clock = r->align_clock;
        const int rc = aim_hold_frame(r->inputs ? aim_input_at(r->inputs, r->count, r->first) : 0, r->state,
                                      r->action, 0);
        s_aim_align_clock = 0x41500000u;
        if (t.step == 6) {
            /* aligned: this tick takes the capture's input of its first
             * frame; tools/test_level_smoke.py finds the tick by this line */
            if (!r->inputs) aim_script_apply(r->first);
            fprintf(stderr, "level smoke: %s: aligned counter=%u\n", k_phases[t.current].name,
                    em_frame_counter());
        }
        return rc;
    }
    ++t.frames;
    const int frame = t.frames + r->first;
    if (t.frames == 1 && (em_live_u8(a, 5) != r->state || em_live_u8(a, 0x1F0) != r->action)) {
        fail("the capture's first input did not enter its state on the next tick (the capture's f13)");
        return 0;
    }
    if (frame + 2 < r->last) {
        if (r->inputs) pad_apply(aim_input_at(r->inputs, r->count, frame), 0, 0);
        else aim_script_apply(frame);
        return 0;
    }
    pad_apply(0, 0, 0);
    if (em_live_u8(a, 5) != 0) {
        fail("the player is not back in idle at the capture's last row");
        return 0;
    }
    fprintf(stderr, "level smoke: %s: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", k_phases[t.current].name,
            g.pos[0], g.pos[1], g.pos[2], g.yaw);
    return 1;
}

/* ---------------------------------------------------- fence_door_side1
 *
 * The C7 capture c7_door1_fence_door_side1 (decomp docs/CAPTURES_C7.md
 * section 4), from route beat 09's end (entry 2, behind the fence): the
 * stick turns toward the door, walks to (422.8, 282.0; 0.6 stick, within
 * 1.0), then to
 * the capture's press stance (422.757, 284.633; f225) at 0.4 stick within
 * 0.1, faces its heading -0.12148 and presses Cross (f225, two
 * frames). The capture's own approach walk (f0..f224) is its tool's
 * navigation, not compared. 00184BA0 selects the door from the south; its
 * 001BBE40 (f228: side 1, the alignment and heading) starts the program
 * with clip 0x43; 001BC150 (f306: B7 = 1, B8 = 2); 0x1AE040 state 4 re-places
 * the player at entry 1 (f371), where 001B07C0(1) writes +4 = 5, +5 = 1, +6 =
 * 0: 0015B610 runs 00183250, the arrival walk-out (52 frames standing, 30
 * walking, 30 slowing), whose exit returns +4 = 1, +5 = 0, +1F0 = 0 (f484).
 * In process: the scan, the door's phases 3 / 4 / 5 and 0, B8 = 2, the
 * record at +4 = 5 after the re-place, D_00810702 = 1, and control back on
 * +4 = 1. tools/test_level_smoke.py check_fence_door_side1 compares the
 * capture row for row. */
enum { FENCE_DOOR_SIDE1_LIMIT = 900 };

static void fence_door_side1_begin(void)
{
    nav_reset();
    if (em_scene_state()->d810700 != 0x0B || em_scene_state()->d810702 != 2)
        fail("fence door side 1 starts in AREA11 room 2 (route beat 09's end)");
}

static int fence_door_side1_frame(void)
{
    uint8_t head[16], block[16];
    int door = em_area11_door_state(head, block);
    const EmPlayerLiveActor *rec = player_states_actor();
    if (door && head[5] == 3) t.saw[0] = 1;
    if (door && head[5] == 4) t.saw[1] = 1;
    if (door && head[5] == 5) t.saw[2] = 1;
    if (em_scene_state()->req[EM_SCENE_REQ_B8] == 2) t.saw[3] = 1;
    if (rec->bytes[4] == 5 && rec->bytes[5] == 1) t.saw[4] = 1;
    switch (t.step) {
    case 0: /* the turn from entry 2's heading pi takes longer than one face() */
    case 1: NAV_STEP(nav_face(-0.14f));
    case 2: NAV_STEP(nav_goto(422.8f, 282.0f, 1.0f, 0.6f, 0));
    case 3: NAV_STEP(nav_settle(5));
    case 4: NAV_STEP(nav_goto(422.757f, 284.633f, 0.1f, 0.4f, 1));
    case 5: NAV_STEP(nav_settle(20));
    case 6: NAV_STEP(nav_face(-0.12148f));
    case 7: NAV_STEP(nav_settle(30));
    case 8:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 9:
        (void)scan_accepted();
        pad_apply(0, 0, 0);
        if (!(door && t.saw[4] && head[5] == 0 && em_scene_state()->d810702 == 1 && rec->bytes[4] == 1 &&
              rec->bytes[5] == 0 && rec->bytes[0x1F0] == 0 && in_control())) {
            if (++t.nav_frames > FENCE_DOOR_SIDE1_LIMIT)
                fail(t.saw[4] ? "the arrival walk-out did not return the player (+4 = 1)"
                     : t.saw[0] ? "the door's room move did not re-place the player at entry 1 in state 5/1"
                                : "Cross at the fence door from side 1 did not start the door (+5 = 3)");
            return 0;
        }
        nav_reset();
        ++t.step;
        return 0;
    default: {
        /* 61 frames: the capture ends 60 rows after control returns (f544). */
        int r = nav_settle(61);
        if (r <= 0)
            return 0;
        if (!t.saw[7] || !t.saw[0] || !t.saw[1] || !t.saw[2] || !t.saw[3] || head[0x0B] != 0) {
            fprintf(stderr, "level smoke: fence_door_side1: scan %u phases 3/4/5 %u/%u/%u B8=2 %u armed %02X\n",
                    t.saw[7], t.saw[0], t.saw[1], t.saw[2], t.saw[3], head[0x0B]);
            fail("the door did not run its phases 3, 4, 5 and the room move from the Use scan");
            return 0;
        }
        fprintf(stderr, "level smoke: fence_door_side1: PASS scan_d810750=%d player=(%.3f,%.5f,%.3f) yaw=%.5f "
                        "room=%u\n", (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw,
                em_scene_state()->d810702);
        return 1;
    }
    }
}

/* -------------------------------------------------------- cage_ladders
 *
 * Route beat 10's two climbs (route_capture.py beat_cage_roof_roger up to
 * the roof): from the truck crossing's end the stick walks (360, 320) then
 * (360, 296) (walk_path, tolerance 1.0), settles, goes to (360, 293.5) at
 * 0.4 stick, settles, faces pi (face() settles 10 frames after the turn)
 * and presses Cross: 00160220 -> 0015D4C0 case 0x32 on the column's
 * authored node enters the ladder (+5 = 0xB, +1F0 = 0x15, clip 0xE3; route
 * f268); once 001662D0 has taken over (+1F0 = 0x17) the stick is held up
 * (the climb, clips 0xE8 / 0xEA, +3 y per cycle; the dismount +1F0 = 0x18
 * with clip 0xF0) until +1F0 leaves the ladder's actions; the player stands
 * on the cage floor (route f577: y 225.374). Then (359.8, 262) at 0.5
 * stick, face pi, Cross and the same climb to the roof (route f780..f1089:
 * y 264.912). In process: both presses entered state 0xB, the climbs
 * reached 0x17 and 0x18 and ended at the route's heights.
 * tools/test_level_smoke.py check_cage_ladders compares both climbs with
 * the capture row for row. */
enum { LADDER_LIMIT = 900 };
static const float k_cage_path[2][2] = {{360.0f, 320.0f}, {360.0f, 296.0f}};

/* walk_path over `path` (count waypoints): each until within `tol`, or
 * blocked (45 frames moving less than 0.3). 1 when the path ends, 0
 * continue, -1 failed. The waypoint index is t.saw[6]. */
static int walk_path(const float (*path)[2], int count, float tol)
{
    if (t.saw[6] >= count) {
        pad_apply(0, 0, 0);
        t.saw[6] = 0;
        nav_reset();
        return 1;
    }
    const float *wp = path[t.saw[6]];
    int slot = t.nav_hist_n % 64;
    t.nav_hist[slot][0] = g.pos[0];
    t.nav_hist[slot][1] = g.pos[2];
    ++t.nav_hist_n;
    int blocked = 0;
    if (t.nav_hist_n > 45) {
        int old = (t.nav_hist_n - 1 - 45) % 64;
        blocked = hypotf(g.pos[0] - t.nav_hist[old][0], g.pos[2] - t.nav_hist[old][1]) < 0.3f;
    }
    if (nav_stick_toward(wp[0], wp[1], 1.0f) <= tol || blocked) {
        t.nav_hist_n = 0;
        ++t.saw[6];
    }
    if (++t.nav_frames > NAV_LIMIT) {
        fail("the walk did not end");
        return -1;
    }
    return 0;
}

/* route_capture ladder(): after the press, wait for 001662D0 (+1F0 0x17),
 * then hold the stick up until +1F0 leaves 0x15 / 0x17 / 0x18. t.saw[slot]
 * records 0xB seen (bit 0), 0x17 (bit 1) and 0x18 (bit 2). */
static int ladder_climb(int slot)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t state = em_live_u8(a, 5), mode = em_live_u8(a, 0x1F0);
    if (state == 0xB && mode == 0x15)
        t.saw[slot] |= 1;
    if (mode == 0x17)
        t.saw[slot] |= 2;
    if (mode == 0x18)
        t.saw[slot] |= 4;
    if (++t.nav_frames > LADDER_LIMIT) {
        fail(t.saw[slot] & 1 ? "the ladder climb did not end" : "Cross at the ladder did not enter state 0xB");
        return -1;
    }
    if (!(t.saw[slot] & 2)) {
        pad_apply(0, 0, 0);
        if (t.nav_frames > 90 && !(t.saw[slot] & 1)) {
            fail("Cross at the ladder did not enter state 0xB (+1F0 0x15)");
            return -1;
        }
        return 0;
    }
    /* The capture's stick reached the game two frames after the row that
     * showed 0x17 (the pad latency, FIRST_LEVEL_ROUTE.md section 6: route
     * 10 clip 0xE6 through f330, the climb clip from f331); the overlay's
     * reaches it on the next frame, so the stick waits two frames more. */
    if (t.saw[slot + 2] < 2) {
        ++t.saw[slot + 2];
        pad_apply(0, 0, 0);
        return 0;
    }
    if (mode == 0x15 || mode == 0x17 || mode == 0x18) {
        pad_apply(0, 0, -1.0f);
        return 0;
    }
    pad_apply(0, 0, 0);
    nav_reset();
    if (t.saw[slot] != 7) {
        fail("the ladder did not run entry 0xB, climb 0x17 and dismount 0x18");
        return -1;
    }
    return 1;
}

static void cage_ladders_begin(void)
{
    nav_reset();
}

static int cage_ladders_frame(void)
{
    switch (t.step) {
    case 0: NAV_STEP(walk_path(k_cage_path, 2, 1.0f));
    case 1: NAV_STEP(nav_settle(5));
    case 2: NAV_STEP(nav_goto(360.0f, 293.5f, 0.5f, 0.4f, 1));
    case 3: NAV_STEP(nav_settle(5));
    case 4: NAV_STEP(nav_face(3.14159265f));
    case 5: NAV_STEP(nav_settle(10));
    case 6: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 7: NAV_STEP(ladder_climb(0));
    case 8:
        if (fabsf(g.pos[1] - 225.374f) > 0.001f) {
            fprintf(stderr, "level smoke: cage_ladders: after ladder A y %.5f\n", g.pos[1]);
            fail("ladder A did not end on the cage floor (route f577: y 225.374)");
            return 0;
        }
        ++t.step;
        return 0;
    case 9: NAV_STEP(nav_goto(359.8f, 262.0f, 0.6f, 0.5f, 1));
    case 10: NAV_STEP(nav_settle(5));
    case 11: NAV_STEP(nav_face(3.14159265f));
    case 12: NAV_STEP(nav_settle(10));
    case 13: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 14: NAV_STEP(ladder_climb(1));
    case 15:
        if (fabsf(g.pos[1] - 264.912f) > 0.001f) {
            fprintf(stderr, "level smoke: cage_ladders: after ladder B y %.5f\n", g.pos[1]);
            fail("ladder B did not end on the cage roof (route f1089: y 264.912)");
            return 0;
        }
        fprintf(stderr, "level smoke: cage_ladders: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0],
                g.pos[1], g.pos[2], g.yaw);
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------- the director's beats
 *
 * cage_roof, crevice_prompt and east_tower are the director 008253F0's
 * beats 0, 1 and 2 (FIRST_LEVEL_ROUTE.md section 5): scripts 0x8294C0,
 * 0x829A40 and 0x829CC0 on the original owner (em_director_original over
 * em_area11_script_host, live since WP-8b), and in beat 10 Roger's
 * alternate script 0x828990 with its voiced line 0x7F; beats 1 and 2 show
 * the voiced lines 0x97 and 0x99. Each beat starts on its own when the
 * previous phase leaves the player inside its quad. The pad stays neutral
 * until the beat has stored its step byte D_00810813 (beat 0: the
 * director's 0x10, then Roger's ordinary branch's 0x11 on the next frame,
 * route 10 f3508 / f3509; beats 1 and 2: 0x20 and 0xFF) and control is
 * back, then settles 90 frames (route_capture settle(); each of the three
 * captures ends 60 rows after its release, and a port whose voiced line
 * tore down early releases early: tools/test_level_smoke.py
 * check_director_beat), so the capture checks compare the release and the
 * follow camera to the capture's end. None of the three
 * scripts moves the player (routes 10 f1089..f3508, 11 f706..f1200 and 13
 * f531..f749 keep +B0..+B8). */
enum { DIRECTOR_LIMIT = 6000, DIRECTOR_SETTLE = 90 };

static uint8_t director_step(void)
{
    const uint8_t *b = em_scene_progress_at(em_scene_state(), 0x00810813u, 1);
    return b ? *b : 0xEE;
}

static int director_beat(uint8_t want)
{
    pad_apply(0, 0, 0);
    if (!t.saw[0]) {
        if (director_step() != want || !in_control()) {
            if (++t.nav_frames > DIRECTOR_LIMIT)
                fail("the director's beat did not end with its step byte and control");
            return 0;
        }
        t.saw[0] = 1;
        nav_reset();
    }
    int r = nav_settle(DIRECTOR_SETTLE);
    if (r <= 0)
        return 0;
    fprintf(stderr, "level smoke: %s: PASS D_00810813 = 0x%02X player=(%.3f,%.5f,%.3f)\n",
            k_phases[t.current].name, director_step(), g.pos[0], g.pos[1], g.pos[2]);
    return 1;
}

static void director_begin(void) { nav_reset(); }
static int cage_roof_frame(void) { return director_beat(0x11); }
static int crevice_prompt_frame(void) { return director_beat(0x20); }
static int east_tower_frame(void) { return director_beat(0xFF); }

/* The ledge climbs of beats 11 and 13: with `fine` the stick first walks
 * to the route's stance before the press at 0.4 stick (navigation input,
 * within 0.1, as the boxes do); without it the preceding walk's stop is
 * the stance, as in route_capture.py (the pipe end: a walk into the pipe's
 * end face stops beside it, and a second approach there slides along the
 * face). Then the player faces the route's heading at the press and
 * settles 30 frames. `land`: the climb ends where the director's beat
 * takes over (routes 11 f706 and 13 f531: 3B8D = 3 on the landing row),
 * so the step passes when +5 leaves 2 instead of waiting for control. */
static int climb_landed(int index)
{
    uint8_t state = em_live_u8(player_states_actor(), 5);
    if (state == 2)
        t.saw[index] = 1;
    if (t.saw[index] && state != 2) {
        nav_reset();
        return 1;
    }
    if (++t.nav_frames > 200) {
        fail(t.saw[index] ? "the ledge climb did not land" : "Cross did not enter the ledge climb (+5 = 2)");
        return -1;
    }
    return 0;
}

static int stance_climb(int step, const float stance[3], int fine, int slot, int land, float top,
                        const char *what)
{
    switch (step) {
    case 0: return fine ? nav_goto(stance[0], stance[1], 0.1f, 0.4f, 1) : 1;
    case 1: return nav_settle(20);
    case 2: return nav_face(stance[2]);
    case 3: return nav_settle(30);
    case 4: return nav_press(EM_PAD_CROSS, 2);
    case 5: return land ? climb_landed(slot) : boxes_climb(slot);
    case 6:
        if (fabsf(g.pos[1] - top) > 0.001f) {
            fprintf(stderr, "level smoke: %s: after the climb y %.5f\n", k_phases[t.current].name, g.pos[1]);
            fail(what);
            return -1;
        }
        return 1;
    default:
        return 1;
    }
}

/* ------------------------------------------------------ crevice_climbs
 *
 * Route beat 11 up to director beat 1 (route_capture.py beat_crevice_prompt):
 * the stick walks (385, 238), (407, 240) east over the bridge (tolerance
 * 1.0); the tank climb from the route's stance (405.283, 240.018), facing
 * 1.5637 (f169): the ledge climb (state 2, +1F0 8, clips 0x70 / 0x79 /
 * 0x8C) onto the tank top at y 286.09 (f264). Then the pipes: the
 * waypoints (420, 262) .. (470, 292) (tolerance 1.5; the short drop at f498
 * is the fall 5 / 0xB and its landing 8 / 0xF; the extra waypoint
 * (470, 300) makes the port's last leg straight down -z, as the
 * original's was), and the pipe-end climb from where that walk stops
 * (route (470.039, 289.876)), facing -3.0327 (f639; clips
 * 0x70 / 0x77 / 0x8C) onto y 279.9 (f705). In process: both presses entered
 * the ledge climb and ended at the route's heights. tools/test_level_smoke.py
 * check_crevice_climbs compares both climbs with the capture. */
static const float k_bridge_path[2][2] = {{385.0f, 238.0f}, {407.0f, 240.0f}};
static const float k_pipe_path[12][2] = {{420.0f, 262.0f}, {416.0f, 270.0f}, {412.0f, 276.0f}, {405.0f, 285.0f},
                                         {401.0f, 300.0f}, {410.0f, 312.0f}, {430.0f, 330.0f}, {450.0f, 347.0f},
                                         {462.0f, 355.0f}, {470.0f, 340.0f}, {470.0f, 300.0f}, {470.0f, 292.0f}};
static const float k_tank_stance[3] = {405.283f, 240.018f, 1.5637f};
static const float k_pipe_stance[3] = {470.039f, 289.876f, -3.0327f};

static void crevice_climbs_begin(void) { nav_reset(); }

static int crevice_climbs_frame(void)
{
    if (t.step == 0) NAV_STEP(walk_path(k_bridge_path, 2, 1.0f));
    if (t.step == 1) NAV_STEP(nav_settle(5));
    if (t.step >= 2 && t.step <= 8)
        NAV_STEP(stance_climb(t.step - 2, k_tank_stance, 1, 0, 0, 286.09f,
                              "the tank climb did not end on the tank top (route f264: y 286.09)"));
    if (t.step == 9) NAV_STEP(nav_settle(10));
    if (t.step == 10) NAV_STEP(walk_path(k_pipe_path, 12, 1.5f));
    if (t.step == 11) NAV_STEP(nav_settle(5));
    if (t.step >= 12 && t.step <= 18)
        NAV_STEP(stance_climb(t.step - 12, k_pipe_stance, 0, 1, 1, 279.9f,
                              "the pipe-end climb did not end on the pipe (route f705: y 279.9)"));
    fprintf(stderr, "level smoke: crevice_climbs: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0], g.pos[1],
            g.pos[2], g.yaw);
    return 1;
}

/* -------------------------------------------------------- crevice_jump
 *
 * Route beat 12 (route_capture.py beat_crevice_jump): the stick walks
 * (485, 275), (477, 262) (tolerance 1.0; the drop off the pipe at f42 is
 * the fall 5 / 0xB), settles 5, faces pi, then runs toward (477, 150) until
 * z <= 249.5, where Cross is held for two frames with the stick kept
 * (f227). 00160220's running-jump probe 0015EC50 enters state 6 (+1F0
 * 0x0C, clips 0x69 / 0x6B; f230) and 001634A0 carries the player across
 * the crevice onto the north block (8 / 0xF, clip 0x6E; landing f277 at
 * (476.4, 269.84, 188.1)). The stick stays on until +1F0 leaves 0x0C and
 * 0x0F, then the pad is released and the player settles. In process: the
 * jump was entered and landed on the north block (z below 210, y
 * 269.84). tools/test_level_smoke.py check_crevice_jump compares the jump
 * with the capture. */
static const float k_jump_path[2][2] = {{485.0f, 275.0f}, {477.0f, 262.0f}};
enum { JUMP_LIMIT = 200 };

/* ------------------------------------------------------------- damage
 *
 * The DAMAGE side runs (docs/DAMAGE.md section 8; decomp CAPTURES_C10.md
 * "DAMAGE"), each its own run from the main line: the capture lane's own
 * closed-loop policies (route_capture.py dmg_beat_*), driven by the port's
 * state, as programs of steps:
 *   dmg_flame         (from crevice_prompt) dmg_00 .. dmg_04: one flame
 *                     contact, the retreat, contacts to 35, the heartbeat
 *                     idle, contacts to 10, contacts to 0, the death and the
 *                     game over with no input, the title after a death,
 *                     Up and Cross, the New Game to first control;
 *   dmg_crevice_fall  (from crevice_prompt) dmg_06: the walking jump short
 *                     of the north block, the landing hit;
 *   dmg_pit_fall      (from truck_preview) dmg_07: the truck's fall, the
 *                     walk off its roof onto the pit floor, the game over.
 * Each beat starts after 35 neutral ticks (the capture's pin: its source
 * snapshot's counter + 30, then idle 5). tools/level_smoke_damage.py
 * compares the run's tick log with the recordings, each window aligned on
 * its event (the hit, the latch, the death, the game-over task, the title,
 * the confirm). In process: the steps' predicates, no fault. Test input
 * only: the policies are the capture tool's. */
enum {
    DS_END = 0, DS_IDLE, DS_HITS_TO, DS_RETREAT, DS_DEATH_TO_GAME_OVER, DS_UNTIL_TITLE, DS_PRESS_UP,
    DS_PRESS_CROSS_NEW_GAME, DS_UNTIL_FIRST_CONTROL, DS_WALK_PATH, DS_SETTLE, DS_FACE, DS_WALKING_JUMP,
    DS_UNTIL_LANDING_HAND_BACK, DS_GOTO, DS_UNTIL_TRUCK_DOWN, DS_WALK_OFF_TRUCK, DS_PRESS_CROSS_LOAD,
    DS_UNTIL_LOAD_SCREEN, DS_PRESS_TRIANGLE_LEAVE, DS_UNTIL_TITLE_PROMPT
};
typedef struct {
    int kind;
    float a, b, c, d;
    const char *what;
} DmgStep;
#define DMG_IDLE(n) {DS_IDLE, (n), 0, 0, 0, "idle"}
static const float k_dmg_flame_xz[2] = {452.3f, 277.6f};   /* the flame's +0xB0 x / z (r7) */
static const float k_dmg_flame_rest[2] = {471.3f, 283.2f};  /* the 11_crevice_prompt release point */
static const DmgStep k_dmg_flame[] = {
    DMG_IDLE(35), {DS_HITS_TO, 95.0f, 0, 0, 0, "dmg_00: one flame contact"},
    {DS_RETREAT, 0, 0, 0, 0, "dmg_00: retreat"}, DMG_IDLE(30),
    DMG_IDLE(35), {DS_HITS_TO, 35.0f, 0, 0, 0, "dmg_01: contacts to 35"},
    {DS_RETREAT, 0, 0, 0, 0, "dmg_01: retreat"}, DMG_IDLE(300),
    DMG_IDLE(35), {DS_HITS_TO, 10.0f, 0, 0, 0, "dmg_02: contacts to 10"},
    {DS_RETREAT, 0, 0, 0, 0, "dmg_02: retreat"}, DMG_IDLE(150),
    {DS_HITS_TO, 0.0f, 0, 0, 0, "dmg_02: contacts to 0"},
    {DS_DEATH_TO_GAME_OVER, 0, 0, 0, 0, "dmg_02: death to the GAME OVER screen"},
    {DS_UNTIL_TITLE, 0, 0, 0, 0, "dmg_03: the hold runs out; the title after a death"},
    DMG_IDLE(30), DMG_IDLE(45), DMG_IDLE(5),
    {DS_PRESS_UP, 0, 0, 0, 0, "dmg_04: Up to the first entry"}, DMG_IDLE(10),
    {DS_PRESS_CROSS_NEW_GAME, 0, 0, 0, 0, "dmg_04: Cross (New Game)"},
    {DS_UNTIL_FIRST_CONTROL, 0, 0, 0, 0, "dmg_04: the New Game to first control"}, DMG_IDLE(60),
    {DS_END, 0, 0, 0, 0, NULL}};
/* dmg_load: dmg_flame to the title after a death, then dmg_05
 * (route_capture dmg_beat_load_screen): Cross until the menu confirms
 * (001AC480 sub 3) or leaves it, until state 5 with the fade idle, 60
 * neutral ticks, Triangle (30-tick waits) until state 5 is left, until the
 * title menu takes input again, 30 ticks. */
static const DmgStep k_dmg_load[] = {
    DMG_IDLE(35), {DS_HITS_TO, 95.0f, 0, 0, 0, "dmg_00: one flame contact"},
    {DS_RETREAT, 0, 0, 0, 0, "dmg_00: retreat"}, DMG_IDLE(30),
    DMG_IDLE(35), {DS_HITS_TO, 35.0f, 0, 0, 0, "dmg_01: contacts to 35"},
    {DS_RETREAT, 0, 0, 0, 0, "dmg_01: retreat"}, DMG_IDLE(300),
    DMG_IDLE(35), {DS_HITS_TO, 10.0f, 0, 0, 0, "dmg_02: contacts to 10"},
    {DS_RETREAT, 0, 0, 0, 0, "dmg_02: retreat"}, DMG_IDLE(150),
    {DS_HITS_TO, 0.0f, 0, 0, 0, "dmg_02: contacts to 0"},
    {DS_DEATH_TO_GAME_OVER, 0, 0, 0, 0, "dmg_02: death to the GAME OVER screen"},
    {DS_UNTIL_TITLE, 0, 0, 0, 0, "dmg_03: the hold runs out; the title after a death"},
    DMG_IDLE(30), DMG_IDLE(45), DMG_IDLE(5),
    {DS_PRESS_CROSS_LOAD, 0, 0, 0, 0, "dmg_05: Cross (LOAD GAME)"},
    {DS_UNTIL_LOAD_SCREEN, 0, 0, 0, 0, "dmg_05: the load screen with the fade idle"}, DMG_IDLE(60),
    {DS_PRESS_TRIANGLE_LEAVE, 0, 0, 0, 0, "dmg_05: Triangle (the screen's exit)"},
    {DS_UNTIL_TITLE_PROMPT, 0, 0, 0, 0, "dmg_05: the title menu again"}, DMG_IDLE(30),
    {DS_END, 0, 0, 0, 0, NULL}};
static const float k_dmg_crevice_path[2][2] = {{485.0f, 275.0f}, {477.0f, 262.0f}};
static const DmgStep k_dmg_crevice[] = {
    DMG_IDLE(35), {DS_WALK_PATH, 0, 0, 0, 0, "dmg_06: to the plateau edge"},
    {DS_SETTLE, 5, 0, 0, 0, "dmg_06: settle"}, {DS_FACE, 3.14159265f, 0, 0, 0, "dmg_06: face north"},
    {DS_WALKING_JUMP, 477.0f, 150.0f, 252.0f, 0.45f, "dmg_06: the walking jump"},
    {DS_UNTIL_LANDING_HAND_BACK, 0, 0, 0, 0, "dmg_06: the landing hit and the hand-back"}, DMG_IDLE(30),
    {DS_END, 0, 0, 0, 0, NULL}};
static const DmgStep k_dmg_pit[] = {
    DMG_IDLE(35), {DS_GOTO, 352.0f, 392.0f, 1.5f, 1.0f, "dmg_07: onto the truck"},
    {DS_UNTIL_TRUCK_DOWN, 0, 0, 0, 0, "dmg_07: the truck's fall"}, DMG_IDLE(30),
    {DS_WALK_OFF_TRUCK, 352.0f, 440.0f, 0, 0, "dmg_07: off the roof"},
    {DS_DEATH_TO_GAME_OVER, 0, 0, 0, 0, "dmg_07: death to the GAME OVER screen"},
    {DS_END, 0, 0, 0, 0, NULL}};

static struct {
    const DmgStep *prog;
    int pc, ticks, sub, hits, tries;
    float hp0;
} D;

static void dmg_begin(void)
{
    nav_reset();
    memset(&D, 0, sizeof D);
    const char *name = k_phases[t.current].name;
    D.prog = strcmp(name, "dmg_flame") == 0 ? k_dmg_flame
             : strcmp(name, "dmg_load") == 0 ? k_dmg_load
             : strcmp(name, "dmg_crevice_fall") == 0 ? k_dmg_crevice : k_dmg_pit;
}

/* route_capture dmg_controllable / the hit loop's walk gate. */
static int dmg_walkable(const EmPlayerLiveActor *a)
{
    return em_live_u8(a, 4) == 1 && (em_live_u8(a, 5) == 0 || em_live_u8(a, 5) == 1);
}

/* dmg_gameover_screen: task 001ACEC0 at 3 / 2 / 3 with the fade idle. */
static int dmg_game_over_screen(void)
{
    return task_byte(EM_SCENE_TASK_08) == 3 && task_byte(EM_SCENE_TASK_09) == 2 &&
           task_byte(EM_SCENE_TASK_0A) == 3 && em_frame_transition()->substate == 0;
}

static int dmg_next(void)
{
    pad_apply(0, 0, 0);
    if (D.prog[D.pc].what)
        fprintf(stderr, "level smoke: %s: done %s at tick %u counter %u\n", k_phases[t.current].name,
                D.prog[D.pc].what, (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
    ++D.pc;
    D.ticks = D.sub = D.tries = 0;
    nav_reset();
    t.step = 0;
    return 0;
}

static int dmg_timeout(int limit, const char *what)
{
    if (++D.ticks > limit) {
        char reason[160];
        snprintf(reason, sizeof reason, "%s: no progress in %d ticks", what, limit);
        fail(reason);
        return -1;
    }
    return 0;
}

static int dmg_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    const DmgStep *st = &D.prog[D.pc];
    unsigned cursor = 0;
    switch (st->kind) {
    case DS_END:
        fprintf(stderr, "level smoke: %s: PASS hits=%d health=%.1f player=(%.3f,%.5f,%.3f)\n",
                k_phases[t.current].name, D.hits, g.status.health, g.pos[0], g.pos[1], g.pos[2]);
        return 1;
    case DS_IDLE:
        pad_apply(0, 0, 0);
        if (++D.ticks >= (int)st->a) return dmg_next();
        return 0;
    case DS_HITS_TO:
        /* dmg_flame_hit, repeated while the health is above the target:
         * walk at the flame while walkable, neutral otherwise, until the
         * health drops; then neutral until +4 == 1 or the health is 0. */
        if (D.sub == 0) {
            if (g.status.health <= st->a) return dmg_next();
            D.hp0 = g.status.health;
            D.sub = 1;
            D.ticks = 0;
        }
        if (D.sub == 1) {
            if (g.status.health < D.hp0) {
                ++D.hits;
                pad_apply(0, 0, 0);
                D.sub = 2;
                D.ticks = 0;
                return 0;
            }
            if (dmg_walkable(a)) nav_stick_toward(k_dmg_flame_xz[0], k_dmg_flame_xz[1], 1.0f);
            else pad_apply(0, 0, 0);
            return dmg_timeout(400, "no flame hit") < 0 ? 0 : 0;
        }
        pad_apply(0, 0, 0);
        if (em_live_u8(a, 4) == 1 || g.status.health <= 0.0f) {
            D.sub = 0;
            return 0;
        }
        return dmg_timeout(300, "the hit reaction did not hand back") < 0 ? 0 : 0;
    case DS_RETREAT:
        /* dmg_retreat. */
        if (D.sub == 0) {
            pad_apply(0, 0, 0);
            if (dmg_walkable(a)) { D.sub = 1; nav_reset(); }
            else (void)dmg_timeout(300, "the retreat's wait for control");
            return 0;
        }
        if (D.sub == 1) {
            int r = nav_goto(k_dmg_flame_rest[0], k_dmg_flame_rest[1], 1.0f, 1.0f, 1);
            if (r > 0) { D.sub = 2; D.ticks = 0; }
            return 0;
        }
        pad_apply(0, 0, 0);
        if (em_live_u8(a, 0) == 1 && g.pd_iframes <= 0 && in_control()) return dmg_next();
        (void)dmg_timeout(300, "the retreat's protection wait");
        return 0;
    case DS_DEATH_TO_GAME_OVER:
        pad_apply(0, 0, 0);
        if (D.sub == 0) {
            if (em_scene_state()->req[EM_SCENE_REQ_B9] == 1) { D.sub = 1; D.ticks = 0; }
            else (void)dmg_timeout(300, "D_008106B9");
            return 0;
        }
        if (D.sub == 1) {
            if (dmg_game_over_screen()) { D.sub = 2; D.ticks = 0; }
            else (void)dmg_timeout(1200, "the GAME OVER screen");
            return 0;
        }
        if (++D.ticks >= 10) return dmg_next();
        return 0;
    case DS_UNTIL_TITLE:
        pad_apply(0, 0, 0);
        if (em_frontend_title_menu(&cursor)) {
            fprintf(stderr, "level smoke: %s: the title menu takes input at counter %u, cursor %u\n",
                    k_phases[t.current].name, em_frame_counter(), cursor);
            if (cursor != 1) {
                fail("the title after a death opened with the cursor off its second entry");
                return 0;
            }
            return dmg_next();
        }
        (void)dmg_timeout(1200, "the title after a death");
        return 0;
    case DS_PRESS_UP:
    case DS_PRESS_CROSS_NEW_GAME: {
        /* dmg_press_until: a two-tick press, then up to 20 ticks for its
         * effect, at most six times. */
        const int menu = em_frontend_title_menu(&cursor);
        const int done = st->kind == DS_PRESS_UP ? (menu && cursor == 0) : !menu;
        if (done && D.sub >= 2) return dmg_next();
        if (D.sub < 2) {
            if (D.sub == 0)
                fprintf(stderr, "level smoke: %s: press %s at counter %u\n", k_phases[t.current].name,
                        st->kind == DS_PRESS_UP ? "UP" : "CROSS", em_frame_counter());
            pad_apply(st->kind == DS_PRESS_UP ? EM_PAD_UP : EM_PAD_CROSS, 0, 0);
            ++D.sub;
            return 0;
        }
        pad_apply(0, 0, 0);
        if (++D.ticks > 20) {
            if (++D.hits > 6) { fail("a title press had no effect"); return 0; }
            D.sub = 0;
            D.ticks = 0;
        }
        return 0;
    }
    case DS_PRESS_CROSS_LOAD:
    case DS_PRESS_TRIANGLE_LEAVE: {
        /* dmg_press_until: a two-tick press, then up to `wait` ticks for its
         * effect (20; Triangle 30), at most six times. Cross: the menu's
         * confirm (sub 3) or the menu left; Triangle: state 5 left. */
        unsigned major = 0, sub = 0;
        em_frontend_title_state(&major, &sub);
        const int cross = st->kind == DS_PRESS_CROSS_LOAD;
        const int done = cross ? (major != 2 || sub == 3) : major != 5;
        if (done && D.sub >= 2) return dmg_next();
        if (D.sub < 2) {
            if (D.sub == 0)
                fprintf(stderr, "level smoke: %s: press %s at counter %u\n", k_phases[t.current].name,
                        cross ? "CROSS (load)" : "TRIANGLE", em_frame_counter());
            pad_apply(cross ? EM_PAD_CROSS : EM_PAD_TRIANGLE, 0, 0);
            ++D.sub;
            return 0;
        }
        pad_apply(0, 0, 0);
        if (++D.ticks > (cross ? 20 : 30)) {
            if (++D.tries >= 6) { fail("a title press had no effect"); return 0; }
            D.sub = 0;
            D.ticks = 0;
        }
        return 0;
    }
    case DS_UNTIL_LOAD_SCREEN: {
        unsigned major = 0, sub = 0;
        pad_apply(0, 0, 0);
        em_frontend_title_state(&major, &sub);
        if (major == 5 && em_frame_transition()->substate == 0) return dmg_next();
        (void)dmg_timeout(600, "the load screen");
        return 0;
    }
    case DS_UNTIL_TITLE_PROMPT:
        pad_apply(0, 0, 0);
        if (em_frontend_title_menu(&cursor)) {
            fprintf(stderr, "level smoke: %s: the title menu takes input again at counter %u, cursor %u\n",
                    k_phases[t.current].name, em_frame_counter(), cursor);
            return dmg_next();
        }
        (void)dmg_timeout(900, "the title menu after the load screen");
        return 0;
    case DS_UNTIL_FIRST_CONTROL:
        /* route_capture dmg_beat_new_game: the area 0x0B with the frame
         * machine in state 1, then the opening (3B8D set), then control. */
        pad_apply(0, 0, 0);
        if (D.sub == 0) {
            if (em_frontend_title_menu(NULL) == 0 && em_scene_state()->d810700 == 0x0B &&
                task_byte(EM_SCENE_TASK_0B) == 1 && em_scene_state()->spad3B8D != 0)
                D.sub = 1;
            (void)dmg_timeout(12000, "the opening after the New Game");
            return 0;
        }
        if (in_control() && em_scene_state()->d810700 == 0x0B && task_byte(EM_SCENE_TASK_08) == 3 &&
            em_live_u8(a, 4) == 1) {
            fprintf(stderr, "level smoke: %s: first control again at tick %u counter %u (%.3f,%.5f,%.3f) "
                            "yaw=%.5f health=%.1f\n", k_phases[t.current].name,
                    (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter(), g.pos[0], g.pos[1], g.pos[2],
                    g.yaw, g.status.health);
            return dmg_next();
        }
        (void)dmg_timeout(12000, "first control after the New Game");
        return 0;
    case DS_WALK_PATH: {
        int r = walk_path(k_dmg_crevice_path, 2, 1.0f);
        if (r > 0) return dmg_next();
        return 0;
    }
    case DS_SETTLE: {
        int r = nav_settle((int)st->a);
        if (r > 0) return dmg_next();
        return 0;
    }
    case DS_FACE: {
        int r = nav_face(st->a);
        if (r > 0) return dmg_next();
        return 0;
    }
    case DS_WALKING_JUMP: {
        /* dmg_running_jump(start None, toward (a, b), z <= c, magnitude d,
         * stall False): run toward the point until the edge test, Cross for
         * two ticks with the stick held, keep the stick until +1F0 has been
         * 0x0C and left it, then release. */
        const uint8_t mode = em_live_u8(a, 0x1F0);
        if (D.sub == 0) {
            if (g.pos[2] <= st->c) { D.sub = 1; D.ticks = 0; }
            else {
                nav_stick_toward(st->a, st->b, st->d);
                (void)dmg_timeout(400, "the edge");
                return 0;
            }
        }
        if (D.sub == 1 || D.sub == 2) {
            const EmPadState keep = t.pad;
            pad_apply(EM_PAD_CROSS, keep.lx, keep.ly);
            ++D.sub;
            return 0;
        }
        if (D.sub == 3) {
            const EmPadState keep = t.pad;
            pad_apply(0, keep.lx, keep.ly);
            if (mode == 0x0C) D.sub = 4;
            else if (++D.ticks > 10) { fail("the walking jump did not start (+1F0 0x0C)"); }
            return 0;
        }
        const EmPadState keep = t.pad;
        pad_apply(0, keep.lx, keep.ly);
        if (mode != 0x0C) return dmg_next();
        (void)dmg_timeout(300, "the walking jump's landing");
        return 0;
    }
    case DS_UNTIL_LANDING_HAND_BACK:
        pad_apply(0, 0, 0);
        if (D.sub == 0) {
            if (em_live_u8(a, 5) == 8) { D.sub = 1; D.ticks = 0; }
            else (void)dmg_timeout(200, "the landing state +5 = 8");
            return 0;
        }
        if (D.sub == 1) {
            if (in_control() && em_live_u8(a, 4) == 1 && em_live_u8(a, 5) == 0) { D.sub = 2; D.ticks = 0; }
            else (void)dmg_timeout(400, "control after the landing");
            return 0;
        }
        if (g.pd_iframes <= 0) return dmg_next();
        (void)dmg_timeout(200, "the landing's protection");
        return 0;
    case DS_GOTO: {
        int r = nav_goto(st->a, st->b, st->c, st->d, 1);
        if (r > 0) return dmg_next();
        return 0;
    }
    case DS_UNTIL_TRUCK_DOWN: {
        pad_apply(0, 0, 0);
        const uint8_t *story = em_scene_progress_at(em_scene_state(), 0x00810792u, 1);
        if (story && *story == 0xFF) return dmg_next();
        (void)dmg_timeout(400, "the truck's fall (D_00810792 = 0xFF)");
        return 0;
    }
    case DS_WALK_OFF_TRUCK:
        if (em_live_u8(a, 5) == 5 || g.status.health <= 0.0f) return dmg_next();
        if (dmg_walkable(a)) nav_stick_toward(st->a, st->b, 1.0f);
        else pad_apply(0, 0, 0);
        (void)dmg_timeout(300, "the walk off the truck");
        return 0;
    default:
        fail("unknown damage step");
        return 0;
    }
}

/* ------------------------------------------------------------- branches
 *
 * The BRANCH side runs (audit 1b item 16; decomp CAPTURES_C10.md
 * "BRANCH"; LEVEL_SMOKE.md "The BRANCH side runs"), each its own run from
 * the main line: the capture lane's own closed-loop policies
 * (route_capture.py br_beat_*), driven by the port's state, as programs of
 * steps. A run that plays several recordings (br_west_ledge: br_05..br_08;
 * br_plateau: br_11..br_13) plays them in the capture's order, each from the
 * previous one's end, as the recordings do. Each beat starts after 35
 * neutral ticks (the capture's pin: its source snapshot's counter + 30,
 * then idle 5) and prints "beat <name> at tick N counter C", the checker's
 * slice. tools/level_smoke_branch.py compares the run's tick log with the
 * recordings, each window aligned on its event (the use scan, the ladder's
 * grab, the box's hit). In process: the steps' predicates, no fault. Test
 * input only: the policies are the capture tool's. */
enum {
    BS_END = 0, BS_BEAT, BS_IDLE, BS_WALK, BS_GOTO, BS_SETTLE, BS_FACE, BS_TAKE, BS_LADDER_UP,
    BS_LADDER_DOWN, BS_CLIMB, BS_RUN_OFF, BS_MELEE, BS_TERMINAL, BS_PANEL_DECLINE, BS_ROGER_TALK
};
typedef struct {
    int kind;
    float a, b, c, d;
    const float (*path)[2];
    int n;
    const char *what;
} BrStep;
#define BR_BEAT(name) {BS_BEAT, 0, 0, 0, 0, NULL, 0, name}
#define BR_IDLE(k) {BS_IDLE, (k), 0, 0, 0, NULL, 0, NULL}
#define BR_SETTLE(k) {BS_SETTLE, (k), 0, 0, 0, NULL, 0, NULL}
#define BR_WALK(p, tol) {BS_WALK, (tol), 0, 0, 0, (p), (int)(sizeof(p) / sizeof((p)[0])), "walk"}
#define BR_GOTO(x, z, tol, mag) {BS_GOTO, (x), (z), (tol), (mag), NULL, 0, "goto"}
#define BR_FACE(yaw) {BS_FACE, (yaw), 0.12f, 0, 0, NULL, 0, "face"}
#define BR_TAKE(x, z, puid) {BS_TAKE, (x), (z), (puid), 0, NULL, 0, "take"}
#define BR_PI 3.14159265f
/* route_capture.py: the ladders' foot and top stances and their faces. */
#define BR_WEST_YAW (-1.27522f)    /* atan2(-0.956, 0.292) */
#define BR_PLATEAU_YAW (-1.39586f) /* atan2(-0.985, 0.174) */
#define BR_PIPE_YAW (-0.70404f)    /* atan2(-0.647, 0.762) */
enum { BR_R3 = 0x7A7980u, BR_R5 = 0x7A7F60u, BR_R6 = 0x7A8250u };

static const float k_br01_path[][2] = {{255.0f, 400.0f}, {238.0f, 422.0f}};
static const float k_br04_path[][2] = {{222.0f, 270.0f}, {214.7f, 282.5f}};
static const float k_br05_path[][2] = {{412.0f, 240.0f}, {398.0f, 215.0f}, {362.0f, 214.0f}};
static const float k_br06_path[][2] = {{316.0f, 240.0f}, {316.0f, 300.0f}, {314.0f, 316.0f}};
static const float k_br08_path[][2] = {{314.0f, 300.0f}, {316.0f, 240.0f}, {316.0f, 226.0f}};
static const float k_br09_path[][2] = {{360.0f, 320.0f}, {360.0f, 296.0f}};
static const float k_br09_floor[][2] = {{366.0f, 272.0f}};
static const float k_br10_path[][2] = {{440.0f, 255.0f}, {458.0f, 235.0f}};
static const float k_br11_path[][2] = {{480.0f, 300.0f}, {481.0f, 330.0f}, {484.0f, 352.0f}};
static const float k_br11_foot[][2] = {{486.0f, 388.0f}, {481.0f, 399.0f}};
static const float k_br12_path[][2] = {{459.5f, 393.0f}, {445.0f, 392.0f}, {432.0f, 394.5f}};
static const float k_br13_path[][2] = {{432.0f, 394.5f}, {445.0f, 392.0f}, {459.5f, 393.0f}, {461.0f, 404.5f}};

static const BrStep k_br_ledge_ammo[] = {
    BR_BEAT("br_00_ledge_ammo"), BR_IDLE(35), BR_GOTO(216.5f, 308.0f, 0.8f, 0.5f), BR_SETTLE(10),
    BR_TAKE(213.6f, 311.9f, 6), {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
static const BrStep k_br_map_item[] = {
    BR_BEAT("br_01_map_item"), BR_IDLE(35), BR_WALK(k_br01_path, 2.0f), BR_GOTO(235.5f, 424.5f, 0.8f, 0.5f),
    BR_SETTLE(10), BR_TAKE(231.1f, 428.0f, 9), {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
static const BrStep k_br_elevator_up[] = {
    BR_BEAT("br_02_elevator_up"), BR_IDLE(35), BR_FACE(-1.3037f), BR_SETTLE(10),
    {BS_TERMINAL, 0, 0, 0, 0, NULL, 0, "the terminal's ride back up"}, {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
/* use_panel's stance: the panel phase's press point (the port's walk stops
 * shorter than route_capture's goal; em_level_smoke_test.c panel_frame). */
static const BrStep k_br_panel_decline[] = {
    BR_BEAT("br_03_panel_decline"), BR_IDLE(35), BR_GOTO(239.7f, 216.0f, 1.0f, 1.0f),
    BR_GOTO(240.5f, 225.0f, 0.8f, 0.5f), BR_SETTLE(20), BR_FACE(0.0f), BR_SETTLE(10),
    {BS_PANEL_DECLINE, 0, 0, 0, 0, NULL, 0, "the panel, No, Triangle"}, {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
static const BrStep k_br_crate_stack[] = {
    BR_BEAT("br_04_crate_stack_break"), BR_IDLE(35), BR_WALK(k_br04_path, 1.0f),
    BR_GOTO(214.7f, 284.0f, 0.6f, 0.5f), BR_SETTLE(10), {BS_MELEE, BR_R5, 0, 0, 0, NULL, 0, "box r5"},
    BR_IDLE(180), {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
static const BrStep k_br_west_ledge[] = {
    BR_BEAT("br_05_west_ladder_up"), BR_IDLE(35), BR_WALK(k_br05_path, 2.0f), BR_SETTLE(5),
    {BS_CLIMB, -BR_PI / 2, 0, 0, 0, NULL, 0, "the corridor box"},
    {BS_RUN_OFF, 326.0f, 215.0f, 332.0f, 0, NULL, 0, "off the box's west side"}, BR_SETTLE(10),
    BR_GOTO(326.5f, 216.5f, 0.6f, 0.5f), BR_SETTLE(5), {BS_LADDER_UP, BR_WEST_YAW, 0, 0, 0, NULL, 0, "up"},
    BR_BEAT("br_06_ledge_crate_break"), BR_IDLE(35), BR_WALK(k_br06_path, 2.0f),
    BR_GOTO(312.0f, 319.0f, 0.6f, 0.5f), BR_SETTLE(10), {BS_MELEE, BR_R6, 0, 0, 0, NULL, 0, "box r6"},
    BR_BEAT("br_07_ledge_magazine"), BR_IDLE(35), BR_GOTO(312.0f, 321.0f, 0.8f, 0.5f), BR_SETTLE(10),
    BR_TAKE(311.6f, 328.7f, 8),
    BR_BEAT("br_08_west_ladder_down"), BR_IDLE(35), BR_WALK(k_br08_path, 2.0f),
    BR_GOTO(316.0f, 218.5f, 0.6f, 0.5f), BR_SETTLE(5),
    {BS_LADDER_DOWN, BR_WEST_YAW + BR_PI, 0, 0, 0, NULL, 0, "down"}, {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
static const BrStep k_br_cage_key[] = {
    BR_BEAT("br_09_cage_key"), BR_IDLE(35), BR_WALK(k_br09_path, 1.0f), BR_SETTLE(5),
    BR_GOTO(360.0f, 293.5f, 0.6f, 0.5f), BR_SETTLE(5), {BS_LADDER_UP, BR_PI, 0, 0, 0, NULL, 0, "ladder A"},
    BR_WALK(k_br09_floor, 1.0f), BR_GOTO(375.0f, 266.8f, 0.8f, 0.5f), BR_SETTLE(10),
    BR_TAKE(381.3f, 266.8f, 7), {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
static const BrStep k_br_yard_ammo[] = {
    BR_BEAT("br_10_yard_ammo"), BR_IDLE(35), BR_WALK(k_br10_path, 2.0f), BR_GOTO(462.0f, 231.0f, 0.8f, 0.5f),
    BR_SETTLE(10), BR_TAKE(467.3f, 227.4f, 4), {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
static const BrStep k_br_plateau[] = {
    BR_BEAT("br_11_plateau_ladder_up"), BR_IDLE(35), BR_WALK(k_br11_path, 2.0f), BR_SETTLE(5),
    {BS_CLIMB, BR_PIPE_YAW, 0, 0, 0, NULL, 0, "the raised pipe"},
    {BS_RUN_OFF, 484.0f, 384.0f, 0, 275.0f, NULL, 0, "off the pipe's south side"}, BR_SETTLE(10),
    BR_WALK(k_br11_foot, 2.0f), BR_GOTO(477.5f, 403.0f, 0.6f, 0.5f), BR_SETTLE(5),
    {BS_LADDER_UP, BR_PLATEAU_YAW, 0, 0, 0, NULL, 0, "up"},
    BR_BEAT("br_12_tower_ammo"), BR_IDLE(35), BR_WALK(k_br12_path, 1.5f), BR_GOTO(430.5f, 404.5f, 0.8f, 0.5f),
    BR_SETTLE(10), BR_TAKE(431.9f, 411.8f, 5),
    BR_BEAT("br_13_plateau_ladder_down"), BR_IDLE(35), BR_WALK(k_br13_path, 1.5f),
    BR_GOTO(467.0f, 404.5f, 0.6f, 0.5f), BR_SETTLE(5),
    {BS_LADDER_DOWN, BR_PLATEAU_YAW + BR_PI, 0, 0, 0, NULL, 0, "down"}, {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};
static const BrStep k_br_roger_talk[] = {
    BR_BEAT("br_14_roger_talk"), BR_IDLE(35), {BS_ROGER_TALK, 0, 0, 0, 0, NULL, 0, "Roger's talk"},
    {BS_END, 0, 0, 0, 0, NULL, 0, NULL}};

static const struct {
    const char *phase;
    const BrStep *prog;
} k_br_programs[] = {
    {"br_ledge_ammo", k_br_ledge_ammo}, {"br_map_item", k_br_map_item}, {"br_elevator_up", k_br_elevator_up},
    {"br_panel_decline", k_br_panel_decline}, {"br_crate_stack", k_br_crate_stack},
    {"br_west_ledge", k_br_west_ledge}, {"br_cage_key", k_br_cage_key}, {"br_yard_ammo", k_br_yard_ammo},
    {"br_plateau", k_br_plateau}, {"br_roger_talk", k_br_roger_talk},
};

static struct {
    const BrStep *prog;
    int pc, sub, ticks, tries, still;
    float y0, tx, tz;
    uint8_t key[0x28], down0;
    /* The recordings' pad latency (below, br_frame): the pads the program
     * set on the last two ticks, and the one the overlay holds. */
    EmPadState queue[2], applied;
    int queued;
} B;

/* ------------------------------------------------------------ options
 *
 * The OPTIONS side run (docs/OPTIONS.md section 6; decomp CAPTURES_C10.md
 * "OPTIONS"): from the end of truck_crossing (the recordings' source
 * snapshot 08_truck_crossing), the nine beats opt_00..opt_08 in a row, each
 * after 35 neutral ticks (the capture's pin: its source snapshot's counter
 * + 30, then idle 5), as programs of the capture lane's own closed-loop
 * policies (route_capture.py opt_beat_*): every press is a two-tick tap
 * repeated (at most six times) until its effect shows in the bytes the
 * recordings sample. Every beat ends in control with the settings, the
 * offset and the masks back at their start values, so the next starts from
 * the recordings' start state. Each beat prints "beat <name> at tick N
 * counter C", each press that took effect "press <button> at tick N";
 * tools/level_smoke_options.py compares the tick log with the recordings
 * beat by beat, aligned on those events. The run's options screen frames
 * close out neither a world nor a status frame: the phase runs every
 * iteration (Phase.every_tick). Test input only: the policies are the
 * capture tool's. */
enum {
    OS_END = 0, OS_BEAT, OS_IDLE, OS_SETTLE, OS_OPEN, OS_CURSOR, OS_UP_TO, OS_CLOSE, OS_PRESS, OS_UNTIL,
    OS_TYPE_TO
};
enum {
    OP_MENU = 1,       /* the options screen in state `arg` (-1: any) */
    OP_NOT_MENU,       /* the options screen closed */
    OP_SETTING,        /* the settings byte `arg` changed since the press */
    OP_SETTING_IS,     /* the settings byte `arg >> 8` is `arg & 0xFF` */
    OP_OFFSET,         /* the screen offset changed since the press */
    OP_ENTER,          /* the options screen in state 10 or `arg` */
    OP_MC_STATE_GE,    /* the card screen's state D_00810040 >= arg */
    OP_QUIT_IS,        /* the quit prompt's choice +0x13 == arg, its sub-state 1 */
    OP_IN_CONTROL,     /* route_capture in_control */
    OP_MC_FADE,        /* the card screen in state 1 and the fade idle */
    OP_MENU_FADE       /* the options screen in state `arg` and the fade idle */
};
typedef struct {
    int kind;
    uint16_t button;
    int pred, arg, wait;
    const char *what;
} OptStep;
#define OPT_BEAT(name) {OS_BEAT, 0, 0, 0, 0, name}
#define OPT_IDLE(n) {OS_IDLE, 0, 0, (n), 0, NULL}
#define OPT_OPEN {OS_OPEN, EM_PAD_SELECT, OP_MENU, 1, 40, "open"}
#define OPT_CURSOR(i) {OS_CURSOR, 0, 0, (i), 0, "cursor"}
#define OPT_CLOSE(b, name) {OS_CLOSE, (b), OP_NOT_MENU, 0, 60, name}
#define OPT_PRESS(b, p, a, w, name) {OS_PRESS, (b), (p), (a), (w), name}
#define OPT_UNTIL(p, a, limit) {OS_UNTIL, 0, (p), (a), (limit), NULL}
/* opt_toggle(row): Cross enters 00201720 (state 5), Right flips the byte,
 * Cross keeps it. */
#define OPT_TOGGLE(row, byte)                                                                       \
    OPT_CURSOR(row), OPT_PRESS(EM_PAD_CROSS, OP_MENU, 5, 30, "enter"), OPT_IDLE(10),               \
    OPT_PRESS(EM_PAD_RIGHT, OP_SETTING, (byte), 30, "right"), OPT_IDLE(20),                        \
    OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 30, "keep"), OPT_IDLE(20)
/* opt_enter_module(row, state): Cross, state 10 (module 0x2B), the row's
 * screen. */
#define OPT_ENTER(row, state)                                                                       \
    OPT_CURSOR(row), OPT_PRESS(EM_PAD_CROSS, OP_ENTER, (state), 20, "enter"),                      \
    OPT_UNTIL(OP_MENU, (state), 600), OPT_IDLE(20)
#define OPT_MOVE(b) OPT_PRESS((b), OP_OFFSET, 0, 30, "move"), OPT_IDLE(6)
#define OPT_END {OS_END, 0, 0, 0, 0, NULL}
static const OptStep k_opt_program[] = {
    /* opt_00_browse_close */
    OPT_BEAT("opt_00_browse_close"), OPT_IDLE(35), OPT_OPEN,
    OPT_CURSOR(1), OPT_CURSOR(2), OPT_CURSOR(3), OPT_CURSOR(4), OPT_CURSOR(5), OPT_CURSOR(6), OPT_CURSOR(7),
    OPT_CURSOR(8), OPT_CURSOR(0),
    {OS_UP_TO, 0, 0, 8, 0, "up"}, {OS_UP_TO, 0, 0, 7, 0, "up"}, {OS_UP_TO, 0, 0, 6, 0, "up"},
    {OS_UP_TO, 0, 0, 5, 0, "up"}, {OS_UP_TO, 0, 0, 4, 0, "up"}, {OS_UP_TO, 0, 0, 3, 0, "up"},
    {OS_UP_TO, 0, 0, 2, 0, "up"}, {OS_UP_TO, 0, 0, 1, 0, "up"}, {OS_UP_TO, 0, 0, 0, 0, "up"},
    OPT_CLOSE(EM_PAD_CROSS, "close CROSS"),
    OPT_OPEN, OPT_CLOSE(EM_PAD_CIRCLE, "close CIRCLE"),
    OPT_OPEN, OPT_CLOSE(EM_PAD_TRIANGLE, "close TRIANGLE"),
    OPT_OPEN, OPT_CLOSE(EM_PAD_SELECT, "close SELECT"),
    /* opt_01_vibration */
    OPT_BEAT("opt_01_vibration"), OPT_IDLE(35), OPT_OPEN, OPT_TOGGLE(1, 1), OPT_TOGGLE(1, 1), OPT_CURSOR(0),
    OPT_CLOSE(EM_PAD_CROSS, "close CROSS"),
    /* opt_02_sound */
    OPT_BEAT("opt_02_sound"), OPT_IDLE(35), OPT_OPEN, OPT_TOGGLE(2, 4), OPT_TOGGLE(2, 4), OPT_CURSOR(0),
    OPT_CLOSE(EM_PAD_CROSS, "close CROSS"),
    /* opt_03_screen_position */
    OPT_BEAT("opt_03_screen_position"), OPT_IDLE(35), OPT_OPEN,
    OPT_ENTER(3, 7), OPT_MOVE(EM_PAD_UP), OPT_MOVE(EM_PAD_UP), OPT_MOVE(EM_PAD_UP), OPT_MOVE(EM_PAD_LEFT),
    OPT_MOVE(EM_PAD_LEFT), OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 60, "keep"), OPT_IDLE(20),
    OPT_ENTER(3, 7), OPT_MOVE(EM_PAD_DOWN), OPT_MOVE(EM_PAD_DOWN), OPT_MOVE(EM_PAD_DOWN),
    OPT_MOVE(EM_PAD_RIGHT), OPT_MOVE(EM_PAD_RIGHT), OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 60, "keep"),
    OPT_IDLE(20),
    OPT_ENTER(3, 7), OPT_MOVE(EM_PAD_UP), OPT_MOVE(EM_PAD_UP), OPT_PRESS(EM_PAD_CIRCLE, OP_MENU, 1, 60, "back"),
    OPT_IDLE(20), OPT_CURSOR(0), OPT_CLOSE(EM_PAD_CROSS, "close CROSS"),
    /* opt_04_brightness */
    OPT_BEAT("opt_04_brightness"), OPT_IDLE(35), OPT_OPEN,
    OPT_ENTER(4, 8), OPT_IDLE(30), OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 60, "back CROSS"), OPT_IDLE(20),
    OPT_ENTER(4, 8), OPT_IDLE(30), OPT_PRESS(EM_PAD_CIRCLE, OP_MENU, 1, 60, "back CIRCLE"), OPT_IDLE(20),
    OPT_ENTER(4, 8), OPT_IDLE(30), OPT_PRESS(EM_PAD_TRIANGLE, OP_NOT_MENU, 0, 60, "close TRIANGLE"),
    OPT_UNTIL(OP_IN_CONTROL, 0, 600), {OS_SETTLE, 0, 0, 20, 0, NULL},
    /* opt_05_button_config */
    OPT_BEAT("opt_05_button_config"), OPT_IDLE(35), OPT_OPEN,
    OPT_ENTER(5, 9), {OS_TYPE_TO, 0, 0, 1, 0, "type"}, OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 60, "keep"), OPT_IDLE(20),
    OPT_ENTER(5, 9), {OS_TYPE_TO, 0, 0, 2, 0, "type"}, OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 60, "keep"), OPT_IDLE(20),
    OPT_ENTER(5, 9), {OS_TYPE_TO, 0, 0, 0, 0, "type"}, OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 60, "keep"), OPT_IDLE(20),
    OPT_CURSOR(0), OPT_CLOSE(EM_PAD_CROSS, "close CROSS"),
    /* opt_06_default */
    OPT_BEAT("opt_06_default"), OPT_IDLE(35), OPT_OPEN, OPT_TOGGLE(1, 1), OPT_CURSOR(7),
    OPT_PRESS(EM_PAD_CROSS, OP_MENU, 6, 30, "enter"), OPT_IDLE(20), OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 30, "no"),
    OPT_IDLE(20), OPT_PRESS(EM_PAD_CROSS, OP_MENU, 6, 30, "enter"), OPT_IDLE(10),
    OPT_PRESS(EM_PAD_RIGHT, OP_SETTING_IS, 0x301, 30, "right"), OPT_IDLE(20),
    OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 30, "yes"), OPT_IDLE(20), OPT_CURSOR(0),
    OPT_CLOSE(EM_PAD_CROSS, "close CROSS"),
    /* opt_07_load_cancel */
    OPT_BEAT("opt_07_load_cancel"), OPT_IDLE(35), OPT_OPEN, OPT_CURSOR(6),
    OPT_PRESS(EM_PAD_CROSS, OP_MENU, 3, 30, "enter"), OPT_UNTIL(OP_MC_FADE, 0, 900),
    {OS_IDLE, 0, 0, 60, 0, "slot choice"}, OPT_PRESS(EM_PAD_TRIANGLE, OP_MC_STATE_GE, 2, 40, "back"),
    OPT_UNTIL(OP_MENU_FADE, 1, 900), {OS_IDLE, 0, 0, 30, 0, "list again"}, OPT_CURSOR(0),
    OPT_CLOSE(EM_PAD_CROSS, "close CROSS"),
    /* opt_08_quit_cancel */
    OPT_BEAT("opt_08_quit_cancel"), OPT_IDLE(35), OPT_OPEN, OPT_CURSOR(8),
    OPT_PRESS(EM_PAD_CROSS, OP_MENU, 4, 30, "enter"), OPT_IDLE(30),
    OPT_PRESS(EM_PAD_RIGHT, OP_QUIT_IS, 1, 40, "right"), OPT_IDLE(20),
    OPT_PRESS(EM_PAD_RIGHT, OP_QUIT_IS, 0, 40, "right"), OPT_IDLE(20),
    OPT_PRESS(EM_PAD_CROSS, OP_MENU, 1, 60, "no"), OPT_IDLE(20),
    OPT_PRESS(EM_PAD_CROSS, OP_MENU, 4, 30, "enter"), OPT_IDLE(30),
    OPT_PRESS(EM_PAD_CIRCLE, OP_MENU, 1, 60, "back CIRCLE"), OPT_IDLE(20),
    OPT_PRESS(EM_PAD_CROSS, OP_MENU, 4, 30, "enter"), OPT_IDLE(30),
    OPT_PRESS(EM_PAD_TRIANGLE, OP_NOT_MENU, 0, 60, "close TRIANGLE"),
    OPT_UNTIL(OP_IN_CONTROL, 0, 600), {OS_SETTLE, 0, 0, 20, 0, NULL},
    OPT_END};

/* The interpreter models the capture tool's frames: every hook call decides
 * the pad of the next frame (an "emit"); checks and step changes take no
 * frame, as route_capture's predicate checks between its steps do. Each
 * step is a small machine over O.part (its pieces: a settle, a tap, an
 * idle). */
enum { EMIT = 0, NEXT = 1 };

static struct {
    int pc, started, prev, credit;
    int part, ticks, tap, wait, tries, target;
    uint16_t button;
    uint8_t before[0x10];
    int16_t offset[2];
    EmPadState queue[2], applied;
    int queued;
} O;

/* Each side phase opt_NN plays its beat's segment of the program: from its
 * OPT_BEAT entry to the next one (or the end). */
static void opt_begin(void)
{
    nav_reset();
    memset(&O, 0, sizeof O);
    const char *name = k_phases[t.current].name;
    O.pc = -1;
    for (int i = 0; k_opt_program[i].kind != OS_END || i == 0; ++i)
        if (k_opt_program[i].kind == OS_BEAT && strncmp(k_opt_program[i].what, name, strlen(name)) == 0 &&
            k_opt_program[i].what[strlen(name)] == '_') {
            O.pc = i;
            break;
        }
    if (O.pc < 0)
        fail("no OPTIONS program for this phase");
}

/* A byte of the slot-0 task record (the hooks run inside or after the
 * task dispatch). */
static uint8_t opt_task_byte(unsigned offset)
{
    EmTask *task = (EmTask *)em_task_slot(0);
    const uint8_t *b = task ? em_scene_task_byte(task->user, offset) : NULL;
    return b ? *b : 0xFF;
}

static unsigned opt_cursor(void)
{
    return (unsigned)(opt_task_byte(0x1C) | opt_task_byte(0x1D) << 8);
}

/* The options screen runs: the gameplay task 001ACEC0 in frame state 2. */
static int opt_menu(int state)
{
    const EmTask *task = em_task_slot(0);
    return task && task->fn == em_scene_task_001ACEC0 && opt_task_byte(0x0B) == 2 &&
           (state < 0 || opt_task_byte(0x0C) == (uint8_t)state);
}

/* route_capture in_control: the selector 3B8D 0, +1F0 0 and the status
 * state D_00810131 0. */
static int opt_in_control(void)
{
    const uint8_t *ui = em_status_runtime_ui_block(em_area11_interaction_host_status());
    return !opt_menu(-1) && em_scene_state()->spad3B8D == 0 &&
           em_live_u8(player_states_actor(), 0x1F0) == 0 && (!ui || ui[1] == 0);
}

static int opt_settled(void)
{
    return opt_in_control() && em_live_u16(player_states_actor(), 0x20C) == 0;
}

enum { OP_CURSOR_IS = 100, OP_TYPE_CHANGED };

static int opt_pred(int pred, int arg)
{
    const EmSceneState *sc = em_scene_state();
    switch (pred) {
    case OP_MENU: return opt_menu(arg);
    case OP_NOT_MENU: return !opt_menu(-1);
    case OP_SETTING: return sc->d810118[arg] != O.before[arg];
    case OP_SETTING_IS: return sc->d810118[arg >> 8] == (uint8_t)arg;
    case OP_OFFSET: return sc->spad3B94 != O.offset[0] || sc->spad3B96 != O.offset[1];
    case OP_ENTER: return opt_menu(10) || opt_menu(arg);
    case OP_MC_STATE_GE: return sc->d810040[0] >= arg;
    case OP_QUIT_IS: return opt_task_byte(0x13) == (uint8_t)arg && opt_task_byte(0x0D) == 1;
    case OP_IN_CONTROL: return opt_in_control();
    case OP_MC_FADE: return sc->d810040[0] == 1 && em_frame_transition()->substate == 0;
    case OP_MENU_FADE: return opt_menu(arg) && em_frame_transition()->substate == 0;
    case OP_CURSOR_IS: return (int)opt_cursor() == arg && opt_menu(1);
    case OP_TYPE_CHANGED: return sc->d810118[0] != O.before[0];
    default: return 0;
    }
}

static const char *opt_button_name(uint16_t b)
{
    return b == EM_PAD_SELECT ? "SELECT" : b == EM_PAD_CROSS ? "CROSS" : b == EM_PAD_CIRCLE ? "CIRCLE"
           : b == EM_PAD_TRIANGLE ? "TRIANGLE" : b == EM_PAD_UP ? "UP" : b == EM_PAD_DOWN ? "DOWN"
           : b == EM_PAD_LEFT ? "LEFT" : b == EM_PAD_RIGHT ? "RIGHT" : "?";
}

static int opt_emit(uint16_t buttons)
{
    pad_apply(buttons, 0, 0);
    return EMIT;
}

static void opt_part(int part)
{
    O.part = part;
    O.ticks = O.tap = O.wait = O.tries = 0;
}

static int opt_next(void)
{
    const OptStep *st = &k_opt_program[O.pc];
    if (st->kind == OS_BEAT)
        fprintf(stderr, "level smoke: %s: beat %s at tick %u counter %u\n", k_phases[t.current].name, st->what,
                (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
    ++O.pc;
    opt_part(0);
    O.prev = 0;
    nav_reset();
    return NEXT;
}

/* route_capture idle(n): n frames of the neutral pad. NEXT when done. */
static int opt_idle(int n)
{
    if (O.credit) { /* a frame the step before already idled (OS_UNTIL) */
        O.credit = 0;
        ++O.ticks;
    }
    if (O.ticks < n) {
        ++O.ticks;
        return opt_emit(0);
    }
    return NEXT;
}

/* route_capture settle(n): idle(n), then until in control with the clip
 * +0x20C at 0 (600 frames). NEXT when done, -1 failed. */
static int opt_settle(int n)
{
    if (O.ticks < n) {
        ++O.ticks;
        return opt_emit(0);
    }
    if (opt_settled()) return NEXT;
    if (++O.wait > 600) { fail("control did not return (settle)"); return -1; }
    return opt_emit(0);
}

/* route_capture opt_press(button, pred, wait): tap (two frames), then up
 * to `wait` frames for the predicate (checked before each), once more
 * after them; at most six taps. NEXT when it took effect, -1 failed. */
static int opt_press(uint16_t button, int pred, int arg, int wait, const char *what)
{
    if (O.tap == 0 && O.tries == 0 && O.wait == 0) {
        const EmSceneState *sc = em_scene_state();
        memcpy(O.before, sc->d810118, sizeof O.before);
        O.offset[0] = sc->spad3B94;
        O.offset[1] = sc->spad3B96;
    }
    if (O.tap < 2) {
        if (O.tap == 0)
            fprintf(stderr, "level smoke: %s: press %s (%s) at tick %u counter %u\n", k_phases[t.current].name,
                    opt_button_name(button), what ? what : "-", (unsigned)em_scene_bindings_log_tick_next(),
                    em_frame_counter());
        ++O.tap;
        return opt_emit(button);
    }
    if (opt_pred(pred, arg)) {
        fprintf(stderr, "level smoke: %s: took effect %s at tick %u counter %u\n", k_phases[t.current].name,
                opt_button_name(button), (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
        return NEXT;
    }
    if (O.wait < wait) {
        ++O.wait;
        return opt_emit(0);
    }
    if (++O.tries >= 6) {
        char reason[128];
        snprintf(reason, sizeof reason, "%s: no effect after six presses of %s", what ? what : "a press",
                 opt_button_name(button));
        fail(reason);
        return -1;
    }
    O.tap = 0;
    O.wait = 0;
    return opt_press(button, pred, arg, wait, what);
}

/* One step's hook work: EMIT (the next frame's pad is set), NEXT (the step
 * is done; the caller runs the next one in the same call) or -1. */
static int opt_step(void)
{
    const OptStep *st = &k_opt_program[O.pc];
    const EmSceneState *sc = em_scene_state();
    int r;
    switch (st->kind) {
    case OS_END:
    done:
        fprintf(stderr, "level smoke: %s: PASS settings=%02X %02X %02X %02X %02X offset=(%d,%d) "
                "masks=%04X %04X %04X player=(%.3f,%.5f,%.3f)\n", k_phases[t.current].name, sc->d810118[0],
                sc->d810118[1], sc->d810118[3], sc->d810118[4], sc->d810118[8], sc->spad3B94, sc->spad3B96,
                sc->spad3B74[0], sc->spad3B74[1], sc->spad3B74[2], g.pos[0], g.pos[1], g.pos[2]);
        pad_apply(0, 0, 0);
        return 2;
    case OS_BEAT:
        if (O.started) /* the next beat's segment: this phase's ends here */
            goto done;
        O.started = 1;
        return opt_next();
    case OS_IDLE:
        r = opt_idle(st->arg);
        if (r == NEXT && st->what)
            fprintf(stderr, "level smoke: %s: %s at tick %u counter %u\n", k_phases[t.current].name, st->what,
                    (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
        return r == NEXT ? opt_next() : r;
    case OS_SETTLE:
        r = opt_settle(st->arg);
        return r == NEXT ? opt_next() : r;
    case OS_UNTIL:
        if (st->pred == OP_MC_FADE || st->pred == OP_MENU_FADE) {
            /* The tool tests the fade of the frame's row, after the frame's
             * transition tick; the hook runs at the task's end, before it.
             * So the test of frame k is made at the hook of frame k + 1:
             * the screen state the hook saw then and the fade at this
             * tick's start. Frame k + 1 already had the neutral pad the
             * policy's next idle wants: it counts as its first frame. */
            const int screen = st->pred == OP_MC_FADE ? em_scene_state()->d810040[0] == 1 : opt_menu(st->arg);
            const int ok = O.prev && em_scene_bindings_fade_at_tick_start() == 0;
            O.prev = screen;
            if (ok) {
                O.credit = 1;
                return opt_next();
            }
            if (++O.ticks > st->wait) {
                fail("the options screen did not reach the state the policy waits for");
                return -1;
            }
            return opt_emit(0);
        }
        if (opt_pred(st->pred, st->arg)) return opt_next();
        if (++O.ticks > st->wait) {
            fail("the options screen did not reach the state the policy waits for");
            return -1;
        }
        return opt_emit(0);
    case OS_PRESS:
        r = opt_press(st->button, st->pred, st->arg, st->wait, st->what);
        return r == NEXT ? opt_next() : r;
    case OS_OPEN:
        /* opt_open: settle 10, SELECT until the browse state (40), idle 10. */
        if (O.part == 0) {
            r = opt_settle(10);
            if (r != NEXT) return r;
            opt_part(1);
        }
        if (O.part == 1) {
            r = opt_press(EM_PAD_SELECT, OP_MENU, 1, 40, "open");
            if (r != NEXT) return r;
            opt_part(2);
        }
        r = opt_idle(10);
        return r == NEXT ? opt_next() : r;
    case OS_CLOSE:
        /* opt_close: the button until the screen closes (60), until in
         * control (600), settle 20. */
        if (O.part == 0) {
            r = opt_press(st->button, OP_NOT_MENU, 0, st->wait, st->what);
            if (r != NEXT) return r;
            opt_part(1);
        }
        if (O.part == 1) {
            if (!opt_in_control()) {
                if (++O.ticks > 600) { fail("control did not return after the close"); return -1; }
                return opt_emit(0);
            }
            opt_part(2);
        }
        r = opt_settle(20);
        return r == NEXT ? opt_next() : r;
    case OS_UP_TO:
        /* opt_beat_browse_close's Up run: Up until the cursor is arg, idle 8. */
        if (O.part == 0) {
            r = opt_press(EM_PAD_UP, OP_CURSOR_IS, st->arg, 30, "up");
            if (r != NEXT) return r;
            opt_part(1);
        }
        r = opt_idle(8);
        return r == NEXT ? opt_next() : r;
    case OS_CURSOR:
        /* opt_cursor_to: Down (or Up when shorter) to the next row until the
         * cursor shows it in the browse state, idle 8; until the row. */
        for (;;) {
            if (O.part == 0) {
                const int cursor = (int)opt_cursor();
                if (cursor == st->arg) return opt_next();
                const int down = ((st->arg - cursor) % 9 + 9) % 9 <= ((cursor - st->arg) % 9 + 9) % 9;
                O.button = down ? EM_PAD_DOWN : EM_PAD_UP;
                O.target = (cursor + (down ? 1 : -1) + 9) % 9;
                opt_part(1);
            }
            if (O.part == 1) {
                r = opt_press(O.button, OP_CURSOR_IS, O.target, 30, "cursor");
                if (r != NEXT) return r;
                opt_part(2);
            }
            r = opt_idle(8);
            if (r != NEXT) return r;
            opt_part(0);
        }
    case OS_TYPE_TO:
        /* opt_button_type: Right / Left until the type is arg, idle 10. */
        for (;;) {
            if (O.part == 0) {
                const int type = sc->d810118[0];
                if (type == st->arg) return opt_next();
                O.button = st->arg > type ? EM_PAD_RIGHT : EM_PAD_LEFT;
                opt_part(1);
            }
            if (O.part == 1) {
                r = opt_press(O.button, OP_TYPE_CHANGED, 0, 30, "type");
                if (r != NEXT) return r;
                opt_part(2);
            }
            r = opt_idle(10);
            if (r != NEXT) return r;
            opt_part(0);
        }
    default:
        fail("unknown options step");
        return -1;
    }
}

/* The hook: run steps until one sets the next frame's pad. 1 the phase
 * passed, 0 continue. The recordings' pad reached the game three frames
 * after the row it was set on (SELECT at f39, frame state 2 at f42: decomp
 * CAPTURES_C10.md "OPTIONS"), the overlay's on the next frame: as the
 * BRANCH side runs do (br_frame), every pad the program sets (test input)
 * reaches the overlay two ticks later, so the closed-loop policies see
 * their taps take effect when the capture tool did and the states last the
 * recordings' rows. */
static int opt_frame(void)
{
    int r = NEXT;
    for (int guard = 0; r == NEXT && guard < 64; ++guard)
        r = opt_step();
    if (r == NEXT) {
        fail("the options program made no frame");
        return 0;
    }
    if (r == 2)
        return 1;
    if (r < 0 || !t.pad_on)
        return 0;
    O.applied = O.queued >= 2 ? O.queue[0] : (EmPadState){0};
    O.queue[0] = O.queue[1];
    O.queue[1] = t.pad;
    if (O.queued < 2) ++O.queued;
    em_input_set_gamepad(&O.applied);
    return 0;
}

static void br_begin(void)
{
    nav_reset();
    memset(&B, 0, sizeof B);
    for (size_t i = 0; i < sizeof k_br_programs / sizeof k_br_programs[0]; ++i)
        if (strcmp(k_br_programs[i].phase, k_phases[t.current].name) == 0)
            B.prog = k_br_programs[i].prog;
    if (!B.prog)
        fail("no BRANCH program for this phase");
}

/* The status block D_00810130 (0x60 bytes) or NULL. */
static const uint8_t *br_ui(void)
{
    return em_status_runtime_ui_block(em_area11_interaction_host_status());
}

/* route_capture in_control: the selector 3B8D 0, +1F0 0 and the status
 * state D_00810131 0 (the policies' predicate, as the recordings test it). */
static int br_ctl(void)
{
    const uint8_t *ui = br_ui();
    return em_scene_state()->spad3B8D == 0 && em_live_u8(player_states_actor(), 0x1F0) == 0 && (!ui || ui[1] == 0);
}

/* route_capture settle(frames): idle `frames`, then until in_control and
 * the record's clip +0x20C is 0 (600 frames). 1 done, 0 continue, -1. */
static int br_settle(int frames)
{
    pad_apply(0, 0, 0);
    ++t.nav_frames;
    if (t.nav_frames <= frames)
        return 0;
    if (br_ctl() && em_live_u16(player_states_actor(), 0x20C) == 0) {
        nav_reset();
        return 1;
    }
    if (t.nav_frames > frames + 600) {
        fail("control did not return (settle)");
        return -1;
    }
    return 0;
}

static int br_status_open(void)
{
    const uint8_t *ui = br_ui();
    return ui && ui[1] == 3;
}

static float br_bearing(float x, float z)
{
    return atan2f(x - g.pos[0], z - g.pos[2]);
}

/* route_capture face(yaw, tol): stick taps toward the body yaw, at most 40
 * frames (the caller settles 10 after it, as face() does). */
static int br_face(float yaw, float tol)
{
    float diff = fmodf(yaw - g.yaw + BR_PI, 2 * BR_PI);
    if (diff < 0)
        diff += 2 * BR_PI;
    diff -= BR_PI;
    if (fabsf(diff) <= tol || ++t.nav_frames > 40) {
        pad_apply(0, 0, 0);
        nav_reset();
        return 1;
    }
    nav_stick_toward(g.pos[0] + 100 * sinf(yaw), g.pos[2] + 100 * cosf(yaw), 0.6f);
    return 0;
}

static int br_next(void)
{
    pad_apply(0, 0, 0);
    const BrStep *st = &B.prog[B.pc];
    if (st->kind == BS_BEAT)
        fprintf(stderr, "level smoke: %s: beat %s at tick %u counter %u\n", k_phases[t.current].name, st->what,
                (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
    else if (st->what)
        fprintf(stderr, "level smoke: %s: done %s at tick %u counter %u (%.3f,%.4f,%.3f)\n",
                k_phases[t.current].name, st->what, (unsigned)em_scene_bindings_log_tick_next(),
                em_frame_counter(), g.pos[0], g.pos[1], g.pos[2]);
    ++B.pc;
    B.sub = B.ticks = B.tries = B.still = 0;
    nav_reset();
    t.saw[6] = 0;
    return 0;
}

static int br_timeout(int limit, const char *what)
{
    if (++B.ticks > limit) {
        char reason[160];
        snprintf(reason, sizeof reason, "%s: no progress in %d ticks", what, limit);
        fail(reason);
        return -1;
    }
    return 0;
}

/* A two-tick press of `buttons` (r.press(name, 2)): 1 when done. */
static int br_press(uint16_t buttons)
{
    if (B.ticks < 2) {
        pad_apply(buttons, 0, 0);
        ++B.ticks;
        return 0;
    }
    pad_apply(0, 0, 0);
    B.ticks = 0;
    return 1;
}

/* The box at `address` hit or out of its rest (route_capture br_box_broken:
 * +0x04 not 0 / 1 / 4, or the damage word +0x36 set). */
static int br_box_broken(uint32_t address)
{
    uint8_t image[EM_ACTOR_RECORD_SIZE];
    if (!em_scene_bindings_record_image(address, image))
        return 0;
    const uint8_t state = image[4];
    return (state != 0 && state != 1 && state != 4) || image[0x36] || image[0x37];
}

static int br_take(const BrStep *st)
{
    const int puid = (int)st->c;
    switch (B.sub) {
    case 0: /* face the item */
        if (br_face(br_bearing(st->a, st->b), 0.08f)) { B.sub = 1; B.ticks = 0; }
        return 0;
    case 1:
        if (br_settle(10) > 0) { B.sub = 2; B.ticks = 0; }
        return 0;
    case 2:
        if (br_press(EM_PAD_CROSS)) { B.sub = 3; B.ticks = 0; }
        return 0;
    case 3: /* the take starts when the use scan leaves control */
        pad_apply(0, 0, 0);
        if (!br_ctl()) {
            fprintf(stderr, "level smoke: %s: take left control at tick %u counter %u\n", k_phases[t.current].name,
                    (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
            B.sub = 5;
            B.ticks = 0;
        } else if (++B.ticks > 40) {
            if (++B.tries >= 4) { fail("Cross at the item started nothing"); return 0; }
            B.sub = 4;
            B.ticks = 0;
        }
        return 0;
    case 4:
        if (br_settle(10) > 0) { B.sub = 0; B.ticks = 0; }
        return 0;
    case 5:
        if (em_pickup_taken(0x0B00 | puid) || br_status_open()) { B.sub = 6; B.ticks = 0; return 0; }
        (void)br_timeout(900, "the take's taken bit or status");
        return 0;
    case 6:
        if (br_status_open()) { B.sub = 7; B.ticks = 0; return 0; }
        if (br_ctl()) { B.sub = 11; B.ticks = 0; return 0; }
        (void)br_timeout(900, "the take's status or control");
        return 0;
    case 7: { /* br_status_exit: browse (05 01), or the page record still */
        const uint8_t *ui = br_ui();
        if (ui && ui[4] == 5 && ui[5] == 1) { B.sub = 8; B.ticks = 0; return 0; }
        uint8_t key[0x28];
        memset(key, 0, sizeof key);
        if (ui) {
            memcpy(key, ui, 8);
            memcpy(key + 8, ui, 0x20);
        }
        B.still = memcmp(key, B.key, sizeof key) == 0 ? B.still + 1 : 0;
        memcpy(B.key, key, sizeof key);
        if (B.still >= 60) { B.sub = 8; B.ticks = 0; return 0; }
        (void)br_timeout(1500, "the take's status page");
        return 0;
    }
    case 8:
        pad_apply(0, 0, 0);
        if (++B.ticks >= 20) { B.sub = 9; B.ticks = 0; B.tries = 0; }
        return 0;
    case 9:
        if (br_press(EM_PAD_TRIANGLE)) {
            fprintf(stderr, "level smoke: %s: triangle at tick %u counter %u\n", k_phases[t.current].name,
                    (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
            B.sub = 10;
            B.ticks = 0;
        }
        return 0;
    case 10:
        pad_apply(0, 0, 0);
        if (!br_status_open()) { B.sub = 12; B.ticks = 0; return 0; }
        if (++B.ticks > 40) {
            if (++B.tries >= 8) { fail("the take's status did not close"); return 0; }
            B.sub = 9;
            B.ticks = 0;
        }
        return 0;
    case 12:
        pad_apply(0, 0, 0);
        if (br_ctl()) { B.sub = 11; B.ticks = 0; return 0; }
        (void)br_timeout(900, "control after the take");
        return 0;
    default: /* 11: settle 30, then the taken bit */
        if (br_settle(30) <= 0) return 0;
        if (!em_pickup_taken(0x0B00 | puid)) { fail("the item was not taken (no taken bit)"); return 0; }
        return br_next();
    }
}

static int br_mode_ladder(uint8_t m) { return m == 0x15 || m == 0x16 || m == 0x17 || m == 0x18; }

/* route_capture br_ladder_up / br_ladder_down (with br_frame's pad
 * latency the climb clip changes four rows after the first 0x17 row, as in
 * br_05 / br_09 / br_11 and route 10). */
static int br_ladder(const BrStep *st, int down)
{
    const uint8_t m = em_live_u8(player_states_actor(), 0x1F0);
    switch (B.sub) {
    case 0:
        if (br_face(st->a, 0.08f)) { B.sub = 1; B.ticks = 0; }
        return 0;
    case 1:
        if (br_settle(10) > 0) { B.sub = 2; B.ticks = 0; }
        return 0;
    case 2:
        if (br_press(EM_PAD_CROSS)) { B.sub = 3; B.ticks = 0; }
        return 0;
    case 3:
        pad_apply(0, 0, 0);
        if (down ? br_mode_ladder(m) : (m == 0x15 || m == 0x16 || m == 0x17)) {
            fprintf(stderr, "level smoke: %s: ladder grab 0x%X at tick %u counter %u\n", k_phases[t.current].name, m,
                    (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
            /* down: the stick from the grab's row on (route_capture sets it
             * on that row). */
            B.sub = down ? 8 : 5;
            B.ticks = 0;
            B.y0 = g.pos[1];
            if (down) pad_apply(0, 0, 1.0f);
        } else if (++B.ticks > 60) {
            if (++B.tries >= 3) { fail(down ? "the ladder was not grabbed from the top" : "the ladder was not grabbed"); return 0; }
            B.sub = 4;
            B.ticks = 0;
        }
        return 0;
    case 4:
        if (br_settle(10) > 0) { B.sub = 0; B.ticks = 0; }
        return 0;
    case 5: /* up: until the climb (0x17), then the stick from its row on */
        pad_apply(0, 0, 0);
        if (m == 0x17) { B.sub = 8; B.ticks = 0; pad_apply(0, 0, -1.0f); return 0; }
        (void)br_timeout(120, "the ladder's climb state 0x17");
        return 0;
    case 8:
        if (down ? (!br_mode_ladder(m) && g.pos[1] < B.y0 - 20.0f) : !br_mode_ladder(m)) {
            pad_apply(0, 0, 0);
            fprintf(stderr, "level smoke: %s: ladder off at tick %u counter %u y %.4f\n", k_phases[t.current].name,
                    (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter(), g.pos[1]);
            B.sub = 9;
            B.ticks = 0;
            return 0;
        }
        pad_apply(0, 0, down ? 1.0f : -1.0f);
        (void)br_timeout(down ? 1500 : 1200, "the ladder's hand-back");
        return 0;
    default:
        if (br_settle(30) > 0) return br_next();
        return 0;
    }
}

static int br_climb(const BrStep *st)
{
    const uint8_t m = em_live_u8(player_states_actor(), 0x1F0);
    switch (B.sub) {
    case 0:
        if (br_face(st->a, 0.08f)) { B.sub = 1; B.ticks = 0; }
        return 0;
    case 1:
        if (br_settle(10) > 0) { B.sub = 2; B.ticks = 0; }
        return 0;
    case 2:
        if (br_press(EM_PAD_CROSS)) { B.sub = 3; B.ticks = 0; }
        return 0;
    case 3:
        pad_apply(0, 0, 0);
        if (m == 8) { B.sub = 5; B.ticks = 0; return 0; }
        if (++B.ticks > 40) {
            if (++B.tries >= 5) { fail("no ledge climb"); return 0; }
            B.sub = 4;
            B.ticks = 0;
        }
        return 0;
    case 4:
        if (br_settle(10) > 0) { B.sub = 0; B.ticks = 0; }
        return 0;
    case 5:
        pad_apply(0, 0, 0);
        if (br_ctl()) { B.sub = 6; B.ticks = 0; return 0; }
        (void)br_timeout(400, "control after the ledge climb");
        return 0;
    default:
        if (br_settle(10) > 0) return br_next();
        return 0;
    }
}

static int br_melee(const BrStep *st)
{
    const uint32_t box = (uint32_t)st->a;
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t image[EM_ACTOR_RECORD_SIZE];
    float bx = 0, bz = 0;
    if (em_scene_bindings_record_image(box, image)) {
        memcpy(&bx, image + 0xB0, 4);
        memcpy(&bz, image + 0xB8, 4);
    }
    switch (B.sub) {
    case 0:
        if (br_face(br_bearing(bx, bz), 0.06f)) { B.sub = 1; B.ticks = 0; }
        return 0;
    case 1:
        if (br_settle(10) > 0) { B.sub = 2; B.ticks = 0; }
        return 0;
    case 2:
        if (br_press(EM_PAD_CIRCLE)) { B.sub = 3; B.ticks = 0; }
        return 0;
    case 3:
        pad_apply(0, 0, 0);
        if (br_box_broken(box)) {
            fprintf(stderr, "level smoke: %s: box %06X hit at tick %u counter %u\n", k_phases[t.current].name,
                    (unsigned)box, (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
            B.sub = 5;
            B.ticks = 0;
            return 0;
        }
        if (++B.ticks > 60) {
            if (++B.tries >= 6) { fail("the box was not hit"); return 0; }
            B.sub = 4;
            B.ticks = 0;
        }
        return 0;
    case 4:
        pad_apply(0, 0, 0);
        if (br_ctl() && em_live_u8(a, 5) == 0) { B.sub = 0; B.ticks = 0; return 0; }
        (void)br_timeout(300, "control after a miss");
        return 0;
    case 5:
        pad_apply(0, 0, 0);
        if (br_ctl() && em_live_u8(a, 5) == 0) { B.sub = 6; B.ticks = 0; return 0; }
        (void)br_timeout(400, "control after the hit");
        return 0;
    default:
        pad_apply(0, 0, 0);
        if (++B.ticks >= 120) return br_next();
        return 0;
    }
}

/* The recordings' pad reached the game three frames after the row it was
 * set on, the overlay's on the next frame (the ladder climbs' stick, the
 * presses' scans: LEVEL_SMOKE.md "The BRANCH side runs"): every pad the
 * program sets (test input) reaches the overlay two ticks later, so the
 * closed-loop policies see their inputs take effect as the capture tool
 * did. */
static int br_frame_inner(void);

static int br_frame(void)
{
    const int r = br_frame_inner();
    if (!t.pad_on || r != 0)
        return r;
    B.applied = B.queued >= 2 ? B.queue[0] : (EmPadState){0};
    B.queue[0] = B.queue[1];
    B.queue[1] = t.pad;
    if (B.queued < 2) ++B.queued;
    em_input_set_gamepad(&B.applied);
    return r;
}

static int br_frame_inner(void)
{
    if (!B.prog)
        return 0;
    const BrStep *st = &B.prog[B.pc];
    const EmPlayerLiveActor *a = player_states_actor();
    switch (st->kind) {
    case BS_END:
        fprintf(stderr, "level smoke: %s: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", k_phases[t.current].name,
                g.pos[0], g.pos[1], g.pos[2], g.yaw);
        return 1;
    case BS_BEAT:
        return br_next();
    case BS_IDLE:
        pad_apply(0, 0, 0);
        if (++B.ticks >= (int)st->a) return br_next();
        return 0;
    case BS_WALK: {
        int r = walk_path(st->path, st->n, st->a);
        if (r > 0) return br_next();
        return 0;
    }
    case BS_GOTO: {
        int r = nav_goto(st->a, st->b, st->c, st->d, 1);
        if (r > 0) return br_next();
        return 0;
    }
    case BS_SETTLE:
        if (br_settle((int)st->a) > 0) return br_next();
        return 0;
    case BS_FACE:
        if (br_face(st->a, st->b)) return br_next();
        return 0;
    case BS_TAKE:
        return br_take(st);
    case BS_LADDER_UP:
        return br_ladder(st, 0);
    case BS_LADDER_DOWN:
        return br_ladder(st, 1);
    case BS_CLIMB:
        return br_climb(st);
    case BS_RUN_OFF: {
        /* br_05: until x <= c in control; br_11: until y < d in control
         * (route_capture in_control: the selector 0, +1F0 0, no status). */
        const int off = st->d != 0 ? g.pos[1] < st->d : g.pos[0] <= st->c;
        if (off && br_ctl())
            return br_next();
        const uint8_t m = em_live_u8(a, 0x1F0);
        if (m == 0 || m == 1) nav_stick_toward(st->a, st->b, 1.0f);
        else pad_apply(0, 0, 0);
        (void)br_timeout(200, st->what);
        return 0;
    }
    case BS_MELEE:
        return br_melee(st);
    case BS_TERMINAL: {
        const uint8_t *floor = em_scene_progress_at(em_scene_state(), 0x0081083Au, 1);
        switch (B.sub) {
        case 0:
            B.down0 = floor ? *floor : 0;
            B.sub = 1;
            /* fall through */
        case 1:
            if (br_press(EM_PAD_CROSS)) { B.sub = 2; B.ticks = 0; }
            return 0;
        case 2:
            pad_apply(0, 0, 0);
            if (!br_ctl()) { B.sub = 4; B.ticks = 0; return 0; }
            if (++B.ticks > 40) {
                if (++B.tries >= 4) { fail("the terminal did not start"); return 0; }
                B.sub = 3;
                B.ticks = 0;
            }
            return 0;
        case 3:
            if (br_settle(10) > 0) { B.sub = 1; B.ticks = 0; }
            return 0;
        case 4:
            pad_apply(0, 0, 0);
            if (floor && *floor != B.down0) { B.sub = 5; B.ticks = 0; return 0; }
            (void)br_timeout(1500, "D_0081083A's toggle");
            return 0;
        case 5:
            pad_apply(0, 0, 0);
            if (br_ctl()) { B.sub = 6; B.ticks = 0; return 0; }
            (void)br_timeout(1500, "control after the ride up");
            return 0;
        default:
            if (br_settle(30) <= 0) return 0;
            if (!floor || *floor != 0) { fail("D_0081083A did not come back to 0"); return 0; }
            return br_next();
        }
    }
    case BS_PANEL_DECLINE:
        switch (B.sub) {
        case 0:
            if (br_press(EM_PAD_CROSS)) { B.sub = 1; B.ticks = 0; }
            return 0;
        case 1:
            pad_apply(0, 0, 0);
            if (!br_ctl()) { B.sub = 2; B.ticks = 0; return 0; }
            (void)br_timeout(60, "Cross at the panel");
            return 0;
        case 2: {
            const uint8_t *ui = br_ui();
            if (br_status_open() && ui[4] == 5 && ui[5] == 4) { B.sub = 3; B.ticks = 0; return 0; }
            (void)br_timeout(900, "the BATTERY prompt");
            return 0;
        }
        case 3:
            pad_apply(0, 0, 0);
            if (++B.ticks >= 30) { B.sub = 4; B.ticks = 0; }
            return 0;
        case 4:
            if (br_press(EM_PAD_CROSS)) {
                fprintf(stderr, "level smoke: %s: cross on No at tick %u counter %u\n", k_phases[t.current].name,
                        (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
                B.sub = 5;
                B.ticks = 0;
            }
            return 0;
        case 5:
            pad_apply(0, 0, 0);
            if (++B.ticks >= 60) { B.sub = 6; B.ticks = 0; B.tries = 0; }
            return 0;
        case 6:
            if (!br_status_open()) { B.sub = 8; B.ticks = 0; return 0; }
            if (br_press(EM_PAD_TRIANGLE)) {
                fprintf(stderr, "level smoke: %s: triangle at tick %u counter %u\n", k_phases[t.current].name,
                        (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
                B.sub = 7;
                B.ticks = 0;
            }
            return 0;
        case 7:
            pad_apply(0, 0, 0);
            if (!br_status_open()) { B.sub = 8; B.ticks = 0; return 0; }
            if (++B.ticks > 40) {
                if (++B.tries >= 8) { fail("the status did not close"); return 0; }
                B.sub = 6;
                B.ticks = 0;
            }
            return 0;
        case 8:
            pad_apply(0, 0, 0);
            if (br_ctl()) { B.sub = 9; B.ticks = 0; return 0; }
            (void)br_timeout(1500, "control after the decline");
            return 0;
        default:
            if (br_settle(30) <= 0) return 0;
            if (power_bit()) { fail("the power came on"); return 0; }
            return br_next();
        }
    case BS_ROGER_TALK: {
        uint32_t record;
        uint8_t head[16], block[16];
        float rp[3] = {0, 0, 0};
        if (!em_area11_roger_state(&record, head, rp, block)) { fail("no Roger node"); return 0; }
        switch (B.sub) {
        case 0:
            if (br_ctl()) { B.sub = 1; B.ticks = 0; return 0; }
            pad_apply(0, 0, 0);
            (void)br_timeout(3000, "control before Roger's talk");
            return 0;
        case 1:
            if (br_face(br_bearing(rp[0], rp[2]), 0.06f)) { B.sub = 2; B.ticks = 0; }
            return 0;
        case 2:
            if (br_settle(10) > 0) { B.sub = 3; B.ticks = 0; }
            return 0;
        case 3:
            if (br_press(EM_PAD_CROSS)) { B.sub = 4; B.ticks = 0; }
            return 0;
        case 4:
            pad_apply(0, 0, 0);
            if (!br_ctl()) {
                fprintf(stderr, "level smoke: %s: talk left control at tick %u counter %u\n",
                        k_phases[t.current].name, (unsigned)em_scene_bindings_log_tick_next(), em_frame_counter());
                B.sub = 7;
                B.ticks = 0;
                return 0;
            }
            if (++B.ticks > 40) {
                if (++B.tries >= 4) { fail("Roger's talk did not start"); return 0; }
                /* route_capture: a step 30% of the way toward Roger. */
                B.tx = g.pos[0] + (rp[0] - g.pos[0]) * 0.3f;
                B.tz = g.pos[2] + (rp[2] - g.pos[2]) * 0.3f;
                B.sub = 5;
                B.ticks = 0;
            }
            return 0;
        case 5:
            if (nav_goto(B.tx, B.tz, 0.5f, 0.4f, 1) > 0) { B.sub = 6; B.ticks = 0; }
            return 0;
        case 6:
            if (br_settle(5) > 0) { B.sub = 1; B.ticks = 0; }
            return 0;
        case 7:
            pad_apply(0, 0, 0);
            if (br_ctl()) { B.sub = 8; B.ticks = 0; return 0; }
            (void)br_timeout(6000, "control after Roger's talk");
            return 0;
        default:
            if (br_settle(60) > 0) return br_next();
            return 0;
        }
    }
    default:
        fail("unknown BRANCH step");
        return 0;
    }
}

static void crevice_jump_begin(void) { nav_reset(); }

static int crevice_jump_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t mode = em_live_u8(a, 0x1F0);
    switch (t.step) {
    case 0: NAV_STEP(walk_path(k_jump_path, 2, 1.0f));
    case 1: NAV_STEP(nav_settle(5));
    case 2: NAV_STEP(nav_face(3.14159265f));
    case 3: NAV_STEP(nav_settle(10));
    case 4:
        if (g.pos[2] > 249.5f) {
            nav_stick_toward(477.0f, 150.0f, 1.0f);
            if (++t.nav_frames > JUMP_LIMIT)
                fail("the run-up did not reach the plateau's edge");
            return 0;
        }
        nav_reset();
        ++t.step;
        /* fall through: Cross with the stick kept */
    case 5:
        pad_apply(EM_PAD_CROSS, t.pad.lx, t.pad.ly);
        if (++t.nav_frames >= 2) {
            nav_reset();
            ++t.step;
        }
        return 0;
    case 6:
        pad_apply(0, t.pad.lx, t.pad.ly);
        if (mode == 0x0C) {
            t.saw[0] = 1;
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 10)
            fail("Cross at the edge did not enter the running jump (+1F0 0x0C)");
        return 0;
    case 7:
        pad_apply(0, t.pad.lx, t.pad.ly);
        if (mode == 0x0F)
            t.saw[1] = 1;
        if (mode == 0x0C || mode == 0x0F) {
            if (++t.nav_frames > JUMP_LIMIT)
                fail("the running jump did not end");
            return 0;
        }
        pad_apply(0, 0, 0);
        nav_reset();
        ++t.step;
        return 0;
    case 8: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        if (!t.saw[1] || g.pos[2] > 210.0f || fabsf(g.pos[1] - 269.84f) > 0.01f) {
            fprintf(stderr, "level smoke: crevice_jump: landed %u at (%.3f, %.5f, %.3f)\n", t.saw[1], g.pos[0],
                    g.pos[1], g.pos[2]);
            fail("the running jump did not land on the north block (route f277: y 269.84, z 188.1)");
            return 0;
        }
        fprintf(stderr, "level smoke: crevice_jump: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0],
                g.pos[1], g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* --------------------------------------------------- east_tower_climb
 *
 * Route beat 13 up to director beat 2 (route_capture.py beat_east_tower):
 * the high ledge climb from the route's stance (444.246, 179.776) at the
 * north end of the east tower's east face, facing -1.6104 (f435; clips
 * 0x70 / 0x79 / 0x8C) onto the tower top at y 289.75 (f530). The approach
 * is route_capture's goto(445, 178) at 0.6 stick. tools/test_level_smoke.py
 * check_east_tower_climb compares the climb with the capture. */
static const float k_tower_stance[3] = {444.246f, 179.776f, -1.6104f};

static void east_tower_climb_begin(void) { nav_reset(); }

static int east_tower_climb_frame(void)
{
    if (t.step == 0) NAV_STEP(nav_goto(445.0f, 178.0f, 0.7f, 0.6f, 1));
    if (t.step == 1) NAV_STEP(nav_settle(5));
    if (t.step >= 2 && t.step <= 8)
        NAV_STEP(stance_climb(t.step - 2, k_tower_stance, 1, 0, 1, 289.75f,
                              "the high ledge climb did not end on the east tower top (route f530: y 289.75)"));
    fprintf(stderr, "level smoke: east_tower_climb: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0],
            g.pos[1], g.pos[2], g.yaw);
    return 1;
}

/* ---------------------------------------------------------------- roger
 *
 * Route beat 14 (route_capture.py beat_roger_encounter): goto(436, 190) at
 * 0.6 stick (tolerance 0.8, a stop counts), settle 5, face -pi/2 (and settle
 * 10, as face() does), then run toward (300, 190) at full stick until
 * x <= 411.5 (f238), Cross for two frames with the stick kept; the stick
 * stays on until +1F0 = 0x0C (the running jump, f241) and on until Roger's
 * script block names 0x8283D0 (Roger 008237E0's ordinary branch: the
 * player crossed into his quad 0x82AB80 in mid-air, f283), then neutral
 * until the scripted frame opens (3B8D != 0) and until control is back
 * (3B8D = 0, +1F0 = 0; f1758), then settle 60. In process: the script ran,
 * the counter D_008107D8 holds bit 0 (0x823AB0) and the player stands at
 * the script's 01/9 placement (338, 289.75, 192), heading -2.531.
 * tools/test_level_smoke.py check_roger compares the encounter with the
 * capture. */
enum { ROGER_RUN_LIMIT = 200, ROGER_ENCOUNTER_LIMIT = 6000 };

static uint32_t roger_script_pc(void)
{
    uint32_t record;
    uint8_t header[16], block[16];
    float position[3];
    if (!em_area11_roger_state(&record, header, position, block))
        return 0;
    uint32_t pc;
    memcpy(&pc, block + 8, 4);
    return pc;
}

static void roger_begin(void) { nav_reset(); }

static int roger_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t mode = em_live_u8(a, 0x1F0);
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(436.0f, 190.0f, 0.8f, 0.6f, 1));
    case 1: NAV_STEP(nav_settle(5));
    case 2: NAV_STEP(nav_face(-1.5707963f));
    case 3: NAV_STEP(nav_settle(10));
    case 4:
        if (g.pos[0] > 411.5f) {
            nav_stick_toward(300.0f, 190.0f, 1.0f);
            if (++t.nav_frames > ROGER_RUN_LIMIT)
                fail("the run-up did not reach x 411.5");
            return 0;
        }
        nav_reset();
        ++t.step;
        /* fall through: Cross with the stick kept */
    case 5:
        pad_apply(EM_PAD_CROSS, t.pad.lx, t.pad.ly);
        if (++t.nav_frames >= 2) {
            nav_reset();
            ++t.step;
        }
        return 0;
    case 6:
        pad_apply(0, t.pad.lx, t.pad.ly);
        if (mode == 0x0C) {
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 10)
            fail("Cross at the edge did not enter the running jump (+1F0 0x0C)");
        return 0;
    case 7:
        pad_apply(0, t.pad.lx, t.pad.ly);
        if (roger_script_pc() == 0x008283D0u) {
            pad_apply(0, 0, 0);
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 120)
            fail("Roger's quad 0x82AB80 did not start script 0x8283D0");
        return 0;
    case 8:
        pad_apply(0, 0, 0);
        if (em_scene_state()->spad3B8D != 0) {
            t.saw[0] = 1;
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 60)
            fail("the encounter did not open its scripted frame (3B8D)");
        return 0;
    case 9:
        pad_apply(0, 0, 0);
        if (em_scene_state()->spad3B8D == 0 && mode == 0 && in_control()) {
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > ROGER_ENCOUNTER_LIMIT)
            fail("control did not return after the encounter");
        return 0;
    case 10: {
        int r = nav_settle(60);
        if (r <= 0)
            return 0;
        const uint8_t *story = em_scene_progress_at(em_scene_state(), 0x008107D8u, 1);
        if (!story || !(*story & 1) || fabsf(g.pos[0] - 338.0f) > 0.01f || fabsf(g.pos[2] - 192.0f) > 0.01f ||
            fabsf(g.pos[1] - 289.75f) > 0.01f || fabsf(g.yaw - -2.5307274f) > 0.001f) {
            fprintf(stderr, "level smoke: roger: story %u at (%.3f, %.5f, %.3f) yaw %.5f\n", story ? *story : 0xEEu,
                    g.pos[0], g.pos[1], g.pos[2], g.yaw);
            fail("the encounter did not end at the script's placement with D_008107D8 bit 0 (route f1758)");
            return 0;
        }
        fprintf(stderr, "level smoke: roger: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f story=%u\n", g.pos[0],
                g.pos[1], g.pos[2], g.yaw, *story);
        return 1;
    }
    default:
        return 0;
    }
}

/* ----------------------------------------------------------------- exit
 *
 * The exit capture (decomp CAPTURES_C10.md EXIT; docs/FIRST_LEVEL_EXIT.md)
 * from route beat 14's end, where the roger phase ends. exit_00 idles until
 * its pin, walks toward fan r2 00827630 (record [2], 0x7A7690), stops
 * outside its hit band, waits for its slow window and walks into the exit
 * box (Z < 156). The fan's cycle runs from the area load, so its phase at
 * beat 14's end is the time since the load (host speed in the port): the
 * phase waits, with a neutral pad, for the cycle point the capture has at
 * its row 31 (fan r2 enters phase 2), and from there replays exit_00's pad
 * (its `inputs`, frame-indexed: the original's state first shows an entry
 * of frame f in its row f + 3, as in the AIM replays, so the port's
 * after-frame of the tick aligned to capture frame c sets the entry of
 * frame c - 2, which the next tick reads). From the crossing on
 * nothing takes input: the departure script, its movie (the frontend's
 * test skip holds START from 2 s, as for the intro movie; the game sees one
 * frame either way), the area change, the AREA01 load and the arrival. The
 * phase ends on the frame before the arrival's 0x1AE040 state-0 rebuild
 * (slot 0 +9 = 1, +B = 0 in area 1); the finish's one more frame is that
 * rebuild, the capture's first frame of control in AREA01 (exit_01 row
 * 306), and the run quits before the first AREA01 world frame (AREA01
 * gameplay is level 2). tools/test_level_smoke.py check_exit compares the
 * rows. */
enum { EXIT_FAN_R2 = 0x007A7690u, EXIT_ALIGN_ROW = 31, EXIT_CROSSING_ROW = 344, EXIT_FAN_WAIT = 400, EXIT_ARRIVAL_LIMIT = 3000 };
typedef struct {
    int f;
    uint8_t lx, ly;
} ExitInput;
static const ExitInput k_exit_inputs[] = { /* exit_00_departure `inputs` (no buttons) */
    {39, 146, 2}, {55, 145, 2}, {58, 144, 2}, {61, 143, 2}, {63, 142, 2}, {65, 141, 2}, {66, 140, 2},
    {67, 139, 1}, {68, 138, 1}, {69, 137, 1}, {70, 135, 1}, {71, 133, 1}, {72, 132, 1}, {73, 131, 1},
    {74, 130, 1}, {75, 129, 1}, {76, 127, 1}, {77, 124, 1}, {78, 121, 1}, {78, 127, 127}, {78, 140, 66},
    {79, 141, 66}, {80, 144, 66}, {81, 146, 67}, {82, 147, 67}, {84, 146, 67}, {85, 143, 66}, {86, 135, 65},
    {86, 127, 127}, {304, 193, 19}, {322, 192, 19}, {324, 192, 18}, {327, 191, 18}, {328, 191, 17},
    {329, 190, 17}, {330, 189, 17}, {331, 189, 16}, {332, 188, 16}, {333, 187, 15}, {334, 186, 15},
    {335, 184, 14}, {336, 183, 13}, {337, 181, 13}, {338, 179, 12}, {339, 176, 11}, {340, 174, 10},
    {341, 171, 8}, {342, 168, 7}, {343, 164, 6}, {344, 127, 127}};
enum { EXIT_INPUTS = (int)(sizeof k_exit_inputs / sizeof k_exit_inputs[0]) };

static void exit_begin(void)
{
    nav_reset();
    t.frames = 0;
    pad_apply(0, 0, 0);
}

/* The capture's pad at frame `frame` (the last entry at or before it; the
 * neutral pad before the first). */
static void exit_apply(int frame)
{
    const ExitInput *in = NULL;
    for (int i = 0; i < EXIT_INPUTS && k_exit_inputs[i].f <= frame; ++i) in = &k_exit_inputs[i];
    if (!in) {
        pad_apply(0, 0, 0);
        return;
    }
    pad_apply(0, (in->lx - 128) / 128.0f, (in->ly - 128) / 128.0f);
}

static int exit_frame(void)
{
    uint8_t phase = 0xFF;
    int16_t timer = 0;
    switch (t.step) {
    case 0:
        /* Fan r2 leaves phase 2 first, so the next entry into it is a whole
         * cycle point (the capture's row 31: phase 2, timer 0). */
        pad_apply(0, 0, 0);
        if (!em_scene_bindings_fan_cycle(EXIT_FAN_R2, &phase, &timer)) {
            fail("fan r2 (0x7A7690) is not a live fan node");
            return 0;
        }
        if (phase != 2) {
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > EXIT_FAN_WAIT) fail("fan r2 did not leave phase 2");
        return 0;
    case 1:
        pad_apply(0, 0, 0);
        if (!em_scene_bindings_fan_cycle(EXIT_FAN_R2, &phase, &timer)) {
            fail("fan r2 (0x7A7690) is not a live fan node");
            return 0;
        }
        if (phase == 2) {
            fprintf(stderr, "level smoke: exit: aligned counter=%u (fan r2 phase 2, timer %d = exit_00 row %d)\n",
                    em_frame_counter(), (int)timer, EXIT_ALIGN_ROW);
            t.frames = EXIT_ALIGN_ROW;
            nav_reset();
            ++t.step;
            exit_apply(t.frames - 2);
            return 0;
        }
        if (++t.nav_frames > EXIT_FAN_WAIT) fail("fan r2 did not enter phase 2 within a cycle");
        return 0;
    case 2: {
        /* This tick is capture frame t.frames. */
        ++t.frames;
        if (t.frames == EXIT_CROSSING_ROW) {
            const uint8_t *story = em_scene_progress_at(em_scene_state(), 0x008107D8u, 1);
            if (!story || *story != 0x81) {
                fail("the crossing did not set D_008107D8 = 0x81 in exit_00's frame 344");
                return 0;
            }
        }
        exit_apply(t.frames - 2);
        if (t.frames < k_exit_inputs[EXIT_INPUTS - 1].f + 2)
            return 0;
        nav_reset();
        ++t.step;
        return 0;
    }
    case 3: {
        /* Automatic from here: the departure, the movie, the area change, the
         * load. The frame before the arrival's rebuild: area 1, slot 0 at
         * the frame machine (+9 = 1) with +B = 0. Watched from the task's
         * end (em_level_smoke_test_task_end), which every frame reaches; the
         * after-frame hook runs only in world frames. */
        pad_apply(0, 0, 0);
        const EmSceneState *s = em_scene_state();
        if (s->d810700 == 1 && task_byte(EM_SCENE_TASK_09) == 1 && task_byte(EM_SCENE_TASK_0B) == 0) {
            fprintf(stderr, "level smoke: exit: PASS area %02X %02X %02X; the next frame is the AREA01 "
                    "arrival's rebuild (exit_01 row 306)\n", s->d810700, s->d810701, s->d810702);
            return 1;
        }
        if (++t.nav_frames > EXIT_ARRIVAL_LIMIT) fail("the AREA01 arrival did not come");
        return 0;
    }
    default:
        return 0;
    }
}

/* ---------------------------------------------------------- a01_arrival
 *
 * Opt-in continuation from exit, with neutral pad only. Route beat 15
 * f741 is the rebuild and f742..801 are its next 60 world frames. No
 * snapshot is imported into the game and no fault is bypassed. The pool
 * witness is taken at the rebuild for check_exit; this mid-frame tail's
 * fade/message/stream fields are replaced by the next tick's start sample
 * in that checker, because the remaining main-loop steps have not run. */
enum { A01_ARRIVAL_WORLD_TICKS = 60, A01_ARRIVAL_PROBE_TICKS_MAX = 600 };

/* EM_A01_ARRIVAL_TICKS=N (60..600) lengthens the idle run (diagnostic): a
 * fault-free run past f801, which route 15 does not record, so the extra
 * ticks prove no behaviour. */
static int a01_arrival_ticks(void)
{
    const char *n = getenv("EM_A01_ARRIVAL_TICKS");
    if (!n) return A01_ARRIVAL_WORLD_TICKS;
    const long v = strtol(n, NULL, 10);
    return v >= A01_ARRIVAL_WORLD_TICKS && v <= A01_ARRIVAL_PROBE_TICKS_MAX ? (int)v : A01_ARRIVAL_WORLD_TICKS;
}

static void a01_arrival_begin(void)
{
    t.frames = 0;
    pad_apply(0, 0, 0);
}

static int a01_arrival_frame(void)
{
    const EmSceneState *s = em_scene_state();
    pad_apply(0, 0, 0);
    if (s->d810700 != 1 || s->d810701 != 0 || s->d810702 != 4 ||
        task_byte(EM_SCENE_TASK_09) != 1 || task_byte(EM_SCENE_TASK_0B) != 1 || s->spad3B8D != 0) {
        fail("AREA01 arrival idle left area 01/00 entry 4 or its gameplay state");
        return 0;
    }
    if (!t.frames) {
        fprintf(stderr, "level smoke: a01_arrival: aligned counter=%u (route 15 row 741; rebuild)\n",
                em_frame_counter());
        em_scene_bindings_log_request_tail();
        em_scene_bindings_log_tail();
    }
    if (t.frames++ < a01_arrival_ticks())
        return 0;
    fprintf(stderr, "level smoke: a01_arrival: PASS world_ticks=%d counter=%u (capture check required)\n",
            a01_arrival_ticks(), em_frame_counter());
    return 1;
}

/* AREA01 pad scripts are test input exported into ignored build files.
 * No captured state is loaded. Inputs take effect three recorded rows
 * after the command; the native pad reaches the next task tick, so the
 * driver submits each recorded command two rows later (the BRANCH rule). */
#include "game/em_area01_exploration_test.h"

enum { A01_PAD_MAX = 2048 };
static AimStick s_a01_pad[A01_PAD_MAX];
static unsigned s_a01_pad_count;
static int s_a01_last_frame;

static void a01_route_begin(void)
{
    pad_apply(0,0,0);
#ifdef EM_AREA01_EXPLORATION_TEST_H
    if (a01_explore_begin()) return;
#endif
    const char *dir=getenv("EM_LEVEL2_PAD_DIR");
    char path[1024];
    int n=dir ? snprintf(path,sizeof path,"%s/%s.pad",dir,k_phases[t.current].name) : -1;
    FILE *f=n>0 && (size_t)n<sizeof path ? fopen(path,"r") : NULL;
    if(!f) { fail("AREA01 route pad script missing (tools/test_level_smoke_area01.py --prepare)");return; }
    unsigned version, gap;
    s_a01_pad_count=0;
    if(fscanf(f,"EMA1 %u %d %u",&version,&s_a01_last_frame,&gap)!=3 ||
       version!=1 || s_a01_last_frame<1 || s_a01_last_frame>20000 || !gap || gap>120) {
        fclose(f);fail("invalid AREA01 pad-script header");return;
    }
    int frame, fields;unsigned buttons,lx,ly;
    while((fields=fscanf(f,"%d %x %u %u",&frame,&buttons,&lx,&ly))==4) {
        if(s_a01_pad_count==A01_PAD_MAX || frame<0 || frame>s_a01_last_frame ||
           buttons>65535 || lx>255 || ly>255 ||
           (s_a01_pad_count && frame<s_a01_pad[s_a01_pad_count-1].f)) {
            fclose(f);fail("invalid AREA01 pad-script entry");return;
        }
        s_a01_pad[s_a01_pad_count++]=(AimStick){frame,(uint16_t)buttons,(uint8_t)lx,(uint8_t)ly};
    }
    int valid=fields==EOF && feof(f) && s_a01_pad_count;
    fclose(f);
    if(!valid) { fail("truncated or malformed AREA01 pad script");return; }
    t.frames=-(int)gap;
}
static int a01_route_frame(void)
{
#ifdef EM_AREA01_EXPLORATION_TEST_H
    if (a01_explore.active) return a01_explore_frame();
#endif
    ++t.frames;
    if(t.frames<0)return 0;
    if(!t.frames)
        fprintf(stderr,"level smoke: %s: aligned counter=%u (recorded row 0)\n",
                k_phases[t.current].name,em_frame_counter());
    if(!strcmp(k_phases[t.current].name,"a01_07")) {
        const EmSceneState *s=em_scene_state();
        if(s->d810700==0 && s->d810701==0 && s->d810702==0 &&
           task_byte(EM_SCENE_TASK_08)==3 && task_byte(EM_SCENE_TASK_09)==1 &&
           task_byte(EM_SCENE_TASK_0A)==0 && task_byte(EM_SCENE_TASK_0B)==0) {
            pad_apply(0,0,0);
            fprintf(stderr,"level smoke: a01_07: boundary counter=%u (capture row 529; AREA00 arrival state 0, before rebuild)\n",em_frame_counter());
            fprintf(stderr,"level smoke: a01_07: PASS frames=%d (gameplay and loader capture check required; level 3 not run)\n",t.frames);
            return 1;
        }
        if(t.frames>s_a01_last_frame+120) {
            fail("AREA00 arrival state 0 did not follow the AREA01 exit");return 0;
        }
    } else if(t.frames>=s_a01_last_frame) {
        pad_apply(0,0,0);
        fprintf(stderr,"level smoke: %s: PASS frames=%d (capture check required)\n",
                k_phases[t.current].name,s_a01_last_frame);
        return 1;
    }
    const AimStick *input=NULL;
    for(unsigned i=0;i<s_a01_pad_count && s_a01_pad[i].f<=t.frames-2;++i)input=&s_a01_pad[i];
    if(input)pad_apply(input->buttons,(input->lx-128)/128.0f,(input->ly-128)/128.0f);
    return 0;
}

/* ------------------------------------------------------------- driver */

void em_level_smoke_test_begin(void)
{
    memset(&t, 0, sizeof t);
    const char *value = getenv("EM_STARTUP_TEST");
    t.active = value && strcmp(value, "newgame-level") == 0;
    if (!t.active)
        return;
    /* The whole route (EM_TEST_FULL's empty endpoint) ends with the AREA01
     * arrival idle, a01_arrival; the AREA01 route beats after it are
     * opt-in. */
    t.until = 0;
    for (int i = 0; i < PHASE_COUNT; ++i)
        if (strcmp(k_phases[i].name, "a01_arrival") == 0)
            t.until = i;
    const char *until = getenv("EM_LEVEL_SMOKE_UNTIL");
    if (until && until[0]) {
        t.until = -1;
        for (int i = 0; i < PHASE_COUNT; ++i)
            if (strcmp(until, k_phases[i].name) == 0)
                t.until = i;
        if (t.until < 0) {
            fprintf(stderr, "level smoke: EM_LEVEL_SMOKE_UNTIL=%s is not a phase; phases:", until);
            for (int i = 0; i < PHASE_COUNT; ++i)
                fprintf(stderr, " %s", k_phases[i].name);
            fputc('\n', stderr);
            t.current = PHASE_COUNT;
            fail("unknown phase");
            return;
        }
    }
    fprintf(stderr, "level smoke: New Game through %s\n", k_phases[t.until].name);
    t.last_live = -1;
    k_phases[0].begin();
}

/* Verification aid (LEVEL_SMOKE.md "Frame captures"):
 * EM_LEVEL_SMOKE_PAGE_CAPTURE=<state>:<path.bmp> saves the second status
 * frame in a row whose ITEM > BATTERY page (UI+4 = 5, 002149F0) is at state
 * <state> (UI+5; 3 the acquisition notice, 4 the confirmation), for a look
 * beside the original's screenshot of that page. Once per run. */
static void page_capture(void)
{
    const char *pc = getenv("EM_LEVEL_SMOKE_PAGE_CAPTURE");
    const char *colon = pc ? strchr(pc, ':') : NULL;
    if (!colon || !colon[1] || t.page_capture_frames < 0)
        return;
    const EmStatusRuntime *status = em_area11_interaction_host_status();
    const EmStatusPage *page = status ? em_status_runtime_page(status) : NULL;
    if (page && page->phase == 3 && page->item.state == 5 &&
        page->item.step == (uint8_t)strtol(pc, NULL, 0)) {
        if (++t.page_capture_frames == 2) {
            em_gfx_request_capture(em_frame_gfx(), colon + 1);
            t.page_capture_frames = -1;
        }
    } else {
        t.page_capture_frames = 0;
    }
}

/* The main-loop counter of the last after-frame call (the every-tick
 * phases' guard). */
static uint32_t s_after_frame_counter = UINT32_MAX;

void em_level_smoke_test_after_frame(void)
{
    if (!t.active || t.failed)
        return;
    s_after_frame_counter = em_frame_counter();
    if (t.stop_pending) {
        stop_frame();
        return;
    }
    if (t.current > t.until || t.current >= PHASE_COUNT)
        return;
    if (em_scene_faulted(em_scene_state())) {
        fail("the scene coordinator faulted");
        return;
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(g.pos[axis]) || !isfinite(g.cam.eye[axis])) {
            fail("nonfinite player or camera");
            return;
        }
    page_capture();
    if (k_phases[t.current].frame() == 1 && !t.failed)
        next_phase();
}

void em_level_smoke_test_tick_end(void)
{
    if (!t.active || t.failed || t.current > t.until || t.current >= PHASE_COUNT ||
        !k_phases[t.current].every_tick || s_after_frame_counter == em_frame_counter())
        return;
    em_level_smoke_test_after_frame();
}

void em_level_smoke_test_task_end(void)
{
    if (!t.active || t.failed)
        return;
    if (stop_frame())
        return;
    if (t.stop_pending || t.current > t.until || t.current >= PHASE_COUNT ||
        k_phases[t.current].frame != exit_frame || t.step != 3)
        return;
    if (em_scene_faulted(em_scene_state())) {
        fail("the scene coordinator faulted");
        return;
    }
    if (exit_frame() == 1 && !t.failed)
        next_phase();
}

void em_level_smoke_test_scene_stopped(void)
{
    if (!t.active || t.failed)
        return;
    const EmSceneFault *fault = &em_scene_state()->fault;
    char reason[96];
    snprintf(reason, sizeof reason, "the scene coordinator faulted at %08X (code %d); the game task is stopped",
             (unsigned)fault->address, (int)fault->code);
    fail(reason);
}

int em_level_smoke_test_active(void) {return t.active;}
int em_level_smoke_test_failed(void) {return t.failed;}
