/* Original event-script control flow (func_001BA1A0 / 001BA1F0).
 * Commands remain 64-byte little-endian records in user-exported assets.
 * This module owns instruction sequencing; typed engine handlers own
 * camera, actors, audio and fades. It does not turn missing handlers into
 * successful commands. */
#ifndef EM_SCRIPT_H
#define EM_SCRIPT_H

#include <stdint.h>

#define EM_SCRIPT_RECORD_SIZE 64

typedef struct {
    int32_t active;          /* actor+0x1F0: >0 running, -1 finished */
    int32_t phase;           /* actor+0x1F4: per-command state */
    uint32_t pc;             /* actor+0x1F8: original address */
    int8_t skip_phase;       /* actor+0x1FC */
    /* The interpreter's per-tick VIEW of scratchpad 0x70003B91, whose one
     * storage is the canonical EmSceneState byte (design 3.2): the host
     * publishes the canonical value here before each tick and writes both
     * when its command writes 3B91; this field is never written back. The
     * 1 -> 2 promotion is 001AE6B0's (0x1AE6E0), on the canonical byte. */
    uint8_t skip_request;    /* per-tick view of canonical 3B91 */
} EmScript;

typedef enum {
    EM_SCRIPT_WAIT = 0,
    EM_SCRIPT_ADVANCE = 1,
    EM_SCRIPT_CONTINUE = 2,
    EM_SCRIPT_ABORT = 3,
    EM_SCRIPT_UNSUPPORTED = -1
} EmScriptCommandResult;

typedef enum {
    EM_SCRIPT_YIELDED = 0,
    EM_SCRIPT_FINISHED = 1,
    EM_SCRIPT_ABORTED = 3,
    EM_SCRIPT_FAULT = -1
} EmScriptResult;

typedef unsigned char *(*EmScriptResolve)(void *context, uint32_t address);
typedef EmScriptCommandResult (*EmScriptExecute)(void *context,
        EmScript *script, unsigned char record[EM_SCRIPT_RECORD_SIZE]);

typedef struct {
    unsigned char *bytes;
    uint32_t base, entry, length;
} EmScriptImage;

int em_script_image_load(EmScriptImage *image, const char *path);
void em_script_image_free(EmScriptImage *image);
unsigned char *em_script_image_read(EmScriptImage *image, uint32_t address,
                                    uint32_t length);

void em_script_start(EmScript *script, uint32_t address);
EmScriptResult em_script_tick(EmScript *script, EmScriptResolve resolve,
                             EmScriptExecute execute, void *context);
uint32_t em_script_u32(const unsigned char *record, unsigned offset);
float em_script_f32(const unsigned char *record, unsigned offset);

#endif
