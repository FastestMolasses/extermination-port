/* em_camera_live.h - the live walking camera (census L13..L16,
 * docs/CAMERA_LIVE.md).
 *
 * This module is the binder of the original camera translations:
 *   em_camera_leftovers       0018B9C0 (the camera frame), 0018BC20 (the
 *                             action dispatch), 001916C0, 00191000, 00193D90,
 *                             0022FCA0, 00230000, 00194D10, the solvers
 *                             0018DD20 / 0018F870 / 0018CE60 / 0018D910
 *   em_camera_follow_original 001921D0, 0018D7B0, 0018D330, 00191390 and the
 *                             chases 0018C6A0 / 0018C4B0 / 00191D40 / 00192010
 *   em_camera_area11_specials 00195130 (camera action 0), 00193EB0, 001936E0
 *   em_camera_commit_original 0018C0D0 (the commit), 00193660
 *   em_census_standins        00102CD0 (the look-at the commit calls)
 *   em_script_host_workers    001B0460 (the area-load re-seat) with
 *                             em_script_door_fan's 001B0080
 * over ONE canonical storage of the original bytes: the camera block
 * 0x008101E0..0x008102AF, the vector pool D_008105D0..D_008106A3 and the
 * camera's scratchpad words (0x700038A0..0x70003A3F, 0x70003400, 0x70003600,
 * 0x70003630, 0x700031B0). The three modules' own scratch structs are views
 * of that storage, loaded and stored at every call between modules.
 *
 * The port's legacy camera struct g.cam (EmCamera, em_game_internal.h) is a
 * VIEW of the same bytes for the modules that still read or write it (the
 * interaction and script hosts, the opening runtime, the renderer): every
 * entry point here loads g.cam's fields into the original bytes first and
 * stores them back afterwards; the field map is in em_camera_live.c
 * (docs/CAMERA_LIVE.md section 3).
 *
 * Scope: the scene with the original AREA11 roster (the first level). A
 * scene without an original collision world keeps em_camera.c's legacy
 * camera (outside the first level). The commit 0018C0D0 serves every
 * scene. */
#ifndef EM_CAMERA_LIVE_H
#define EM_CAMERA_LIVE_H

#include <stdint.h>

#include "game/em_camera_follow_original.h"
#include "game/em_interaction_projection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the camera reads from the rest of the port, supplied by the binder
 * (em_scene_bindings.c in the game; a fixture supplies its own). */
typedef struct EmCameraLiveHost {
    void *context;
    /* The live player record D_008102B0 (its +A0 / +B0 / +C4 are replaced
     * by the port's canonical placement, pose hip and heading). */
    const EmPlayerLiveActor *(*player)(void *context);
    /* The player's bone-1 position (0015BCF0's +B0 and 0x70003B40) and its
     * published Euler angles (0x70003B50 x/y/z): 1, or 0 while none is
     * available (+B0 then reads the placement, 0x70003B50 the record's
     * +C0 / heading / +C8: substitutions for the pose the port does not
     * evaluate before first control, docs/CAMERA_LIVE.md section 5). */
    int (*hip)(void *context, float out[3]);
    int (*euler)(void *context, float out[3]);
    /* The pad assignment block 0x70003B74.. (word 6 is 0x70003B80), or
     * NULL (then 0x70003B80 reads 0). */
    const uint16_t *(*pad_config)(void *context);
    /* The scratchpad word 0x700031F0 (its low byte is read). */
    const int32_t *carry31F0;
    /* Camera action 0's legacy pre-emption (em_camera.h
     * camera_area11_standins over the g.cam view): CAMERA_STANDIN_*. */
    int (*standins)(void *context);
    /* 0022EEF0(cam, 1), the scripted timeline of +4 == 3, over the g.cam
     * view. 0, or -1 on a fault. */
    int (*timeline)(void *context);
} EmCameraLiveHost;

/* Build the camera worlds for the area just loaded (after the collision
 * world and the player stage are bound; 001AF5C0's position). The camera
 * block and pool are left as they are (001B0460 writes them next). 0, or -1
 * when the collision world, a host slot or a table the camera needs is
 * missing (assets/camera_tables.emrg, tools/export_camera_tables.py). */
int em_camera_live_bind(const EmCameraLiveHost *host);
/* 1 while the live camera runs the scene's camera frame (AREA11). */
int em_camera_live_bound(void);

/* 0018B9C0(D_008101E0): the camera frame at its scene worker position. 0,
 * or -1 with the scene fault latched. */
int em_camera_live_frame(void);

/* 0018C0D0(D_008101E0, mode) over the canonical storage, then the port's
 * view matrix (em_cs_view_to_native) and projection (g.viewproj from the
 * zoom). Every scene. 0, or -1 with the fault latched. */
int em_camera_live_commit(int mode);

/* 0018D7B0(D_008101E0, style) (the frame machine's state-4 re-seat solve).
 * 0, or -1 with the fault latched. */
int em_camera_live_solve(int style);

/* 001B7B30 op0D sub 3 / 4 / 5 after 0018CBD0 has seeded cam+10 / cam+20 /
 * cam+30 (the legacy g.cam view): 0018D7B0(cam, 5), 0018D7B0(cam, 1), the
 * halfword cam+A0 = 0x78. 1, or 0 on a fault. */
int em_camera_live_scripted_retarget(void);

/* 001B0460(a0) at the placement 001B07C0 makes (area load and room move):
 * the record reader, 001B0250 and the player's placed +A0 / +C0 come from
 * the caller (em_scene_bindings.c's spawn binding). */
typedef struct EmCameraLiveRoom {
    void *context;
    /* A word of the room camera table window (D_0024D650's records):
     * 0, or -1 outside the exported window (a fault). */
    int (*read_word)(void *context, uint32_t address, uint32_t *out);
    /* 001B0250(): the area flags D_008106C8 (the spawn translation). */
    int (*w_001B0250)(void *context);
    const float *d810350;   /* player +0xA0 quad (the placement) */
    const float *d810370;   /* player +0xC0 quad */
    const int32_t *d8104E0; /* player +0x230 */
} EmCameraLiveRoom;
int em_camera_live_001B0460(int a0, const EmCameraLiveRoom *room);

/* The canonical bytes -> the g.cam view and the renderer's view matrix
 * (after a direct write through em_camera_live_bytes, e.g. a fixture's
 * captured camera). */
void em_camera_live_view_publish(void);

/* The canonical words, by original address (0x008101E0..0x008102AF and
 * 0x008105D0..0x008106A3), or NULL for any other address. */
uint8_t *em_camera_live_bytes(uint32_t address, uint32_t size);
/* The render-context projection record 001DD980 publishes (+0x2450..+0x2467
 * of *D_00275670); one storage for every publisher. */
EmInteractionProjection *em_camera_live_projection(void);

/* 0, or the address of the first missing or failing callee of the last
 * failed call (for the fault report). */
uint32_t em_camera_live_fault(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_CAMERA_LIVE_H */
