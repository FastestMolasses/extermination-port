/* Typed adapter for the exported original power-panel programs. Missing
 * player/camera/status/frame bindings are faults, never completion. */
#ifndef EM_PANEL_PROGRAM_H
#define EM_PANEL_PROGRAM_H
#include "game/em_panel.h"
#include "game/em_script.h"

typedef struct {
    void *context;
    /* Boolean hooks return1 only when the real operation was accepted;
     * zero is an unsupported/failed binding. message_done alone polls. */
    /* Original001B82D0 sub2/sub4, including its shared frame state.
     * Return an EmScriptCommandResult; the host owns the real handshake. */
    EmScriptCommandResult (*frame)(void *,EmScript *,const unsigned char *record);
    int (*camera_retarget)(void *); /*001B7B30 sub3: CBD0,solve5,solve1,A0=120 */
    int (*message_start)(void *,uint32_t token,uint32_t delay);
    int (*message_done)(void *); /*0 waiting,1 D2821B4==2,-1 host failure */
    int (*player_animation)(void *,uint16_t clip,float rate,float blend);
    int (*battery_open)(void *,EmPanel *owner,uint8_t request);
    int (*sound)(void *,uint32_t cue); /* original volume4096 on all axes */
    int (*power)(void *,uint8_t mask); /* OR into current area's D810841 */
} EmPanelProgramHooks;

typedef struct {
    EmScriptImage image;
    EmScript script;
    EmPanel *owner;
    EmPanelProgramHooks hooks;
    int failed;
} EmPanelProgram;

int em_panel_program_load(EmPanelProgram *program,const char *path,
                           EmPanel *owner,const EmPanelProgramHooks *hooks);
void em_panel_program_free(EmPanelProgram *program);
int em_panel_program_start(EmPanelProgram *program,EmPanelScript entry,
                            uint32_t message_token);
/* 0 yielded,1 finished,-1 unsupported/missing binding. */
int em_panel_program_tick(EmPanelProgram *program);
#endif
