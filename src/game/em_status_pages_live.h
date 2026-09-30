/* em_status_pages_live - the live binding of the status pages
 * (docs/STATUS_PAGES.md section 5): the translations of em_status_pages_*
 * and em_area01_ui_* run over the port's own storage of the original
 * bytes they address, and every callee they reach is dispatched, by its
 * original address, to the port's translation of it.
 *
 * Entries (one call each frame the original calls them):
 *   0020F950 MAP, 00211970 SPR4, 00214020 DATABASE (0020CDC0 phase 3
 *   sub-state 2, page id 1..3); 00214570 EQUIPMENT, 00215870 EVENT,
 *   002160B0 HEALING (0020EE50 states 4, 6, 7; 002160B0 gets the caller's
 *   s0, which 0020EE50 holds t in). MAP's 22 UI-pool nodes run 002101C0
 *   from the pool walk 001B0000 (em_status_models' node hook), over the
 *   same views plus their record.
 *
 * Memory (EmArea01Ui views, searched in this order; any other address a
 * translation reaches faults):
 *   D_00810130 (0xA0)          the status block (the runtime's; its UI+0x20
 *                              word is the shared 00208AD0 clock)
 *   D_008106B0 (0x48)          the request bytes (EmSceneState.req)
 *   D_00810700 (0x640)         only the migrated ranges of the progress block
 *                              (em_scene_progress_canonical)
 *   D_00810858 / 5C            health / infection (g.status, per call)
 *   D_008104D0                 the player record's +0x220, the same health
 *   D_00810C61 / C62 / CB4     em_weapon's fire mode, magazine and reserve
 *   D_008111D0 (0x10)          001C5FB0's result buffer
 *   D_0028A49C, *D_0028A49C    the record container (module 0 slot 3)
 *   D_00810E64 / E65, E70 / E74 / E78   the stick and the pad words
 *   D_002821B0 (0x9C)          the message block (em_message_live's)
 *   D_00275BD8                 the page core's module-busy byte
 *   the .data windows           assets/status_pages (tools/export_status_pages.py)
 *   D_002862C0 (0x40)          the number string buffer (bss)
 *   0x70003B64 / 0x70003B8D    the main-loop counter and the mode byte
 *   0x70003400..0x70003C00     the scratchpad work area
 *   0x010202B0                 002160B0 state 4's s0 probe (below)
 *   the EE stack               0x01FF0000..0x02000000 (the frames the
 *                              routines carve and hand to callees)
 *   MAP (0020F950 / 002101C0 / 00210030 / 00210F30 / 00211400):
 *   D_00810700..702            the area bytes (EmSceneState)
 *   D_0028B020 (24 x 0x2F0)    the UI pool, em_status_models' records
 *   D_00810610 (0x40)          the models' view matrix (the camera pool's)
 *   D_0028A570                 the MAP bank word: 0 until module 0x1E
 *                              loads, then the relocated slot 0x38 (the
 *                              loader's slot word, frame d28A570, or
 *                              without a loader em_gs_texture_word;
 *                              read-only)
 *   D_00275BCC                 the free bone-slot count, as the port's
 *                              slots stand for the original's stack: the
 *                              free slots of em_status_models' pool
 *   D_00810350 (0xC), D_00810374   the player record's +0xA0..+0xA8 and
 *                              +0xC4 (g.pos, g.yaw; read-only)
 *   D_00275670, context +0x2468   the render context word and its zoom
 *                              (em_render_context_live; read-only)
 * The .data windows and the other read-only views are compared after
 * every call: a store into one faults.
 *
 * 002160B0 state 4 reads the byte t + 0x50 + s0 with s0 = t, i.e.
 * 0x010202B0, which holds 0x2B in all 16 route images and the status-hub
 * image (STATUS_PAGES.md section 2): the view holds that captured byte.
 *
 * Callees: see em_status_pages_live.c's dispatcher table. A callee the
 * first level cannot reach faults with its address (docs/STATUS_PAGES.md
 * section 1 proves each unreachable). */
#ifndef EM_STATUS_PAGES_LIVE_H
#define EM_STATUS_PAGES_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_gs_texture.h"
#include "game/em_item_trail.h"
#include "game/em_message_service.h"
#include "game/em_status_draw.h"

typedef struct EmStatusPagesLive EmStatusPagesLive;

/* The host's side (em_battery_page_live's convention): every worker
 * returns 0 on success and a negative value on a fault. */
