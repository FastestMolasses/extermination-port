/* em_area_title.h - the area-title node 001C5930 bound live
 * (docs/STATUS_UI_LEFTOVERS.md 2.6, "Binding").
 *
 * The node's behaviour is em_sul_001C5930 (em_status_ui_leftovers: the .s
 * of 001C5930, verified by tools/test_status_ui_leftovers_reference.py) over
 * the node's own record bytes: +0x04 (state), +0x05 (title phase), +0x06
 * (band phase), +0x28 (title timer), +0x2A (title string index), +0x1F0
 * (band) and +0x1F4 (band timer). Its workers:
 *   001CC170  em_message_live_cc170 (the message service's 001CC170)
 *   001CC1E0  em_message_live_cc1e0 (its 001CC1E0 glyph runs; the passes
 *             are drawn with the frame's message glyphs)
 *   001AFC10  the caller's free of the node's record
 * Its memory: D_00810700 / 701 and the scratchpad mode byte 0x70003B8D
 * from the scene state, D_008106B8 (the scene's B8 byte), D_008104D8 (the
 * player's infection float, g.status.infection), D_00289B40 (built at
 * load by em_sul_001B0BA0 from the exported D_0024A850 counts, as 001B0BA0
 * builds it at boot), and the ELF data tools/export_area_title.py exports
 * (assets/area_title.emat): the pointer tables D_002671C0 / D_0026726C, the
 * strings they name and the style record D_00265520.
 *
 * Fail-stop: a missing export, a read outside the regions or a faulting
 * worker latches (em_area_title_fault) and returns -1. */
#ifndef EM_AREA_TITLE_H
#define EM_AREA_TITLE_H

#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA_TITLE_EXPORT "assets/area_title.emat"

/* Load the export (once; later calls return the first result). 0, or -1. */
int em_area_title_load(void);

/* The node's behaviour 001C5930 over `node` (a pool record whose +0x10 is
 * 001C5930). `free_node` is 001AFC10 (it frees the record; the node is not
 * touched after it). `infection` is D_008104D8's float. 1 ran (0 when it
 * freed the node), -1 latched. */
typedef int (*EmAreaTitleFree)(void *ctx, EmActor *node);
int em_area_title_001C5930(EmActor *node, const EmSceneState *scene, float infection,
                           EmAreaTitleFree free_node, void *ctx);

/* The node's title / band fields (the tick log), 0 when `node` holds none. */
typedef struct {
    uint8_t state, title_phase, band_phase;
    int16_t title_timer, title_index;
    int32_t band;
    int16_t band_timer;
} EmAreaTitleRecord;
int em_area_title_record(const EmActor *node, EmAreaTitleRecord *out);

uint32_t em_area_title_fault(void);
/* The 001CC1E0 lines drawn since start-up (the tick log). */
uint32_t em_area_title_lines(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA_TITLE_H */
