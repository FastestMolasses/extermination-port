/* AREA01 resource/worker binding for the single live area-script host. */
#ifndef EM_AREA01_SCRIPT_LIVE_H
#define EM_AREA01_SCRIPT_LIVE_H
#include "game/em_area01_runtime.h"
#include "game/em_area11_script_host.h"
#include "game/em_module_loader.h"
#include "game/em_spawn_table.h"

#define EM_AREA01_SCRIPT_BOOT_PATH "assets/area01_boot_scripts"
#define EM_AREA01_SCRIPT_DEST_PATH "assets/area01/door_destinations.emsp"
typedef struct {
    EmArea01RuntimeHost host;
    EmModuleLoader *loader;
    /* Own boot data exactly once. The overlay descriptor borrows the drive. */
    EmScriptImage boot[4], overlay;
    EmSpawnTable destinations, timeline_tables;
    void *bank_ctx;
    int (*bank)(void *, EmActor *, uint32_t *value, int write);
    int bound;
} EmArea01Script;

/* Zero-initialize before first use. The host's worker is called with native
 * actor/player/camera state published, and must return in that same state.
 * It brackets any original-address worker with the live transaction API.
 * The boot EMSC files are exported from the pinned ELF; NULL paths use
 * the defaults. bind does not reset the shared host: call host_reset first. */
int em_area01_script_bind(EmArea01Script *, const EmArea01RuntimeHost *,
                         EmModuleLoader *, const char *boot_path, const char *dest_path);
void em_area01_script_free(EmArea01Script *);
/* Install after bind, before start/tick. Called in native/published state;
 * read uses the authoritative actor snapshot, write brackets the live
 * transaction and changes only +0x40. NULL keeps the existing Box owner. */
void em_area01_script_bank(EmArea01Script *, void *ctx,
                           int (*)(void *, EmActor *, uint32_t *, int write));
/* Optional canonical-memory provider for the boot script/data windows.
 * No record/overlay copy is made. Writes are limited to original scripts. */
uint8_t *em_area01_script_bytes(EmArea01Script *, uint32_t, uint32_t, int write);
/* Borrowed descriptor, also used by the shared host. Reacquires delivered
 * overlay bytes; rejects any overlay whose MWo3 id/address are not AREA01. */
EmScriptImage *em_area01_script_image(EmArea01Script *, uint32_t entry);
/* Register the canonical delivered player script banks (table rows 96..98).
 * Overlapping tails of a delivery are merged. No bytes are copied or owned;
 * the player's pose host must reset before replacement module delivery. */
int em_area01_script_player_banks(EmArea01Script *,
    int (*map)(void *, uint32_t address, uint32_t size, const uint8_t *), void *ctx);
#endif