typedef struct {
    void *context;
    /* 001FB9F0(id, a1, a2, a3) */
    int (*sound)(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    /* 001FCB90(x, y, group, line), the help presenter 001FCF10 calls */
    int (*present)(void *context, int32_t x, int32_t y, int32_t group, int32_t line);
    /* 00185420(item): *owner = the record address, 0 for none */
    int (*find_device)(void *context, int32_t item, uint32_t *owner);
    /* 0015C700(D_008102B0) after the page's health stores (g.status holds
     * them): table16 reads a halfword of the .data windows */
    int (*player_0015C700)(void *context, int (*table16)(void *, uint32_t, int16_t *),
                           void *table_context);
    /* The MAP page's 001B0000 model draws at their place in the page's
     * stream: draw what the walk queued (em_status_models_render) inside
     * the SCISSOR_1 window `clip` (x0, y0, x1, y1 on the 512 x 448
     * canvas). 1 drawn. */
    int (*models_draw)(void *context, EmGfx *gfx, const float clip[4]);
} EmStatusPagesHost;

/* Indices of the request bytes the page core keeps a view of. */
enum {
    EM_STATUS_PAGES_REQ_B0 = 0x00, /* D_008106B0 */
    EM_STATUS_PAGES_REQ_B1 = 0x01, /* D_008106B1 */
    EM_STATUS_PAGES_REQ_C5 = 0x15, /* D_008106C5 */
    EM_STATUS_PAGES_REQ_CC = 0x1C  /* D_008106CC */
};

/* The storage one call runs over (see the header). */
typedef struct {
    uint8_t *ui;                   /* D_00810130, 0xA0 bytes */
    uint32_t *ui_clock;            /* UI+0x20 as the runtime keeps it */
    uint8_t *busy;                 /* D_00275BD8 */
    uint8_t *req;                  /* D_008106B0, 0x48 bytes */
    uint8_t *progress;             /* D_00810700, 0x640 bytes */
    EmMessageBlock *message;       /* D_002821B0 */
    uint8_t *spad3B8D;             /* 0x70003B8D */
    float *health, *infection;     /* D_00810858 / 5C */
    uint8_t warning;               /* D_008104E4 (00208AD0's colour) */
    uint32_t counter;              /* 0x70003B64 */
    uint16_t held, pressed, repeat; /* D_00810E70 / 74 / 78 */
    uint8_t stick_x, stick_y;      /* D_00810E64 / 65 */
    const EmStatusHealthData *health_data; /* 00208AD0's resident records */
    /* 001FF080(0, module): the page core's module worker (clears the busy
     * byte when the load completes). 1 accepted. */
    int (*module_load)(void *context, uint32_t module);
    void *module_context;
    /* SPR4: em_weapon's storage of D_00810C62 (the loaded magazine) and
     * D_00810CB4 (the reserve), and its fire mode D_00810C61 (read before,
     * set after a store; a value em_weapon refuses faults) */
    uint8_t *c62;
    int16_t *cb4;
    uint8_t (*fire_mode)(void);
    void (*set_fire_mode)(uint8_t mode);
    /* 0020D930 / 0020AC70: 001B62C0's stick math and the shared trail
     * D_00821300 / D_00275C90 (the runtime's), drawn by the page */
    const EmItemMath *math;
    EmItemTrail *trail;
    /* MAP: the UI pool and its model workers (em_status_models), and the
     * player record's +0xA0..+0xA8 / +0xC4 (g.pos / g.yaw) */
    struct EmStatusModels *models;
    const float *player_position, *player_yaw;
    uint8_t *area; /* D_00810700..702, the scene state's area bytes */
    /* D_0028A570 = D_0028A490[0x38]: the live screen-module loader's slot
     * word, which its 001FF830 state 7 relocates when module 0x1E loads
     * (the one storage while a loader is bound). NULL: em_gs_texture's
     * copy of that relocation (tools/export_status_pages.py; the
     * fixtures and tests that bind no loader). */
    const uint32_t *d28A570;
} EmStatusPagesFrame;

/* Loads the data (assets/status_pages/status_pages.emsp). NULL when it is
 * missing or invalid. */
EmStatusPagesLive *em_status_pages_live_load(const char *path, const EmStatusPagesHost *host);
void em_status_pages_live_free(EmStatusPagesLive *live);
/* The GS memory the page textures decode from (the runtime applies the
 * module loads and the 00200970(1) restore to it). */
EmGsTexture *em_status_pages_live_gs(EmStatusPagesLive *live);
/* One call of the page at original address `page` (see the header). 0, or
 * -1 on a fault (reported on stderr with the original address). */
int em_status_pages_live_tick(EmStatusPagesLive *live, uint32_t page, const EmStatusPagesFrame *frame);
/* The frame's draw stream (em_page_draw). */
int em_status_pages_live_render(EmStatusPagesLive *live, EmGfx *gfx);
void em_status_pages_live_deactivate(EmStatusPagesLive *live);
/* For tests: the calls the last tick made, by original callee address. */
unsigned em_status_pages_live_calls(const EmStatusPagesLive *live, uint32_t callee);

#endif
