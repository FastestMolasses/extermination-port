/* em_truck.c — the AREA-11 wedged truck (placement record 16), drawn
 * STATIC at its manifest placement. See em_truck.h for what the original
 * overlay behaviours 00823FF0 (truck) and 008251E0 (camera trigger) do and
 * why none of it runs here yet (WP-12).
 *
 * The only seam into the rest of the port is the render-chain draw publish
 * (em_game.c). The truck registers nothing with the moving-surface registry
 * and plays no sound. */

#include "em_truck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "em_collision.h"
#include "../em_model.h"
#include "../em_gfx.h"

/* 00823FF0 state byte (actor +0x04): its init leaves an unfallen truck in
 * state 4. The port never leaves that state until WP-12. */
#define TRUCK_STATE_NONE   0
#define TRUCK_STATE_WEDGED 4

typedef struct {
    int   present;     /* a truck is installed this scene */
    float pos[3];      /* placement position (actor +0xB0) */
    float rx;          /* placement Euler tilt */
    float ry;          /* placement yaw */

    /* render */
    int        has_mesh;
    EmModel    model;
    EmGfxMesh *mesh;
    float     *palette;
    EmGfx     *gfx;    /* owning gfx (for unload) */
} EmTruck;

static EmTruck t;

/* ----------------------------------------------------------------------- */

static void truck_unload_mesh(void)
{
    if (t.mesh && t.gfx) em_gfx_mesh_destroy(t.gfx, t.mesh);
    if (t.has_mesh)      em_model_free(&t.model);
    free(t.palette);
    t.mesh     = NULL;
    t.palette  = NULL;
    t.has_mesh = 0;
}

void em_truck_clear(EmGfx *gfx)
{
    if (gfx) t.gfx = gfx;
    truck_unload_mesh();
    memset(&t, 0, sizeof t);
}

/* truck_pose — pose the truck palette at its placement: T(pos) * R_y(ry) *
 * R_x(rx) onto a fresh copy of the model-local frame-0 palette. Baked once
 * at install; the truck does not move. */
static void truck_pose(void)
{
    if (!t.has_mesh || !t.palette) return;
    em_model_palette_at(&t.model, 0, 0.0, t.palette);

    const float cy = cosf(t.ry), sy = sinf(t.ry);
    const float cx = cosf(t.rx), sx = sinf(t.rx);
    for (uint32_t b = 0; b < t.model.bone_count; b++) {
        float *m = t.palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0];
            float y = m[col * 4 + 1];
            float z = m[col * 4 + 2];
            /* R_x (pitch about world X): rotate (y,z) */
            float y1 =  cx * y - sx * z;
            float z1 =  sx * y + cx * z;
            /* R_y (yaw about world Y): rotate (x,z1) */
            float x2 =  cy * x + sy * z1;
            float z2 = -sy * x + cy * z1;
            m[col * 4 + 0] = x2;
            m[col * 4 + 1] = y1;
            m[col * 4 + 2] = z2;
        }
        m[12] += t.pos[0];
        m[13] += t.pos[1];
        m[14] += t.pos[2];
    }
}

int em_truck_install(EmGfx *gfx, const char *model_path,
                     const float pos[3], float rx, float ry)
{
    em_truck_clear(gfx);            /* drop any prior truck */
    t.gfx     = gfx;
    t.present = 1;
    t.pos[0]  = pos[0];
    t.pos[1]  = pos[1];
    t.pos[2]  = pos[2];
    t.rx      = rx;
    t.ry      = ry;

    if (!model_path || em_model_load(&t.model, model_path) != 0) {
        printf("truck: mesh %s failed to load — the truck is ABSENT this "
               "scene\n", model_path ? model_path : "(none)");
        t.present  = 0;
        t.has_mesh = 0;
        return -1;
    }
    t.mesh = em_gfx_mesh_create(gfx, t.model.verts, t.model.vert_count,
                                t.model.indices, t.model.index_count,
                                (const EmGfxTexDesc *)t.model.texs,
                                t.model.tex_count, t.model.texels,
                                t.model.flags);
    t.palette = malloc(t.model.bone_count * 16 * sizeof(float));
    if (!t.mesh || !t.palette) {
        printf("truck: GPU/palette alloc failed — truck ABSENT\n");
        truck_unload_mesh();
        t.present = 0;
        return -1;
    }
    t.has_mesh = 1;
    truck_pose();                   /* bake the static placement pose */
    printf("truck: STATIC at (%.1f, %.1f, %.1f) rx %.3f ry %.3f — %u verts "
           "(00823FF0/008251E0 not translated; WP-12)\n",
           t.pos[0], t.pos[1], t.pos[2], t.rx, t.ry, t.model.vert_count);
    return 0;
}

