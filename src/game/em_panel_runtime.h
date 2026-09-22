/* Original power-panel owner and scripts attached to one shared player.
 * Status/menu dispatch remains a real host worker, never a completion timer. */
#ifndef EM_PANEL_RUNTIME_H
#define EM_PANEL_RUNTIME_H

#include "game/em_interaction_runtime.h"
#include "game/em_panel_program.h"

typedef struct {
    void *context;
    int (*align_player)(void *); /*001B6F00 and all00182F90 position mirrors */
    int (*message_start)(void *, uint32_t token, uint32_t delay);
    int (*message_done)(void *); /*0 waiting,1 actual phase2,-1 failure */
    int (*battery_open)(void *, EmPanel *owner, uint8_t request);
    int (*sound)(void *, uint32_t cue);
    int (*power)(void *, uint8_t mask);
    int (*stop_indicator)(void *);
} EmPanelRuntimeHooks;

typedef struct {
    EmPanel owner;
    EmPanelProgram program;
    EmInteractionRuntime *interaction;
    EmPanelRuntimeHooks hooks;
    int failed;
} EmPanelRuntime;

/* The adapter address is its shared-owner token and must remain stable.
 * Load returns1 on success. Required world workers must all be present. */
int em_panel_runtime_load(EmPanelRuntime *, const char *program_path,
    int completed, EmInteractionRuntime *, const EmPanelRuntimeHooks *);
/* Call only for the original use-scan winner, before any owner tick. */
int em_panel_runtime_arm(EmPanelRuntime *);
/* One pooled-owner callback. Actual status frames freeze this entire call,
 * including any pending script; the ordinary player worker is separate. */
int em_panel_runtime_tick(EmPanelRuntime *, int has_small_battery,
    int ordinary_tasks_enabled);
/* Normal release occurs at the following ordinary player callback. */
int em_panel_runtime_free(EmPanelRuntime *);

#endif