int em_truck_present(void) { return t.present; }

void em_truck_update(const float player_pos[3])
{
    /* Static until WP-12: no trigger, no fall, no carry. The original
     * arm condition (player standing on the truck, 00823FF0) and the camera
     * trigger (008251E0) are not approximated here. */
    (void)player_pos;
}

int em_truck_draw(EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count)
{
    if (!t.present || !t.has_mesh || !t.mesh || !t.palette) return 0;
    *mesh       = t.mesh;
    *palette    = t.palette;
    *bone_count = t.model.bone_count;
    return 1;
}

int em_truck_state(void)
{
    return t.present ? TRUCK_STATE_WEDGED : TRUCK_STATE_NONE;
}

void em_truck_pos(float out[3])
{
    out[0] = t.pos[0];
    out[1] = t.pos[1];
    out[2] = t.pos[2];
}

/* ----------------------------------------------------------------------- */
/* Headless self-test (EM_TRUCK_TEST=1), no GPU/scene. Asserts the interim
 * contract: a placed truck stays in the wedged state at its placement for
 * many frames whether the player is far away, inside the 008251E0 camera
 * trigger bands, or standing on top, and it registers no moving surface
 * (a rider on top is not moved). */

static int trk_check(int cond, const char *what, int *fail)
{
    if (cond) return 1;
    (*fail)++;
    printf("truck test: CHECK FAILED — %s\n", what);
    return 0;
}

int em_truck_static_selftest(void)
{
    int fail = 0;

    /* Seat a mesh-less truck directly (no GPU): the placement from the
     * scene_snow manifest line. */
    memset(&t, 0, sizeof t);
    t.present = 1;
    t.pos[0]  = 380.8f; t.pos[1] = 164.0f; t.pos[2] = 391.1f;
    t.rx      = -0.314f;
    t.ry      = 1.5708f;
    const EmTruck before = t;

    /* far away; inside the 008251E0 band X(319,336) Z(390,427); on top */
    const float players[3][3] = {
        { 250.0f, 230.0f, 209.0f },
        { 327.0f, 230.0f, 408.0f },
        { 380.8f, 197.6f, 391.1f },
    };
    for (int k = 0; k < 3; k++) {
        for (int frame = 0; frame < 240; frame++) {
            em_collision_moving_clear();
            em_truck_update(players[k]);
            float rider[3] = { players[k][0], players[k][1], players[k][2] };
            int carried = em_collision_moving_carry(rider);
            if (!trk_check(carried == 0 && rider[0] == players[k][0] &&
                           rider[1] == players[k][1] &&
                           rider[2] == players[k][2],
                           "no moving surface is registered", &fail))
                break;
        }
        trk_check(em_truck_state() == TRUCK_STATE_WEDGED,
                  "the truck stays wedged (state 4)", &fail);
        trk_check(t.present == before.present &&
                  t.pos[0] == before.pos[0] && t.pos[1] == before.pos[1] &&
                  t.pos[2] == before.pos[2] && t.rx == before.rx &&
                  t.ry == before.ry,
                  "the truck placement is unchanged", &fail);
    }

    printf("truck test: %s\n", fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);

    memset(&t, 0, sizeof t);   /* leave the module clean */
    return fail;
}

/* EM_TRUCK_TEST (legacy self-test) retired 2026-09-23. */
